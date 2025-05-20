/*
 * Copyright 2022 NXP
 * NXP confidential.
 * This software is owned or controlled by NXP and may only be used strictly
 * in accordance with the applicable license terms.  By expressly accepting
 * such terms or by downloading, installing, activating and/or otherwise using
 * the software, you are agreeing that you have read, and that you agree to
 * comply with and are bound by, such license terms.  If you do not agree to
 * be bound by the applicable license terms, then you may not retain, install,
 * activate or otherwise use the software.
 */

#include "chip.h"
#include "stopwatch.h"
#include "lpcsdr_usb.h"
#include "lpcsdr_dma.h"
#include "lpcsdr_hsadc.h"
#include "lpcsdr_spifi.h"
#include "lpcsdr_gpio.h"
#include "lpcsdr_ipc.h"
#include "lpcsdr_tuner.h"
#include "lpcsdr_protocol.h"
#include "lpcsdr_uart.h"
#include "lpcsdr_panic.h"
#include <string.h>

static bool bulk_test_mode = false;
static bool fast_cpu = false;
static bool rf_power = false;

#define USB_BLOCK_SIZE ALIGN_TO(sizeof(ep1_header_t) + (HSADC_BUFFER_SIZE * 3 / 4), 512)

/* callback from USB code to indicate it's got a free buffer available */
void lpcsdr_usb_space_available(void)
{
    if (bulk_test_mode)
        lpcsdr_ipc_send_m4(M4_QUEUE_TEST_DATA, 0, 0, 0);
}

/* callback from USB code to indicate the USB connection state changed (USB reset or reconfiguration) */
void lpcsdr_usb_state_changed(void)
{
    /* no-op for now */
}

/* callback from DMA code to indicate there's a new HSADC buffer waiting to be copied */
bool lpcsdr_dma_hsadc_buffer_ready(dma_lli_t *buffer, uint32_t status)
{
    return lpcsdr_ipc_send_m4(M4_COPY_HSADC_BUFFER, (uint32_t) buffer, status, 0);
}

/* Given `count` words of HSADC samples in `src`, with 8 12-bit samples per 4 words,
 * copy and pack the samples into `dst` with 8 12-bit samples per 3 words.
 */
static void pack_samples(const uint32_t *src, uint32_t *dst, uint32_t count)
{
#if 0
    for (; count > 3; src += 4, dst += 3, count -= 4) {
        dst[0] = (src[0] & ~0xF000F000) | ((src[3] & 0x0F000F00) << 4);
        dst[1] = (src[1] & ~0xF000F000) | ((src[3] & 0x00F000F0) << 8);
        dst[2] = (src[2] & ~0xF000F000) | ((src[3] & 0x000F000F) << 12);
    }
#else
    // hand-rolled assembly implementing the same loop as above

    // pre-conditioning of `count` lives on the C side, so the compiler can
    // do constant propagation etc.
    count /= 4;
    if (!count)
        return;

    uint32_t scratch;
    __asm__ volatile (
            "1: ldm %[src]!, {r3,r4,r5,r6}\n\t"          // load 4 words (8 samples)
            "bic r3,r3,#0xF000F000\n\t"                  // clear high 4 bits of first 6 samples (could be omitted if we trust the hardware)
            "bic r4,r4,#0xF000F000\n\t"                  //   --"--
            "bic r5,r5,#0xF000F000\n\t"                  //   --"--
            "and %[scratch],r6,#0x0F000F00\n\t"          // extract bits 11:8 of samples 7/8
            "orr r3,r3,%[scratch],lsl #4\n\t"            //  and insert into the top 4 bits of samples 1/2
            "and %[scratch],r6,#0x00F000F0\n\t"          // extract bits 7:4 of samples 7/8
            "orr r4,r4,%[scratch],lsl #8\n\t"            //  and insert into the top 4 bits of samples 3/4
            "and %[scratch],r6,#0x000F000F\n\t"          // extract bits 3:0 of samples 7/8
            "orr r5,r5,%[scratch],lsl #12\n\t"           //  and insert into the top 4 bits of samples 5/6
            "stm %[dst]!, {r3,r4,r5}\n\t"                // store 3 words
            "subs %[count],%[count],#1\n\t"              // loop if more data
            "bne 1b\n\t"
            : /* outputs */
              [src] "+r" (src),
              [dst] "+r" (dst),
              [count] "+r" (count),
              [scratch] "=&r" (scratch)
            : /* inputs */
            : /* clobber */ "r3", "r4", "r5", "r6", "cc", "memory");
#endif
}

