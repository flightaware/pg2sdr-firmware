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
#include "lpcsdr_protocol.h"
#include <string.h>

static bool bulk_test_mode = false;
static bool high_power_mode = false;

/* callback from USB code to indicate it's got a free buffer available */
void lpcsdr_usb_space_available(void)
{
    if (bulk_test_mode)
        lpcsdr_ipc_send_m4(M4_QUEUE_TEST_DATA, 0, 0, 0);
}

/* callback from USB code to indicate the USB connection state changed (USB reset or reconfiguration) */
void lpcsdr_usb_state_changed(void)
{
    lpcsdr_ipc_send_m4(M4_UPDATE_POWER_STATE, 0, 0, 0);
}

/* callback from DMA code to indicate there's a new HSADC buffer waiting to be copied */
bool lpcsdr_dma_hsadc_buffer_ready(dma_lli_t *buffer, uint32_t status)
{
    return lpcsdr_ipc_send_m4(M4_COPY_HSADC_BUFFER, (uint32_t) buffer, status, 0);
}

/* Given `count` words of HSADC samples in `src`, with 8 12-bit samples per 4 words,
 * copy and pack the samples into `dst` with 6 12-bit samples per 3 words.
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
        lpcsdr_dma_hsadc_copy_complete(buffer);
        return;
    }

    USB_DTD_T *dTD = lpcsdr_usb_get_dtd();
    if (!dTD) {
        /* No available USB buffer, host is not keeping up, drop data */
        pending_usb_status |= BLOCK_STATUS_USB_OVERRUN;
        lpcsdr_dma_hsadc_copy_complete(buffer);
        return;
    }

    /* Fill in USB block header */
    usb_header_t *header = (usb_header_t*) dTD->buffer;
    header->magic = 0xDEADBEEF;
    header->samples = HSADC_BUFFER_SIZE / 2;
    header->sequence = start_seq;
    header->status = pending_usb_status;

    /* Pack samples following the header */
    uint32_t *out_samples = (uint32_t*) (header + 1);

    const uint32_t in_words = HSADC_BUFFER_SIZE/4;
    static_assert(in_words % 4 == 0);
    pack_samples((uint32_t *)buffer->destaddr, (uint32_t *)out_samples, in_words);

    /* Zero out trailing data up to the 512-byte boundary */
    const uint32_t out_words = in_words * 3 / 4;
    const uint32_t used = sizeof(*header) + out_words * 4;
    const uint32_t pad = ((used + 511) & ~511) - used;
    if (pad > 0) {
        memset(out_samples + out_words, 0, pad);
    }

    const uint32_t total_block_len = sizeof(*header) + out_words * 4 + pad;
    static_assert(total_block_len <= DTD_BUFFER_SIZE);
    header->block_len = total_block_len;

    /* We're done copying to the USB buffer. Check that the source buffer is
     * still valid - it may have started to get clobbered while we were halfway
     * through the copy. If it was clobbered, we don't know that we got a good copy,
     * so discard the buffer.
     */
    memory_barrier();
    uint32_t status = lpcsdr_dma_hsadc_copy_complete(buffer);
    if (buffer->sequence == start_seq && !(status & LLI_STATUS_CLOBBERED)) {
        /* we copied everything out successfully with no clobber, send the data */
        lpcsdr_usb_queue_dtd(dTD, used + pad);
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

static void set_low_power_mode(void)
{
    high_power_mode = false;
    Chip_SetupCoreClock(CLKIN_CRYSTAL, 48000000, false);
    SystemCoreClockUpdate();
    StopWatch_Init();
}

static void set_high_power_mode(void)
{
    high_power_mode = true;
    Chip_SetupCoreClock(CLKIN_CRYSTAL, MAX_CLOCK_FREQ, false);
    SystemCoreClockUpdate();
    StopWatch_Init();
}

static void m4_update_power_state()
{
    if (!high_power_mode && lpcsdr_usb_is_ready()) {
        set_high_power_mode();
    } else if (high_power_mode && !lpcsdr_usb_is_ready()) {
        set_low_power_mode();
    }
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
static uint32_t measure_frequency(CHIP_CGU_CLKIN_T clkin, uint32_t loops)
{
    if (clkin >= CLKINPUT_PD)
        return 0;
    if (clkin == CLKIN_CRYSTAL)
        return 12000000; /* by definition */

    uint32_t rcnt_xtal, fcnt_xtal;
    measure_frequency_raw(CLKIN_CRYSTAL, loops, &rcnt_xtal, &fcnt_xtal);
    if (!fcnt_xtal)
        return 0;

    uint32_t rcnt, fcnt;
    measure_frequency_raw(clkin, loops, &rcnt, &fcnt);
    if (!rcnt)
        return 0;

    return (uint64_t)12000000 * fcnt / rcnt * rcnt_xtal / fcnt_xtal;
}

static void m4_usb_ep0_in(const ipc_message_t *message)
{
    uint32_t request = message->values[0];
    uint32_t valueAndIndex = message->values[1];
    uint32_t length = message->values[2];

    uint8_t *buf = lpcsdr_usb_control_buffer;
    uint32_t *buf32 = (uint32_t *) buf;

    switch (request) {
    case 0x01:
        /* comms check */
        buf[0] = 0xDE;
        buf[1] = 0xAD;
        buf[2] = 0xBE;
        buf[3] = 0xEF;
        lpcsdr_usb_ep0_data_in(buf, 4);
        return;

    case 0x02:
        /* SPI: read manufacturer/device ID */
        lpcsdr_spifi_read_manufacturer_device_id(buf);
        lpcsdr_usb_ep0_data_in(buf, 2);
        return;

    case 0x03:
        /* SPI: read unique ID */
        lpcsdr_spifi_read_unique_id(buf);
        lpcsdr_usb_ep0_data_in(buf, 8);
        return;

    case 0x04:
        /* SPI: read data */
        if (length > sizeof(lpcsdr_usb_control_buffer) || valueAndIndex > 0x00FFFFFF || valueAndIndex + length > 0x01000000) {
            lpcsdr_usb_ep0_stall();
            return;
        }

        lpcsdr_spifi_read_data(valueAndIndex, buf, length);
        lpcsdr_usb_ep0_data_in(buf, length);
        return;

    case 0x05:
        /* SPI: read data, quad */
        if (length > sizeof(lpcsdr_usb_control_buffer) || valueAndIndex > 0x00FFFFFF || valueAndIndex + length > 0x01000000) {
            lpcsdr_usb_ep0_stall();
            return;
        }

        lpcsdr_spifi_fast_read_quad(valueAndIndex, buf, length);
        lpcsdr_usb_ep0_data_in(buf, length);
        return;

    case 0x06:
        /* Read switch states */
        buf32[0] = (lpcsdr_read_sw1() ? 1 : 0) | (lpcsdr_read_sw2() ? 2 : 0);
        lpcsdr_usb_ep0_data_in(buf, 4);
        return;

    case 0x07:
        /* Measure clock input frequency */
        buf32[0] = measure_frequency((CHIP_CGU_CLKIN_T) valueAndIndex, 120000);
        lpcsdr_usb_ep0_data_in(buf, 4);
        return;

    case 0x08:
        /* Measure base clock frequency */
        buf32[0] = measure_frequency(Chip_Clock_GetBaseClock((CHIP_CGU_CLKIN_T) valueAndIndex), 120000);
        lpcsdr_usb_ep0_data_in(buf, 4);
        return;

    case 0x09:
        /* read PLL0AUDIO regs */
        buf32[0] = LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_STAT;
        buf32[1] = LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_CTRL;
        buf32[2] = LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_MDIV;
        buf32[3] = LPC_CGU->PLL[CGU_AUDIO_PLL].PLL_NP_DIV;
        buf32[4] = LPC_CGU->PLL0AUDIO_FRAC;
        buf32[5] = LPC_CGU->IDIV_CTRL[CLK_IDIV_E];
        lpcsdr_usb_ep0_data_in(buf, 6*4);
        return;

    case 0x0A:
        /* read random status stuff */

        /* ADCHS */
        buf32[0] = LPC_ADCHS->CONFIG;
        buf32[1] = LPC_ADCHS->INTS[0].STATUS;
        buf32[2] = LPC_ADCHS->FIFO_STS;
        buf32[3] = LPC_ADCHS->DSCR_STS;

        /* DMA */
        buf32[4] = LPC_GPDMA->CONFIG;
        buf32[5] = LPC_GPDMA->ENBLDCHNS;
        buf32[6] = LPC_GPDMA->RAWINTTCSTAT;
        buf32[7] = LPC_GPDMA->RAWINTERRSTAT;
        buf32[8] = LPC_GPDMA->CH[0].CONFIG;
        buf32[9] = LPC_GPDMA->CH[0].CONTROL;
        buf32[10] = LPC_GPDMA->CH[0].SRCADDR;
        buf32[11] = LPC_GPDMA->CH[0].DESTADDR;
        buf32[12] = LPC_GPDMA->CH[0].LLI;
        buf32[13] = (uint32_t) hsadc_current_lli;
        buf32[14] = hsadc_next_sequence;
        buf32[15] = 0xDEADBEEF;

        lpcsdr_usb_ep0_data_in(buf, 16*4);
        return;

    case 0x0B:
        /* memory read */
        if (length > sizeof(lpcsdr_usb_control_buffer)) {
            lpcsdr_usb_ep0_stall();
            return;
        }

        memcpy(buf, (uint8_t*) valueAndIndex, length);
        lpcsdr_usb_ep0_data_in(buf, length);
        return;

    default:
        lpcsdr_usb_ep0_stall();
        return;
    }

}

static void m4_usb_ep0_out(const ipc_message_t *message)
{
    uint32_t request = message->values[0];
    uint32_t valueAndIndex = message->values[1];
    uint32_t length = message->values[2];

    const uint8_t *buf = lpcsdr_usb_control_buffer;
    const uint32_t *buf32 = (const uint32_t *)buf;

    switch (request) {
    case 0x01:
        /* comms check */
        if (length != 4 || buf32[0] != 0xDEADBEEF) {
            lpcsdr_usb_ep0_stall();
            return;
        }

        lpcsdr_usb_ep0_out_ack();
        return;

    case 0x10:
        /* SPI: write data */
        if (valueAndIndex > 0x00FFFFFF || valueAndIndex + length > 0x01000000) {
            lpcsdr_usb_ep0_stall();
            return;
        }

        if (lpcsdr_spifi_page_program(valueAndIndex, buf, length) != LPC_OK) {
            lpcsdr_usb_ep0_stall();
            return;
        }

        lpcsdr_usb_ep0_out_ack();
        return;

    case 0x11:
        /* SPI: erase sector */
        if (valueAndIndex > 0x00FFFFFF || (valueAndIndex & 0x0FFF) != 0) {
            lpcsdr_usb_ep0_stall();
            return;
        }

        if (lpcsdr_spifi_sector_erase(valueAndIndex) != LPC_OK) {
            lpcsdr_usb_ep0_stall();
            return;
        }

        lpcsdr_usb_ep0_out_ack();
        return;

    case 0x12:
        /* Start ADC clock */
        if (length != sizeof(hsadc_clock_config_t)) {
            lpcsdr_usb_ep0_stall();
            return;
        }

        if (!lpcsdr_hsadc_clock_start((hsadc_clock_config_t *) buf)) {
            lpcsdr_usb_ep0_stall();
            return;
        }

        lpcsdr_usb_ep0_out_ack();
        return;

    case 0x13:
        /* Start ADC conversion & bulk transfer */
        lpcsdr_dma_hsadc_start();
        lpcsdr_hsadc_conversion_start();
        lpcsdr_usb_ep0_out_ack();
        return;

    case 0x14:
        /* Stop ADC conversion & bulk transfer */
        lpcsdr_dma_hsadc_stop();
        lpcsdr_hsadc_conversion_stop();
        lpcsdr_usb_ep0_out_ack();
        return;

    default:
        lpcsdr_usb_ep0_stall();
        return;
    }
}

static void m4_handle_message(const ipc_message_t *message)
{
    switch (message->message) {
    case M4_QUEUE_TEST_DATA:
        m4_queue_test_data();
        break;

    case M4_UPDATE_POWER_STATE:
        m4_update_power_state();
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
    set_low_power_mode();

    lpcsdr_gpio_init();
    lpcsdr_spifi_init();
    lpcsdr_dma_init();
    lpcsdr_hsadc_init();
    lpcsdr_ipc_init();
    lpcsdr_usb_init();

    /* enable CLK0/CLK2 for ADC clock measurement */
    Chip_SCU_ClockPinMuxSet(0, SCU_MODE_FUNC1 | SCU_MODE_INACT);
    Chip_SCU_ClockPinMuxSet(2, SCU_MODE_FUNC1 | SCU_MODE_INACT);

    lpcsdr_ipc_handle_messages_forever(m4_handle_message);

    // not reached
}
