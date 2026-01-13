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
#include "pg2sdr_usb.h"
#include "pg2sdr_dma.h"
#include "pg2sdr_hsadc.h"
#include "pg2sdr_spifi.h"
#include "pg2sdr_gpio.h"
#include "pg2sdr_ipc.h"
#include "pg2sdr_tuner.h"
#include "pg2sdr_protocol.h"
#include "pg2sdr_uart.h"
#include "pg2sdr_panic.h"
#include "pg2sdr_m4clock.h"
#include "pg2sdr_hardware.h"
#include "morse.h"
#include <string.h>

pg2sdr_interrupts_t pg2sdr_interrupts;

static bool bulk_test_mode = false;
static bool rf_power = false;
static uint64_t serial_number = 0;

#define USB_BLOCK_SIZE ALIGN_TO(sizeof(ep1_header_t) + (HSADC_BUFFER_SIZE * 3 / 4), 512)

/* callback from USB code to indicate it's got a free buffer available */
void pg2sdr_usb_space_available(void)
{
    if (bulk_test_mode)
        pg2sdr_ipc_send_m4(M4_QUEUE_TEST_DATA, 0, 0, 0);
}

/* callback from USB code to indicate the USB connection state changed (USB reset or reconfiguration) */
void pg2sdr_usb_state_changed(void)
{
    /* no-op for now */
}

/* callback from DMA code to indicate there's a new HSADC buffer waiting to be copied */
bool pg2sdr_dma_hsadc_buffer_ready(dma_lli_t *buffer, uint32_t status)
{
    return pg2sdr_ipc_send_m4(M4_COPY_HSADC_BUFFER, (uint32_t) buffer, status, 0);
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
        pg2sdr_dma_hsadc_copy_complete(buffer, false);
        return;
    }

    USB_DTD_T *dTD = pg2sdr_usb_get_dtd();
    if (!dTD) {
        /* No available USB buffer, host is not keeping up, drop data */
        pending_usb_status |= BLOCK_STATUS_USB_OVERRUN;
        pg2sdr_dma_hsadc_copy_complete(buffer, false);
        return;
    }

    static_assert(USB_BLOCK_SIZE <= DTD_BUFFER_SIZE);

    /* Fill in USB block header */
    ep1_header_t *header = (ep1_header_t*) dTD->buffer;
    header->magic = BLOCK_MAGIC;
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
    uint32_t status = pg2sdr_dma_hsadc_copy_complete(buffer, true);
    if (buffer->sequence == start_seq && !(status & LLI_STATUS_CLOBBERED)) {
        /* we copied everything out successfully with no clobber, send the data */
        pg2sdr_usb_queue_dtd(dTD, USB_BLOCK_SIZE);
        pending_usb_status = 0;
    } else {
        /* clobbered during the copy, drop data and return the dTD to the pool */
        pending_usb_status |= BLOCK_STATUS_PACKING_OVERRUN;
        pg2sdr_usb_free_dtd(dTD);
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
        USB_DTD_T *dTD = pg2sdr_usb_get_dtd();
        if (!dTD)
            break;
        random_fill_word(dTD->buffer, DTD_BUFFER_SIZE);
        pg2sdr_usb_queue_dtd(dTD, DTD_BUFFER_SIZE);
    }
}

static void update_cpu_speed(void)
{
    uint32_t hsadc_frequency = pg2sdr_hsadc_get_sampling_rate();
    pg2sdr_m4clock_set_freq(hsadc_frequency * 5, false);
}

static void set_rf_power_off(void)
{
    if (!rf_power)
        return;

    rf_power = false;
    pg2sdr_tuner_handle_poweroff();
    pg2sdr_set_rfen(false);
    pg2sdr_led_set(0, C_OFF);
}

static void set_rf_power_on(void)
{
    if (rf_power)
        return;

    rf_power = true;
    pg2sdr_set_rfen(true);
    pg2sdr_tuner_handle_poweron();
    pg2sdr_led_set(0, C_ON);
}

/* Measure the frequency of a clock input using the CGU's FREQ_MON registry.
 * This produces a frequency estimate relative to the internal IRC 12MHz clock.
 *
 * clkin: the CLKIN_* constant for the clock input to measure (should be a real clock input, not CLKINPUT_PD)
 * *rcnt_sum: on return, total number of clock cycles of the IRC clock seen over the measurement period
 * *fcnt_sum: on return, total number of clock cycles of the measured input seen over the measurement period
 *
 * The measured clock frequency is approximately (12e6 * (*fcnt_sum) / (*rcnt_sum))
 */
