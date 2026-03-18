/*
 *  pg2sdr_led.c - PG2 firmware, blinkenlights
 *
 *  Copyright (c) 2026 FlightAware All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in the
 *  documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "pg2sdr_led.h"
#include "pg2sdr_isr.h"

#include "chip.h"

static Color led_state[3] = { C_OFF, C_OFF, C_OFF };  /* last requested LED state */
static uint32_t led_pattern;                          /* current override pattern */
static uint32_t led_pattern_shifted;                  /* partially shifted pattern for this cycle */

void pg2sdr_led_init()
{
    /* we use the RIT to periodically update the LED status */
    Chip_RIT_Init(LPC_RITIMER);
    NVIC_EnableIRQ(RITIMER_IRQn);
}

void pg2sdr_led_clock_update()
{
    /* M4 base clock frequency changed, update RIT interval
     * as the RIT counter runs off the M4 clock
     */
    Chip_RIT_SetTimerInterval(LPC_RITIMER, 250 /* milliseconds */);
}

void pg2sdr_led_set(unsigned led_id, Color c)
{
    if (led_id >= (sizeof(led_state) / sizeof(led_state[0])))
        return;

    led_state[led_id] = c;

    if (!led_pattern)
        pg2sdr_gpio_led_set(led_id, c);
}

/* LED patterns are a cycle of 1..6 combinations of LED state,
 * packed into a uint32_t as a series of 5-bit values, starting
 * from the low bits. The 5-bit values are cycled through repeatedly
 * at 4Hz (i.e. 250ms per value). Each 5-bit value is formatted as:
 *
 * bit 4:    yellow RF power LED on/off
 * bit 3:    first bicolor LED, green LED on/off
 * bit 2:    first bicolor LED, red LED on/off
 * bit 1:    second bicolor LED, green LED on/off
 * bit 0:    second bicolor LED, red LED on/off
 *
 * Any entirely-zero high bits are skipped, to allow for cycles
 * that are shorter than 6 entries. For sequences that include
 * an all-LEDs-off state, ensure that this is not the last
 * (highest-bits) entry -- rotate the sequence (e.g. start
 * with the all-LEDs-off state) to achieve this.
 *
 * A pattern that's entirely zero means "no pattern, use normal
 * LED output"
 */
static void update_leds()
{
    if (!led_pattern) {
        /* set normal state */
        pg2sdr_gpio_led_set(0, led_state[0]);
        pg2sdr_gpio_led_set(1, led_state[1]);
        pg2sdr_gpio_led_set(2, led_state[2]);
        return;
    }

    /* cycle override pattern */
    Color l0 = (led_pattern_shifted >> 4) & 1; /* yellow RF power LED */
    Color l1 = (led_pattern_shifted >> 2) & 3; /* first bicolor LED */
    Color l2 = (led_pattern_shifted >> 0) & 3; /* second bicolor LED */

    pg2sdr_gpio_led_set(0, l0);
    pg2sdr_gpio_led_set(1, l1);
    pg2sdr_gpio_led_set(2, l2);

    /* shift in next entry */
    led_pattern_shifted >>= 5;
    if (!led_pattern_shifted) {
        /* no further entries, cycle back to the start */
        led_pattern_shifted = led_pattern;
    }
}    

void pg2sdr_led_set_pattern(uint32_t pattern)
{
    WITH_DISABLED_INTERRUPTS {
        led_pattern = led_pattern_shifted = pattern;
        update_leds();
    }

    if (pattern) {
        Chip_RIT_Enable(LPC_RITIMER);
    } else {
        Chip_RIT_Disable(LPC_RITIMER);
    }
}

void RITIMER_IRQHandler()
{    
    Chip_RIT_ClearInt(LPC_RITIMER); /* ack the interrupt */
    update_leds();
}
