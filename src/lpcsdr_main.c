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

static void m4_copy_hsadc_buffer(dma_lli_t *buffer, uint32_t dma_status)
{
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

    usb_header_t *header = (usb_header_t*) dTD->buffer;
    header->magic = 0xDEADBEEF;
    header->samples = HSADC_BUFFER_SIZE / 2;
    header->sequence = start_seq;
    header->status = pending_usb_status;

    uint32_t *out_samples = (uint32_t*) (header + 1);

    const uint32_t in_words = HSADC_BUFFER_SIZE/4;
    static_assert(in_words % 4 == 0);
    pack_samples((uint32_t *)buffer->destaddr, (uint32_t *)out_samples, in_words);

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
        m4_copy_hsadc_buffer((dma_lli_t*) message->values[0], message->values[1]);
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
