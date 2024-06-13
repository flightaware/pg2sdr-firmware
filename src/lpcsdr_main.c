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
#include "lpcsdr_hsadc.h"
#include "lpcsdr_spifi.h"
#include "lpcsdr_gpio.h"

#include <cr_section_macros.h>

static volatile bool wakeup_requested = false;

static void wake_m4(void)
{
    wakeup_requested = true;
}

void lpcsdr_usb_space_available(void)
{
    wake_m4();
}

void lpcsdr_usb_state_changed(void)
{
    wake_m4();
}

static void pack_samples(const void *src, void *dst, uint32_t src_len)
{
    __asm__ volatile (
            "1: ldm.w %0!, {r1,r2,r3,r4}\n\t"    // load 4 words (8 samples)
            "bic r1,r1,#0xF000F000\n\t"          // clear high 4 bits of first 6 samples (could be omitted if we trust the hardware)
            "bic r2,r2,#0xF000F000\n\t"
            "bic r3,r3,#0xF000F000\n\t"
            "and r5,r4,#0x0F000F00\n\t"          // extract bits 11:8 of samples 7/8
            "orr r1,r1,r5,lsl #4\n\t"            //  and insert into the top 4 bits of samples 1/2
            "and r5,r4,#0x00F000F0\n\t"          // extract bits 7:4 of samples 7/8
            "orr r2,r2,r5,lsl #8\n\t"            //  and insert into the top 4 bits of samples 3/4
            "and r5,r4,#0x000F000F\n\t"          // extract bits 3:0 of samples 7/8
            "orr r3,r3,r5,lsl #12\n\t"           //  and insert into the top 4 bits of samples 5/6
            "stm.w %1!, {r1,r2,r3}\n\t"          // store 3 words
            "subs %2,%2,#16\n\t"
            "bgt 1b"                             // loop until done
            : "+r" (src), "+r" (dst), "+r" (src_len)       /* outputs */
            :                                              /* inputs */
            : "r1", "r2", "r3", "r4", "r5", "cc", "memory" /* clobber */);
}

#if 0
/* a little linear congruential PRNG, just to get some randomness in the USB data we transfer */
static uint32_t random_state = 123456789;
static void random_fill_byte(uint8_t *buffer, unsigned size)
{
    for (unsigned i = 0; i < size; ++i) {
        random_state = random_state * 0xD9F5 + 1;
        *buffer++ = (uint8_t) (random_state >> 24);
    }
}

static void random_fill_word(uint8_t *buffer, unsigned size)
{
    uint32_t *u32 = (uint32_t*) buffer;
    for (unsigned i = 0; i < size/4; ++i) {
        random_state = random_state * 0xD9F5 + 1;
        *u32++ = random_state;
    }
}
#endif

static bool high_power_mode = false;

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
    Chip_SetupCoreClock(CLKIN_CRYSTAL, /* MAX_CLOCK_FREQ */ 120000000, false);
    SystemCoreClockUpdate();
    StopWatch_Init();
}

static void m4_work(void)
{
    bool usb_ready = lpcsdr_usb_is_ready();
    if (!usb_ready && high_power_mode) {
        set_low_power_mode();
    } else if (usb_ready && !high_power_mode) {
        // later: have a control transfer to enable high-power mode / start sampling
        set_high_power_mode();
    }

    while (usb_ready) {
        USB_DTD_T *dtd = lpcsdr_usb_get_dtd();
        if (!dtd)
            break;

//        sequence_fill(dtd->buffer, DTD_BUFFER_SIZE);
//        random_fill_word(dtd->buffer, DTD_BUFFER_SIZE);
//        random_fill_byte(dtd->buffer, DTD_BUFFER_SIZE);
        pack_samples((uint32_t*) 0x20000000, dtd->buffer, (DTD_BUFFER_SIZE / 12) * 16);
        lpcsdr_usb_queue_dtd(dtd, DTD_BUFFER_SIZE);

        usb_ready = lpcsdr_usb_is_ready();
    }
}

int main(void) {
    set_low_power_mode();

    lpcsdr_gpio_init();
    lpcsdr_spifi_init();
    lpcsdr_usb_init();

    while (1) {
        m4_work();

        __disable_irq();
        if (!wakeup_requested)
            __WFI();
        wakeup_requested = false;
        __enable_irq();
    }

    // not reached
}