static uint32_t pending_usb_status; /* Status bits waiting to be sent in the next successfully-queued block */

static void m4_copy_hsadc_buffer(const ipc_message_t *message)
{
    dma_lli_t *buffer = (dma_lli_t*) message->values[0];
    uint32_t dma_status = message->values[1];

    pending_usb_status |= dma_status; /* accumulate status bits */

    uint32_t start_seq = buffer->sequence;
    if (buffer->status & LLI_STATUS_CLOBBERED) {
        /* buffer got clobbered while it was waiting on the IPC queue, drop data.
         * we do this check early to avoid doing redundant work -- the same
         * check also happens again after copying/packing is complete.
         */
        pending_usb_status |= BLOCK_STATUS_PACKING_OVERRUN;
        lpcsdr_dma_hsadc_copy_complete(buffer, false);
        return;
    }

    USB_DTD_T *dTD = lpcsdr_usb_get_dtd();
    if (!dTD) {
        /* No available USB buffer, host is not keeping up, drop data */
        pending_usb_status |= BLOCK_STATUS_USB_OVERRUN;
        lpcsdr_dma_hsadc_copy_complete(buffer, false);
        return;
    }

    static_assert(USB_BLOCK_SIZE <= DTD_BUFFER_SIZE);

    /* Fill in USB block header */
    ep1_header_t *header = (ep1_header_t*) dTD->buffer;
    header->magic = 0xDEADBEEF;
    header->block_len = USB_BLOCK_SIZE;
    header->samples = HSADC_BUFFER_SIZE / 2;
    header->sequence = start_seq;
    header->status = pending_usb_status;

    /* Pack samples following the header */
    uint32_t *out_samples = (uint32_t*) (header + 1);

    const uint32_t in_words = HSADC_BUFFER_SIZE/4;
    static_assert(in_words % 4 == 0);
    pack_samples((uint32_t *)buffer->destaddr, out_samples, in_words);

    /* Zero out trailing data up to the 512-byte boundary */
    const uint32_t out_words = in_words * 3 / 4;
    const uint32_t used = sizeof(*header) + out_words * 4;
    const uint32_t pad = USB_BLOCK_SIZE - used;
    if (pad > 0) {
        memset(out_samples + out_words, 0, pad);
    }

    /* We're done copying to the USB buffer. Check that the source buffer is
     * still valid - it may have started to get clobbered while we were halfway
     * through the copy. If it was clobbered, we don't know that we got a good copy,
     * so discard the buffer.
     */
    memory_barrier();
    uint32_t status = lpcsdr_dma_hsadc_copy_complete(buffer, true);
    if (buffer->sequence == start_seq && !(status & LLI_STATUS_CLOBBERED)) {
        /* we copied everything out successfully with no clobber, send the data */
        lpcsdr_usb_queue_dtd(dTD, USB_BLOCK_SIZE);
        pending_usb_status = 0;
    } else {
        /* clobbered during the copy, drop data and return the dTD to the pool */
        pending_usb_status |= BLOCK_STATUS_PACKING_OVERRUN;
        lpcsdr_usb_free_dtd(dTD);
    }
}

/* a little linear congruential PRNG, just to get some randomness in the USB data we transfer */
static uint32_t random_state = 123456789;
static void random_fill_word(uint8_t *buffer, unsigned size)
{
    uint32_t *u32 = (uint32_t*) buffer;
    for (unsigned i = 0; i < size/4; ++i) {
        random_state = random_state * 0xD9F5 + 1;
        *u32++ = random_state;
    }
}

