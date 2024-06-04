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
#include "lpcsdr_spifi.h"
#include "lpcsdr_gpio.h"

#include <cr_section_macros.h>


int main(void) {
    // Read clock settings and update SystemCoreClock variable
    SystemCoreClockUpdate();

    StopWatch_Init();

    lpcsdr_gpio_init();
    lpcsdr_spifi_init();
    lpcsdr_usb_init();

    while (1) {
        /* Everything is currently interrupt-driven, so just sleep and wait for interrupts */
        __WFI();
    }

    // not reached
}