static void measure_frequency_vs_irc(CHIP_CGU_CLKIN_T clkin, uint32_t *rcnt_sum, uint32_t *fcnt_sum)
{
    uint32_t r_sum = 0, f_sum = 0;
    uint32_t rcnt_initial = 0x1FF;
    uint32_t timeout_ticks = StopWatch_MsToTicks(5); /* Measure over 5ms */
    uint32_t start_ticks = StopWatch_Start();

    while (StopWatch_Elapsed(start_ticks) < timeout_ticks) {
        LPC_CGU->FREQ_MON =
                rcnt_initial | /* RCNT */
                ((clkin & 0x1F) << 24); /* CLK_SEL */
        StopWatch_DelayUs(5);
        LPC_CGU->FREQ_MON |= _BIT(23); /* set MEAS */

        uint32_t start_ticks = StopWatch_Start();
        uint32_t stat;
        while ( ((stat = LPC_CGU->FREQ_MON) & _BIT(23)) && StopWatch_Elapsed(start_ticks) < timeout_ticks )
            __NOP();
        if (stat & _BIT(23))
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

static uint32_t irc_xtal_rcnt, irc_xtal_fcnt;

/* Update the IRC calibration used by measure_frequency
 * by measuring the presumed-to-be-accurate 12MHz crystal input
 * against the IRC clock.
 */
static void calibrate_irc()
{
    measure_frequency_vs_irc(CLKIN_CRYSTAL, &irc_xtal_rcnt, &irc_xtal_fcnt);
}

/* Measure the frequency of a given clock input, using the 12MHz crystal oscillator
 * as a reference.
 *
 * The internal reference is actually the IRC clock, and we then adjust for the
 * frequency of that versus the crystal (assuming that the crystal is more accurate
 * than the IRC clock)
 *
 * Returns a frequency in Hz, or 0 if something went wrong
 */
static uint32_t measure_frequency(CHIP_CGU_CLKIN_T clkin)
{
    if (clkin >= CLKINPUT_PD)
        return 0;
    if (clkin == CLKIN_CRYSTAL)
        return 12000000; /* by definition */

    uint32_t rcnt, fcnt;
    measure_frequency_vs_irc(clkin, &rcnt, &fcnt);
    if (!rcnt)
        return 0;

    if (irc_xtal_fcnt)
        return (uint32_t) (12000000.0 * fcnt / rcnt * irc_xtal_rcnt / irc_xtal_fcnt);
    else
        return (uint32_t) (12000000.0 * fcnt / rcnt);
}

/* Process an EP0 IN control transfer described in `message`;
 * fill pg2sdr_usb_control_buffer with results.
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

    uint8_t *buf = pg2sdr_usb_control_buffer;

    switch (request) {
    case EP0_IN_COMMS_CHECK: {
        ep0_in_comms_check_t *result = (ep0_in_comms_check_t *)buf;
        result->magic = COMMS_CHECK_MAGIC;
        return true;
    }

    case EP0_IN_FLASH_DEVICE_ID: {
        /* SPI: read manufacturer/device ID */
        debug_printf("< FLASH_DEVICE_ID\r\n");
        ep0_in_flash_device_id_t *result = (ep0_in_flash_device_id_t *)buf;
        pg2sdr_spifi_read_manufacturer_device_id(&result->device_id);
        return true;
    }

    case EP0_IN_FLASH_UNIQUE_ID: {
        /* SPI: read unique ID */
        debug_printf("< FLASH_UNIQUE_ID\r\n");
        ep0_in_flash_unique_id_t *result = (ep0_in_flash_unique_id_t *)buf;
        pg2sdr_spifi_read_unique_id(&result->unique_id);
        return true;
    }

    case EP0_IN_FLASH_READ:
        /* SPI: read data */
        debug_printf("< FLASH_READ(0x%06x, <%u bytes>)\r\n", valueAndIndex, length);
        if (valueAndIndex > 0x00FFFFFF || valueAndIndex + length > 0x01000000)
            return false;

        pg2sdr_spifi_read_data(valueAndIndex, buf, length);
        return true;

    case EP0_IN_FLASH_READ_QUAD:
        /* SPI: read data, quad */
        debug_printf("< FLASH_READ_QUAD(0x%06x, <%u bytes>)\r\n", valueAndIndex, length);
        if (valueAndIndex > 0x00FFFFFF || valueAndIndex + length > 0x01000000)
            return false;

        pg2sdr_spifi_fast_read_quad(valueAndIndex, buf, length);
        return true;

    case EP0_IN_BOARD_STATUS: {
        ep0_in_board_status_t *result = (ep0_in_board_status_t *)buf;

        static_assert(sizeof(ep0_in_board_status_t) <= sizeof(pg2sdr_usb_control_buffer));

        /* Fill in the state we know of directly */
        if (pg2sdr_read_sw1())
            result->flags |= STATUS_SW1_USBBOOT;
        if (!pg2sdr_read_sw2())
            result->flags |= STATUS_SW2_PRESSED;
        if (rf_power)
            result->flags |= STATUS_RF_POWER_ON;
#ifdef HW_IS_PG2SDR
        result->flags |= STATUS_IS_PG2SDR;
#endif
#ifdef HW_IS_AIRSPY
        result->flags |= STATUS_IS_AIRSPY;
#endif

        result->usb_samples_per_block = HSADC_BUFFER_SIZE/2;
        result->usb_bytes_per_block = USB_BLOCK_SIZE;

        /* delegate for the rest */
        pg2sdr_hsadc_status(result);
        pg2sdr_dma_status(result);
        pg2sdr_usb_status(result);
        pg2sdr_tuner_status(result);
        pg2sdr_m4clock_status(result);

        /* measure base clock frequencies (takes about 5ms per clock, so we only do this if requested) */
        if (valueAndIndex != 0) {
            calibrate_irc();
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

        result->serial_number = serial_number;

        result->intr_systick = pg2sdr_interrupts.systick;
        result->intr_dma = pg2sdr_interrupts.dma;
        result->intr_usart0 = pg2sdr_interrupts.usart0;
        result->intr_usb0 = pg2sdr_interrupts.usb0;
        result->intr_wwdt = pg2sdr_interrupts.wwdt;
        result->intr_m0app = pg2sdr_interrupts.m0app;
        result->intr_m4 = pg2sdr_interrupts.m4;

        result->reset_reason = pg2sdr_reset_reason;
        result->reset_code = pg2sdr_reset_code;

        return true;
    }

    case EP0_IN_MEMORY_READ:
        /* read arbitrary area of memory */
        debug_printf("< MEMORY_READ(0x%08x, <%u bytes>)\r\n", valueAndIndex, length);
        memcpy(buf, (uint8_t*) valueAndIndex, length);
        return true;

    case EP0_IN_TUNER_READ: {
        debug_printf("< TUNER_READ(%u..%u, mode=%u)\r\n", value, value + length - 1, index);

        /* read tuner regs; value = first reg to read; index = cache mode (0=use cache if possible, 1=bypass cache, 2=refresh cache) */
        if (value >= 32 || (value + length) > 32) {
            /* out of range */
            return false;
        }

        switch ((tuner_cache_mode_t) index) {
        case CACHE_NORMAL: /* use cache */
            return pg2sdr_tuner_read_regs(value, buf, length);

        case CACHE_BYPASS: /* bypass cache */
            if (!pg2sdr_tuner_read_regs_direct(buf, value + length))
                return false;
            memmove(buf, buf + value, length);
            return true;

        case CACHE_REFRESH: /* refresh cache */
            if (!pg2sdr_tuner_shadow_from_chip())
                return false;
            return pg2sdr_tuner_read_regs(value, buf, length);

        default: /* bad mode */
            return false;
        }
    }

    case EP0_IN_TUNER_LOCK: {
        debug_printf("< TUNER_LOCK(vco=%u,timeout=%u)\r\n", value, index);

        if (value > 7) {
            /* bad vco_current */
            return false;
        }

        if (index > 500) {
            /* timeout too large */
            return false;
        }

        int lock = pg2sdr_tuner_lock(value, index);
        if (lock < 0) {
            /* I2C communication error */
            return false;
        }

        ep0_in_tuner_lock_t *result = (ep0_in_tuner_lock_t *)buf;
        result->pll_locked = lock ? 1 : 0;
        return true;
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
    uint32_t requested_length = message->values[2];
    if (requested_length > sizeof pg2sdr_usb_control_buffer) {
        pg2sdr_usb_ep0_stall();
        return;
    }

    memset(pg2sdr_usb_control_buffer, 0, sizeof pg2sdr_usb_control_buffer);
    if (!process_ep0_in(message)) {
        debug_printf("EP0 in: control transfer stall, type=%02x index=%02x value=%02x length=%u\r\n",
                    message->values[0], message->values[1] & 0xFFFF, message->values[1] >> 16, message->values[2]);
        pg2sdr_usb_ep0_stall();
        return;
    }

    pg2sdr_usb_ep0_data_in(pg2sdr_usb_control_buffer, requested_length);
}

/* Image upload area for EP0_OUT_LOAD_IMAGE. This overlaps with the DMA buffers,
 * so DMA must stay halted while the image load happens. load_image_size track this
 * (it tracks the number of uploaded bytes since we last halted DMA)
 */

static uint8_t * const load_image_buffer = (uint8_t*) AHB_SRAM_BANK_0;
static uint8_t * const load_image_buffer_end = (uint8_t*) (AHB_SRAM_BANK_0 + 0xFFFC);
static uint32_t load_image_size = 0;

static bool try_boot_image()
{
    if (load_image_size < 16 + 32) {
        debug_printf("try_boot_image: %u bytes is too small\r\n", load_image_size);
        return false; /* not even enough space for the header and initial vectors */
    }

    /* validate image header */
    uint32_t *header = (uint32_t *) load_image_buffer;
    if ((header[0] & 0x3FFF) != 0x3F1A) {
        debug_printf("try_boot_image: bad header[0]\r\n");
        return false;
    }
    if (header[1] != 0 || header[2] != 0) {
        debug_printf("try_boot_image: bad header[1..2]\r\n");
        return false;
    }
    if (header[3] != 0xFFFFFFFF) {
        debug_printf("try_boot_image: bad header[3]\r\n");
        return false;
    }

    /* validate vector table */
    uint32_t *vecs = (uint32_t *)(load_image_buffer + 16);
    uint32_t sum =
        vecs[0] + vecs[1] + vecs[2] + vecs[3] +
        vecs[4] + vecs[5] + vecs[6] + vecs[7];
    if (sum != 0) {
        debug_printf("try_boot_image: bad vector table\r\n");
        return false;
    }

    uint32_t image_size = (header[0] >> 16) * 512 + 16;
    if (image_size > load_image_size) {
        debug_printf("try_boot_image: image_size=%u but load_image_size=%u\r\n", image_size, load_image_size);
        return false; /* image upload seems incomplete */
    }
    uint8_t *image_end = load_image_buffer + image_size;

    /* We use a relocator that implements something like this pseudo-C function:
     *
     * void relocate_and_boot(uint32_t reset_r0, uint32_t *dest, uint32_t *src, uint32_t len)
     * {
     *   SP = src[0];
     *   ResetISR = src[1];
     *   do {
     *     *dest++ = *src++;
     *     --len;
     *   } while (len != 0);
     *   LR = 0xFFFFFFFF;
     *   ResetISR(reset_r0);
     * }
     *
     * implemented (in relocator.s) directly in assembly to avoid any other side effects
     * and work OK even if the code itself is copied around.
     *
     * We copy that code to the end of the new image, and then call it in that new location
     * to relocate the new firmware image into place (overwriting the currently running old
     * firmware!) and jump to the new image's reset ISR. The copy of the relocation code is
     * needed so that we're definitely running from somewhere that's not going to be overwritten
     * by the new firmware mid-relocation.
     */

    extern uint8_t relocator_start; /* start of relocation function, in relocator.s */
    extern uint8_t relocator_end;   /* end of relocation function */
    const uint8_t *relocator_copy_start = &relocator_start;
    size_t relocator_copy_size = &relocator_end - &relocator_start;

    if (image_end + relocator_copy_size > load_image_buffer_end) {
        debug_printf("try_boot_image: not enough space for relocation code\r\n");
        return false;
    }
    memcpy(image_end, relocator_copy_start, relocator_copy_size);
    __asm__ volatile ("dsb" ::: "memory"); /* flush d-cache and i-cache just in case */
    __asm__ volatile ("isb" ::: "memory");

    /* Everything seems okay. Shut everything down, then pass control to the
     * copied relocation code which will copy the new firmware image into place and
     * start it.
     */
    debug_printf("Loading new firmware from RAM..\r\n");
    pg2sdr_uart_flush();
    pg2sdr_usb_ep0_out_ack();
    StopWatch_DelayMs(250); /* give the hardware some time to send the USB ack */

    /* turn off hardware, disconnect from USB bus */
    set_rf_power_off();
    pg2sdr_usb_disconnect();
    Chip_CREG_DisableUSB0Phy();

    /* disable all NVIC interrupt sources and SysTick */
    for (unsigned i = 0; i < 8; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFF;   /* NVIC ICER0-7, clear-enable all interrupts */
        NVIC->ICPR[i] = 0xFFFFFFFF;   /* NVIC ICPR0-7, clear-pending all interrupts */
    }
    SysTick->CTRL = 0;

    /* set CPU clock to use the IRC clock at 96MHz, disable external crystal */
    Chip_SetupCoreClock(CLKIN_IRC, 96000000, false);
    Chip_Clock_DisableCrystal();

    /* set VTOR/MEMMAP to the expected initial values */
    disable_interrupts();
    LPC_CREG->MXMEMMAP = 0x10000000;       /* default M4MEMMAP for booting an image from RAM */
    SCB->VTOR = 0x00000000;                /* default VTOR, remapped to the image vector table via M4MEMMAP */

    /* call relocation code, which will:
     *  a) copy the new image to 0x10000000 (which will overwrite the currently loaded firmware!)
     *    .. this is why we copy the relocation patch to the end of load_image_buffer,
     *    so that code can continue to execute even while the loaded firmware is being overwritten
     *  b) load SP from the first vector table entry (vStackTop)
     *  c) jump to the reset ISR address in the second vector table entry to start the new image
     */
    void (*relocate_and_start_image)(uint32_t,void*,void*,uint32_t) =
            (void (*)(uint32_t,void*,void*,uint32_t)) (image_end + 1); /* +1 to set Thumb bit in target interworking address */
    relocate_and_start_image(RESET_LOAD, (void*)0x10000000, load_image_buffer + 16, (image_size-16)/4);
    pg2sdr_hard_reset(); /* not reached */
}

static bool process_ep0_out(const ipc_message_t *message)
{
    ep0_out_request_t request = (ep0_out_request_t) message->values[0];
    uint32_t valueAndIndex = message->values[1];
    uint32_t length = message->values[2];
    const uint8_t *buf = pg2sdr_usb_control_buffer;

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
        debug_printf("> FLASH_WRITE(0x%06x,<%u bytes>)\r\n", valueAndIndex, length);
        /* SPI: write data */
        if (valueAndIndex > 0x00FFFFFF || valueAndIndex + length > 0x01000000)
            return false;

        /* must be contained within a single page */
        if ((valueAndIndex >> 8) != ((valueAndIndex + length - 1) >> 8))
            return false;

        return (pg2sdr_spifi_page_program(valueAndIndex, buf, length) == LPC_OK);

    case EP0_OUT_FLASH_ERASE:
        debug_printf("> FLASH_ERASE(0x%06x)\r\n", valueAndIndex);
        /* SPI: erase sector */
        if (valueAndIndex > 0x00FFFFFF || (valueAndIndex & 0x0FFF) != 0)
            return false;

        return (pg2sdr_spifi_sector_erase(valueAndIndex) == LPC_OK);

    case EP0_OUT_START_TRANSFER: {
        /* Set up ADC clock, start transferring data */
        if (length != sizeof(ep0_out_start_transfer_t))
            return false;

        load_image_size = 0; /* enabling DMA will trash any upload in progress */

        /* stop DMA and reset EP1 to ensure there's no stale data in the queues */
        pg2sdr_dma_hsadc_stop();
        pg2sdr_usb_ep1_disable();

        ep0_out_start_transfer_t *param = (ep0_out_start_transfer_t *) buf;
        debug_printf("> START_TRANSFER(N=%u,M=%u,P=%u,I=%u)\r\n",
                     param->n_divisor,
                     param->m_divisor,
                     param->p_divisor,
                     param->idiv_divisor);
        if (!pg2sdr_hsadc_clock_start(param->n_divisor,
                                      param->m_divisor,
                                      param->p_divisor,
                                      param->idiv_divisor))
            return false;

        if (!pg2sdr_hsadc_conversion_start()) {
            pg2sdr_hsadc_clock_stop();
            return false;
        }

        /* start everything */
        update_cpu_speed();
        pg2sdr_dma_hsadc_start();
        pg2sdr_usb_ep1_enable();
        return true;
    }

    case EP0_OUT_STOP_TRANSFER:
        /* Stop ADC conversion & bulk transfer */
        debug_printf("> STOP_TRANSFER\r\n");
        pg2sdr_dma_hsadc_stop();
        pg2sdr_usb_ep1_disable();
        pg2sdr_hsadc_conversion_stop();
        pg2sdr_hsadc_clock_stop();
        update_cpu_speed();
        return true;

    case EP0_OUT_SET_RF_POWER:
        debug_printf("> SET_RF_POWER(%u)\r\n", valueAndIndex);
        switch ((rf_power_mode_t) valueAndIndex) {
        case RF_POWER_OFF:
            set_rf_power_off();
            return true;

        case RF_POWER_ON:
            set_rf_power_on();
            return true;

        case RF_POWER_RESET: /* RF power toggle (tuner reset) */
            set_rf_power_off();
            StopWatch_DelayMs(5); /* wait a while to let VDD3_RF discharge */
            set_rf_power_on();
            return true;

        default: /* bad request */
            return false;
        }

    case EP0_OUT_TUNER_WRITE: {
        debug_printf("> TUNER_WRITE(%u..%u)\r\n", valueAndIndex, valueAndIndex+length-1);
        /* write tuner regs starting at valueAndIndex */
        return pg2sdr_tuner_write_regs(valueAndIndex, buf, length);
    }

    case EP0_OUT_TUNER_UPDATE: {
        debug_printf("> TUNER_UPDATE(%u..%u)\r\n", valueAndIndex, valueAndIndex+length/2-1);
        /* selective tuner reg update
         * first half of data contains new bit values to set
         * second half of data indicates which bits to apply changes to
         */
        if ((length & 1) != 0) {
            /* must have an even number of bytes = whole number of regs to affect */
            return false;
        }

        return pg2sdr_tuner_update_regs(valueAndIndex, buf, buf + length/2, length/2);
    }

    case EP0_OUT_LOAD_IMAGE: {
        //debug_printf("> LOAD_IMAGE(%08x,%u)\r\n", valueAndIndex, length);

        /* general protocol here is:
         *
         *   LOAD_IMAGE(0, <N bytes>)        first call will halt any in-progress DMA
         *   LOAD_IMAGE(1*N, <N bytes>)      subsequent calls must provide consecutive addresses
         *   LOAD_IMAGE(2*N, <N bytes>)
         *     ...
         *   LOAD_IMAGE(X*N, 0)              final call with 0 length triggers booting the new image
         *
         * The uploaded image should start with a valid LPC header.
         * N must fit into pgs2sdr_usb_control_buffer (currently 512 bytes)
         *
         * To cancel an in-progress image load, just don't do the final call, the load will be
         * abandoned when START_TRANSFER next gets sent.
         */
        if (valueAndIndex == 0) {
            /* start of upload. We are going to reuse the DMA buffer space, so
             * DMA must be stopped while this happens.
             */
            pg2sdr_dma_hsadc_stop();
            pg2sdr_usb_ep1_disable();
            pg2sdr_hsadc_conversion_stop();
            pg2sdr_hsadc_clock_stop();
            load_image_size = 0;
        }

        if (valueAndIndex != load_image_size) {
            /* mis-sequenced upload, or DMA was restarted */
            return false;
        }

        if (!length) {
            /* zero length means "end of image" */
            return try_boot_image();
        }

        uint8_t *dest = load_image_buffer + load_image_size;
        if (dest + length > load_image_buffer_end)
            return false;

        memcpy(dest, buf, length);
        load_image_size += length;
        return true;
    }

    case EP0_OUT_RESET: {
        /* ack, then delay a bit before the reset to give the host a chance to see the ack */
        debug_printf("> RESET\r\n");
        pg2sdr_usb_ep0_out_ack();
        StopWatch_DelayMs(250);
        debug_printf("Resetting now.\r\n");
        pg2sdr_uart_flush();
        pg2sdr_reset();
        /* not reached */
    }

    case EP0_OUT_WATCHDOG_TEST: {
        /* ack first, to give the host a chance to see the ack before we reset */
        debug_printf("> WATCHDOG_TEST\r\n");
        pg2sdr_usb_ep0_out_ack();
        for (unsigned i = 0; i < 100; ++i) {
            debug_printf("%u ", i);
            StopWatch_DelayMs(100);
        }
        debug_printf(" ... apparently I didn't reset?\r\n");
        return true;
    }

    case EP0_OUT_UART_TEST: {
        /* run UART tests */
        debug_printf("> UART_TEST\r\n");

        pg2sdr_uart_write(">123456<", 8);            /* write < FIFO size */
        pg2sdr_uart_flush();
        pg2sdr_uart_write(">123456789ABCDE<", 16);   /* write exactly FIFO size */
        pg2sdr_uart_flush();
        pg2sdr_uart_write(">123456789ABCDEFG<", 18);   /* write > FIFO size */
        pg2sdr_uart_flush();
        pg2sdr_uart_write(">123456789ABCDEF0123456789ABCDE<", 32);   /* write exactly 2x FIFO */
        pg2sdr_uart_flush();

        pg2sdr_uart_write(">1234567", 8); /* 8 + 8 bytes */
        pg2sdr_uart_write("89ABCDE<", 8);
        pg2sdr_uart_flush();

        pg2sdr_uart_write(">123456789ABCDEF", 16); /* 16 + 8 bytes */
        pg2sdr_uart_write("GHIJKLM<", 8);
        pg2sdr_uart_flush();

        pg2sdr_uart_write(">123456789ABCDEFGH", 18); /* 18 + 6 bytes */
        pg2sdr_uart_write("IJKLM<", 6);
        pg2sdr_uart_flush();

        debug_printf("\r\ndebug_printf tests:\r\n");
        debug_printf("int: %d hex: %02x string: >%s<\r\n", 42, 0xAB, "this is a string");

        const char *longstring = "this is a long string. a reasonably long string. actually quite a long string indeed.";
        debug_printf("this should get truncated: %s %s %s %s\r\n", longstring, longstring, longstring, longstring);
        debug_printf("and here's another message.\r\n");

        debug_printf("all done.\r\n");

        return true;
    }

    case EP0_OUT_CONFIG_ADC: {
        debug_printf("> CONFIG_ADC(%u)\r\n", valueAndIndex);
        pg2sdr_hsadc_set_config(valueAndIndex & 1, valueAndIndex & 2, valueAndIndex & 4);
        return true;
    }

    default:
        return false;
    }

    /* not reached */
}

static void m4_usb_ep0_out(const ipc_message_t *message)
{
    if (process_ep0_out(message)) {
        pg2sdr_usb_ep0_out_ack();
    } else {
        debug_printf("EP0 out: control transfer stall, type=%02x index=%02x value=%02x length=%u\r\n",
                     message->values[0], message->values[1] & 0xFFFF, message->values[1] >> 16, message->values[2]);
        pg2sdr_usb_ep0_stall();
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

static void disable_unused_clocks(void)
{
    /* power down unused dividers */
    Chip_Clock_SetDivider(CLK_IDIV_A, CLKINPUT_PD, 1);
    Chip_Clock_SetDivider(CLK_IDIV_B, CLKINPUT_PD, 1);
    Chip_Clock_SetDivider(CLK_IDIV_C, CLKINPUT_PD, 1);
    Chip_Clock_SetDivider(CLK_IDIV_D, CLKINPUT_PD, 1);
    /* E will be used by ADCHS */

    /* disable base clocks we don't use */

    /* BASE_SAFE_CLK - used by watchdog */
    /* BASE_USB0_CLK - used by USB0 */
    Chip_Clock_DisableBaseClock(CLK_BASE_PERIPH);  /* SGPIO and M0 core, unused */
    Chip_Clock_DisableBaseClock(CLK_BASE_USB1);    /* USB1, unused */
    /* BASE_M4_CLK - used by M4, but disable branches we don't need: */
    /* CLK_MX_BUS needed */
    /* CLK_MX_SPIFI needed */
    /* CLK_MX_GPIO needed */
    Chip_Clock_Disable(CLK_MX_LCD);
    Chip_Clock_Disable(CLK_MX_ETHERNET);
    /* CLK_MX_USB0 needed */
    Chip_Clock_Disable(CLK_MX_EMC);
    Chip_Clock_Disable(CLK_MX_SDIO);
    /* CLK_MX_DMA needed */
    Chip_Clock_Disable(CLK_MX_SCT);
    Chip_Clock_Disable(CLK_MX_USB1);
    Chip_Clock_Disable(CLK_MX_EMC_DIV);
    Chip_Clock_Disable(CLK_MX_FLASHA);
    Chip_Clock_Disable(CLK_MX_FLASHB);
    Chip_Clock_Disable(CLK_M4_M0APP);
    /* CLK_MX_ADCHS needed */
    Chip_Clock_Disable(CLK_MX_EEPROM);
    /* CLK_MX_WWDT needed */
#ifndef HW_HAS_UART
    Chip_Clock_Disable(CLK_MX_UART0);
#endif
    Chip_Clock_Disable(CLK_MX_UART1);
    Chip_Clock_Disable(CLK_MX_SSP0);
    /* CLK_MX_TIMER0 needed (for StopWatch_*) */
    Chip_Clock_Disable(CLK_MX_TIMER1);
    Chip_Clock_Disable(CLK_MX_RITIMER);
    Chip_Clock_Disable(CLK_MX_UART2);
    Chip_Clock_Disable(CLK_MX_UART3);
    Chip_Clock_Disable(CLK_MX_TIMER2);
    Chip_Clock_Disable(CLK_MX_TIMER3);
    Chip_Clock_Disable(CLK_MX_SSP1);
    Chip_Clock_Disable(CLK_MX_QEI);

    /* BASE_SPIFI_CLK - used by SPIFI */
    Chip_Clock_DisableBaseClock(CLK_BASE_SPI);     /* SPI (not SPIFI), unused */
    Chip_Clock_DisableBaseClock(CLK_BASE_PHY_RX);  /* Ethernet PHY RX, unused */
    Chip_Clock_DisableBaseClock(CLK_BASE_PHY_TX);  /* Ethernet PHY TX, unused */
#ifdef HW_USES_I2C0
    /* BASE_APB1_CLK - I2C0 needed, disable other peripherals */
    Chip_Clock_Disable(CLK_APB1_CAN1);
    Chip_Clock_Disable(CLK_APB1_I2S);
    Chip_Clock_Disable(CLK_APB1_MOTOCON);
#else
    Chip_Clock_DisableBaseClock(CLK_BASE_APB1);    /* I2C0 and other APB1 peripherals unused */
#endif

#ifdef HW_USES_I2C1
    /* BASE_APB3_CLK - I2C1 needed, disable other peripherals */
    Chip_Clock_Disable(CLK_APB3_ADC0);
    Chip_Clock_Disable(CLK_APB3_ADC1);
    Chip_Clock_Disable(CLK_APB3_CAN0);
    Chip_Clock_Disable(CLK_APB3_DAC);
#else
    Chip_Clock_DisableBaseClock(CLK_BASE_APB3);    /* I2C1 and other APB3 peripherials unused */
#endif
    Chip_Clock_DisableBaseClock(CLK_BASE_LCD);     /* LCD, unused */
    /* BASE_ADCHS_CLK - ADCHS */
    Chip_Clock_DisableBaseClock(CLK_BASE_SDIO);    /* SDIO, unused */
    Chip_Clock_DisableBaseClock(CLK_BASE_SSP0);    /* SSP0, unused */
    Chip_Clock_DisableBaseClock(CLK_BASE_SSP1);    /* SSP1, unused */
#ifdef HW_HAS_UART
    /* BASE_UART0_CLK - UART */
#else
    Chip_Clock_DisableBaseClock(CLK_BASE_UART0);   /* UART0, unused */
#endif
    Chip_Clock_DisableBaseClock(CLK_BASE_UART1);   /* UART1, unused */
    Chip_Clock_DisableBaseClock(CLK_BASE_UART2);   /* UART2, unused */
    Chip_Clock_DisableBaseClock(CLK_BASE_UART3);   /* UART3, unused */
#ifdef HW_HAS_CLKOUT
    /* BASE_OUT_CLK, used for debug output on CLK0-3 */
#else
    Chip_Clock_DisableBaseClock(CLK_BASE_OUT);
#endif
    Chip_Clock_DisableBaseClock(CLK_BASE_CGU_OUT0); /* OUT0, unused */
    Chip_Clock_DisableBaseClock(CLK_BASE_CGU_OUT1); /* OUT1, unused */
}

/* startup sanity check that the 12MHz crystal is working
 * if it is not working, then USB won't work, and switching the CPU
 * to use the crystal will also fail.
 */
static void check_external_crystal()
{
    uint32_t rcnt, fcnt;
    measure_frequency_vs_irc(CLKIN_CRYSTAL, &rcnt, &fcnt);
    double xtal = (rcnt == 0 ? 0 : 12e6 * fcnt / rcnt);
    debug_printf("XTAL %.2fMHz\r\n", xtal/1e6);

    /* USB wants no more than 2500ppm (0.25%), but we are measuring
     * using the IRC clock which is only trimmed to within 1-2%, so
     * accept larger errors here - maybe something will still work.
     */
    if (xtal < 11.5e6 || xtal > 12.5e6) /* crystal is no good if not within ~5% */
        pg2sdr_panic(MORSE_X);          /* blink LEDS (X for XTAL) and reset */
}

int main(void) {
    /* get a minimal system up before touching the CPU clock */
    StopWatch_Init();
    pg2sdr_gpio_init();
    pg2sdr_uart_init();
    pg2sdr_diagnose_reset();
    check_external_crystal();

    /* now we trust the crystal enough to switch the CPU to use it */
    pg2sdr_m4clock_init();
    pg2sdr_spifi_init();
    serial_number = pg2sdr_spifi_read_unique_id();
    pg2sdr_dma_init();
    pg2sdr_hsadc_init();
    pg2sdr_tuner_init();
    pg2sdr_ipc_init();
    pg2sdr_usb_init(serial_number);

    disable_unused_clocks();

    debug_printf("M4 entering main loop\r\n");
    pg2sdr_ipc_handle_messages_forever(m4_handle_message);

    // not reached
}