static void m4_queue_test_data()
{
    while (bulk_test_mode) {
        USB_DTD_T *dTD = lpcsdr_usb_get_dtd();
        if (!dTD)
            break;
        random_fill_word(dTD->buffer, DTD_BUFFER_SIZE);
        lpcsdr_usb_queue_dtd(dTD, DTD_BUFFER_SIZE);
    }
}

static void handle_clock_change(void)
{
    SystemCoreClockUpdate();
    StopWatch_Init();
    lpcsdr_i2c_clock_update();
}

static void setup_clocks(void)
{
    Chip_SetupCoreClock(CLKIN_CRYSTAL, 48000000, false);
    Chip_Clock_SetBaseClock(CLK_BASE_APB1, CLKIN_MAINPLL, true, false);
    Chip_Clock_SetBaseClock(CLK_BASE_APB3, CLKIN_MAINPLL, true, false);
    handle_clock_change();
}

static void set_slow_cpu(void)
{
    if (!fast_cpu)
        return;

    fast_cpu = false;
    Chip_SetupCoreClock(CLKIN_CRYSTAL, 48000000, false);
    handle_clock_change();
}

static void set_fast_cpu(void)
{
    if (fast_cpu)
        return;

    fast_cpu = true;
    Chip_SetupCoreClock(CLKIN_CRYSTAL, 110000000, false);
    handle_clock_change();
}

static void set_rf_power_off(void)
{
    if (!rf_power)
        return;

    rf_power = false;
    lpcsdr_tuner_handle_poweroff();
    lpcsdr_set_rfen(false);
    lpcsdr_led_set(0, C_OFF);
}

static void set_rf_power_on(void)
{
    if (rf_power)
        return;

    rf_power = true;
    lpcsdr_set_rfen(true);
    lpcsdr_tuner_handle_poweron();
    lpcsdr_led_set(0, C_ON);
}

/* Measure the frequency of a clock input using the CGU's FREQ_MON registry.
 * This produces a frequency estimate relative to the internal IRC 12MHz clock.
 *
 * clkin: the CLKIN_* constant for the clock input to measure (should be a real clock input, not CLKINPUT_PD)
 * cycles: approximate number of IRC clock cycles to measure over
 * *rcnt_sum: on return, total number of clock cycles of the IRC clock seen over the measurement period
 * *fcnt_sum: on return, total number of clock cycles of the measured input seen over the measurement period
 *
 * The measured clock frequency is approximately (12e6 * (*fcnt_sum) / (*rcnt_sum))
 */
static void measure_frequency_raw(CHIP_CGU_CLKIN_T clkin, uint32_t cycles, uint32_t *rcnt_sum, uint32_t *fcnt_sum)
{
    uint32_t r_sum = 0, f_sum = 0;

    uint32_t rcnt_initial = 0x1FF;
    while (r_sum < cycles) {
        LPC_CGU->FREQ_MON =
                rcnt_initial | /* RCNT */
                ((clkin & 0x1F) << 24); /* CLK_SEL */
        for (int delay = 100; delay; --delay)
            ;
        LPC_CGU->FREQ_MON |= _BIT(23); /* set MEAS */

        unsigned timeout = 200000;
        uint32_t stat;
        while ( ((stat = LPC_CGU->FREQ_MON) & _BIT(23)) && --timeout )
            __NOP();
        if (!timeout)
            break;

        uint32_t rcnt = rcnt_initial - (stat & 0x1FF);
        uint32_t fcnt = (stat >> 9) & 0x3FFF;

        r_sum += rcnt;
        f_sum += fcnt;

        if (fcnt == 0x3FFF) {
            /* measurement stopped because fcnt saturated;
             * for subsequent loops, use a smaller rcnt.
             *
             * This is because we only measure an exact
             * count for whichever counter saturates first;
             * the other counter might be mid-clock-cycle
             * when saturation happens, producing a measurement
             * error. So we prefer the counter for the slower
             * clock (longer clock cycle) to saturate first, to
             * reduce the measurement error.
             */
            rcnt_initial = rcnt - rcnt/16;
            r_sum = f_sum = 0;
        }
    }

    *rcnt_sum = r_sum;
    *fcnt_sum = f_sum;
}

/* Measure the frequency of a given clock input, using the 12MHz crystal oscillator
 * as a reference.
 *
 * This measures both the crystal oscillator and the requested clock input against
 * the internal IRC clock using measure_frequency_raw, then returns an adjusted
 * measurement assuming that the crystal is at exactly 12MHz.
 *
 * Returns a frequency in Hz, or 0 if something went wrong
 */
static uint32_t measure_frequency(CHIP_CGU_CLKIN_T clkin)
{
    if (clkin >= CLKINPUT_PD)
        return 0;
    if (clkin == CLKIN_CRYSTAL)
        return 12000000; /* by definition */

    uint32_t rcnt_xtal, fcnt_xtal;
    measure_frequency_raw(CLKIN_CRYSTAL, 250, &rcnt_xtal, &fcnt_xtal);
    if (!fcnt_xtal)
        return 0;

    uint32_t rcnt, fcnt;
    measure_frequency_raw(clkin, 250, &rcnt, &fcnt);
    if (!rcnt)
        return 0;

    return (uint32_t) (12000000.0 * fcnt / rcnt * rcnt_xtal / fcnt_xtal);
}

/* Process an EP0 IN control transfer described in `message`;
 * fill lpcsdr_usb_control_buffer with results.
 *
 * For fixed-size responses, it's okay to just fill the buffer with the full
 * response regardless of `length`; the caller will sort out the details.
 *
 * The caller guarantees that length <= sizeof lpc_usb_control_buffer
 *
 * Return true to return data to the host, false to return an endpoint stall
 */
static bool process_ep0_in(const ipc_message_t *message)
{
    ep0_in_request_t request = (ep0_in_request_t) message->values[0];
    uint32_t valueAndIndex = message->values[1];
    uint16_t value = valueAndIndex & 0xFFFF;
    uint16_t index = valueAndIndex >> 16;
    uint32_t length = message->values[2];

    uint8_t *buf = lpcsdr_usb_control_buffer;

    switch (request) {
    case EP0_IN_COMMS_CHECK: {
        ep0_in_comms_check_t *result = (ep0_in_comms_check_t *)buf;
        result->magic = COMMS_CHECK_MAGIC;
        return true;
    }

    case EP0_IN_FLASH_DEVICE_ID: {
        /* SPI: read manufacturer/device ID */
        ep0_in_flash_device_id_t *result = (ep0_in_flash_device_id_t *)buf;
        lpcsdr_spifi_read_manufacturer_device_id(&result->device_id);
        return true;
    }

    case EP0_IN_FLASH_UNIQUE_ID: {
        /* SPI: read unique ID */
        ep0_in_flash_unique_id_t *result = (ep0_in_flash_unique_id_t *)buf;
        lpcsdr_spifi_read_unique_id(&result->unique_id);
        return true;
    }

    case EP0_IN_FLASH_READ:
        /* SPI: read data */
        if (valueAndIndex > 0x00FFFFFF || valueAndIndex + length > 0x01000000)
            return false;

        lpcsdr_spifi_read_data(valueAndIndex, buf, length);
        return true;

    case EP0_IN_FLASH_READ_QUAD:
        /* SPI: read data, quad */
        if (valueAndIndex > 0x00FFFFFF || valueAndIndex + length > 0x01000000)
            return false;

        lpcsdr_spifi_fast_read_quad(valueAndIndex, buf, length);
        return true;

    case EP0_IN_BOARD_STATUS: {
        /* Fill in the state we know of directly */
        ep0_in_board_status_t *result = (ep0_in_board_status_t *)buf;
        if (fast_cpu)
            result->flags |= STATUS_FAST_CPU;
        if (lpcsdr_read_sw1())
            result->flags |= STATUS_SW1_USBBOOT;
        if (!lpcsdr_read_sw2())
            result->flags |= STATUS_SW2_PRESSED;
        if (rf_power)
            result->flags |= STATUS_RF_POWER_ON;

        result->usb_samples_per_block = HSADC_BUFFER_SIZE/2;
        result->usb_bytes_per_block = USB_BLOCK_SIZE;

        /* delegate for the rest */
        lpcsdr_hsadc_status(result);
        lpcsdr_dma_status(result);
        lpcsdr_usb_status(result);
        lpcsdr_tuner_status(result);

        /* measure base clock frequencies (takes about 20ms per clock, so we only do this if requested) */
        if (valueAndIndex != 0) {
            result->clock_32k = measure_frequency(CLKIN_32K);
            result->clock_irc = measure_frequency(CLKIN_IRC);
            result->clock_pll0usb = measure_frequency(CLKIN_USBPLL);
            result->clock_pll0audio = measure_frequency(CLKIN_AUDIOPLL);
            result->clock_pll1 = measure_frequency(CLKIN_MAINPLL);
            result->clock_idiv_a = measure_frequency(CLKIN_IDIVA);
            result->clock_idiv_b = measure_frequency(CLKIN_IDIVB);
            result->clock_idiv_c = measure_frequency(CLKIN_IDIVC);
            result->clock_idiv_d = measure_frequency(CLKIN_IDIVD);
            result->clock_idiv_e = measure_frequency(CLKIN_IDIVE);
        }

        return true;
    }

    case EP0_IN_MEMORY_READ:
        /* read arbitrary area of memory */
        memcpy(buf, (uint8_t*) valueAndIndex, length);
        return true;

    case EP0_IN_TUNER_READ: {
        /* read tuner regs; value = first reg to read; index = cache mode (0=use cache if possible, 1=bypass cache, 2=refresh cache) */
        if (value >= 32 || (value + length) > 32) {
            /* out of range */
            return false;
        }

        switch (index) {
        case 0: /* use cache */
            return lpcsdr_tuner_read_regs(value, buf, length);

        case 1: /* bypass cache */
            if (!lpcsdr_tuner_read_regs_direct(buf, value + length))
                return false;
            memmove(buf, buf + value, length);
            return true;

        case 2: /* refresh cache */
            if (!lpcsdr_tuner_shadow_from_chip())
                return false;
            return lpcsdr_tuner_read_regs(value, buf, length);

        default: /* bad mode */
            return false;
        }
    }

    default:
        return false;
    }

    /* not reached */
}

/* Handle a EP0_IN IPC message and send a suitable USB control transfer response.
 * General rules are:
 *  - If the requested length exceeds our internal buffer size, stall
 *  - If the requested length is smaller than the available data, return a truncated response
 *  - If the requested length is larger than the available data, return a response of the requested length with trailing zero padding
 */
static void m4_usb_ep0_in(const ipc_message_t *message)
{
    debug_printf("ep0 in (type=%02x, index=%02x, value=%02x, length=%u): ",
                 message->values[0], message->values[1] & 0xFFFF, message->values[1] >> 16, message->values[2]);

    uint32_t requested_length = message->values[2];
    if (requested_length > sizeof lpcsdr_usb_control_buffer) {
        lpcsdr_usb_ep0_stall();
        return;
    }

    memset(lpcsdr_usb_control_buffer, 0, sizeof lpcsdr_usb_control_buffer);
    if (!process_ep0_in(message)) {
        debug_printf("STALL\r\n");
        lpcsdr_usb_ep0_stall();
        return;
    }

    debug_printf("returning %u bytes\r\n", requested_length);
    lpcsdr_usb_ep0_data_in(lpcsdr_usb_control_buffer, requested_length);
}

static bool process_ep0_out(const ipc_message_t *message)
{
    ep0_out_request_t request = (ep0_out_request_t) message->values[0];
    uint32_t valueAndIndex = message->values[1];
    uint32_t length = message->values[2];
    const uint8_t *buf = lpcsdr_usb_control_buffer;

    switch (request) {
    case EP0_OUT_COMMS_CHECK: {
        if (length != sizeof(ep0_out_comms_check_t))
            return false;

        const ep0_out_comms_check_t *param = (const ep0_out_comms_check_t *) buf;
        if (param->magic != COMMS_CHECK_MAGIC)
            return false;

        return true;
    }

    case EP0_OUT_FLASH_WRITE:
        /* SPI: write data */
        if (valueAndIndex > 0x00FFFFFF || valueAndIndex + length > 0x01000000)
            return false;

        /* must be contained within a single page */
        if ((valueAndIndex >> 8) != ((valueAndIndex + length - 1) >> 8))
            return false;

        return (lpcsdr_spifi_page_program(valueAndIndex, buf, length) == LPC_OK);

    case EP0_OUT_FLASH_ERASE:
        /* SPI: erase sector */
        if (valueAndIndex > 0x00FFFFFF || (valueAndIndex & 0x0FFF) != 0)
            return false;

        return (lpcsdr_spifi_sector_erase(valueAndIndex) == LPC_OK);

    case EP0_OUT_START_TRANSFER: {
        /* Set up ADC clock, start transferring data */
        if (length != sizeof(ep0_out_start_transfer_t))
            return false;

        ep0_out_start_transfer_t *param = (ep0_out_start_transfer_t *) buf;
        if (!lpcsdr_hsadc_clock_start(param->n_divisor,
                                      param->m_divisor,
                                      param->p_divisor,
                                      param->idiv_divisor))
            return false;

        if (!lpcsdr_hsadc_conversion_start()) {
            lpcsdr_hsadc_clock_stop();
            return false;
        }

        set_fast_cpu();
        lpcsdr_dma_hsadc_start();
        lpcsdr_usb_ep1_enable();
        return true;
    }

    case EP0_OUT_STOP_TRANSFER:
        /* Stop ADC conversion & bulk transfer */
        lpcsdr_dma_hsadc_stop();
        lpcsdr_usb_ep1_disable();
        lpcsdr_hsadc_conversion_stop();
        lpcsdr_hsadc_clock_stop();
        set_slow_cpu();
        return true;

    case EP0_OUT_SET_RF_POWER:
        switch (valueAndIndex) {
        case 0: /* RF power off */
            set_rf_power_off();
            return true;

        case 1: /* RF power on */
            set_rf_power_on();
            return true;

        case 2: /* RF power toggle (tuner reset) */
            set_rf_power_off();
            StopWatch_DelayMs(5); /* wait a while to let VDD3_RF discharge */
            set_rf_power_on();
            return true;

        default: /* bad request */
            return false;
        }

    case EP0_OUT_TUNER_WRITE: {
        /* write tuner regs starting at valueAndIndex */
        return lpcsdr_tuner_write_regs(valueAndIndex, buf, length);
    }

    case EP0_OUT_TUNER_UPDATE: {
        /* selective tuner reg update
         * first half of data contains new bit values to set
         * second half of data indicates which bits to apply changes to
         */
        if ((length & 1) != 0) {
            /* must have an even number of bytes = whole number of regs to affect */
            return false;
        }

        return lpcsdr_tuner_update_regs(valueAndIndex, buf, buf + length/2, length/2);
    }

    case EP0_OUT_RESET: {
        /* ack, then delay a bit before the reset to give the host a chance to see the ack */
        debug_printf("Preparing to reset.. ");
        lpcsdr_usb_ep0_out_ack();
        StopWatch_DelayMs(250);
        debug_printf("resetting now.\r\n");
        lpcsdr_uart_flush();
        lpcsdr_reset();
        /* not reached */
    }

    case EP0_OUT_WATCHDOG_TEST: {
        debug_printf("sleeping for a long time to trigger WDT: ");
        for (unsigned i = 0; i < 100; ++i) {
            debug_printf("%u ", i);
            StopWatch_DelayMs(100);
        }
        debug_printf(" ... apparently I didn't reset?\r\n");
        return true;
    }

    case EP0_OUT_UART_TEST: {
        /* run UART tests */
        debug_printf("basic UART tests:\r\n");

        lpcsdr_uart_write(">123456<", 8);            /* write < FIFO size */
        lpcsdr_uart_flush();
        lpcsdr_uart_write(">123456789ABCDE<", 16);   /* write exactly FIFO size */
        lpcsdr_uart_flush();
        lpcsdr_uart_write(">123456789ABCDEFG<", 18);   /* write > FIFO size */
        lpcsdr_uart_flush();
        lpcsdr_uart_write(">123456789ABCDEF0123456789ABCDE<", 32);   /* write exactly 2x FIFO */
        lpcsdr_uart_flush();

        lpcsdr_uart_write(">1234567", 8); /* 8 + 8 bytes */
        lpcsdr_uart_write("89ABCDE<", 8);
        lpcsdr_uart_flush();

        lpcsdr_uart_write(">123456789ABCDEF", 16); /* 16 + 8 bytes */
        lpcsdr_uart_write("GHIJKLM<", 8);
        lpcsdr_uart_flush();

        lpcsdr_uart_write(">123456789ABCDEFGH", 18); /* 18 + 6 bytes */
        lpcsdr_uart_write("IJKLM<", 6);
        lpcsdr_uart_flush();

        debug_printf("\r\ndebug_printf tests:\r\n");
        debug_printf("int: %d hex: %02x string: >%s<\r\n", 42, 0xAB, "this is a string");

        const char *longstring = "this is a long string. a reasonably long string. actually quite a long string indeed.";
        debug_printf("this should get truncated: %s %s %s %s\r\n", longstring, longstring, longstring, longstring);
        debug_printf("and here's another message.\r\n");

        debug_printf("all done.\r\n");

        return true;
    }

    case EP0_OUT_CONFIG_ADC: {
        lpcsdr_hsadc_set_config(valueAndIndex & 1, valueAndIndex & 2, valueAndIndex & 4);
        return true;
    }

    default:
        return false;
    }

    /* not reached */
}

static void m4_usb_ep0_out(const ipc_message_t *message)
{
    debug_printf("ep0 out (type=%02x, index=%02x, value=%02x, length=%u): ",
                 message->values[0], message->values[1] & 0xFFFF, message->values[1] >> 16, message->values[2]);

    if (process_ep0_out(message)) {
        debug_printf("ACK\r\n");
        lpcsdr_usb_ep0_out_ack();
    } else {
        debug_printf("STALL\r\n");
        lpcsdr_usb_ep0_stall();
    }
}

static void m4_handle_message(const ipc_message_t *message)
{
    switch (message->message) {
    case M4_QUEUE_TEST_DATA:
        m4_queue_test_data();
        break;

    case M4_COPY_HSADC_BUFFER:
        m4_copy_hsadc_buffer(message);
        break;

    case M4_USB_EP0_IN:
        m4_usb_ep0_in(message);
        break;

    case M4_USB_EP0_OUT:
        m4_usb_ep0_out(message);
        break;
    }
}

int main(void) {
    setup_clocks();

    lpcsdr_uart_init();
    lpcsdr_diagnose_reset();
    lpcsdr_gpio_init();
    lpcsdr_spifi_init();
    lpcsdr_dma_init();
    lpcsdr_hsadc_init();
    lpcsdr_tuner_init();
    lpcsdr_ipc_init();
    lpcsdr_usb_init();

    debug_printf("M4 entering main loop\r\n");
    lpcsdr_ipc_handle_messages_forever(m4_handle_message);

    // not reached
}
