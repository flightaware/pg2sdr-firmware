/*
 *  pg2sdr_gpio.c - PG2 firmware, GPIO & LED handling
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

#include "pg2sdr_gpio.h"
#include "pg2sdr_hardware.h"

#include "chip.h"
#include "stopwatch.h"

/* pin / GPIO definitions for LEDs that we will use */
typedef struct {
    bool valid;         /* is this entry valid? */
    uint8_t pingrp;     /* SCU pin group */
    uint8_t pinnum;     /* SCU pin number */
    uint8_t gpioport;   /* GPIO port */
    uint8_t gpiopin;    /* GPIO pin number */
} led_pin_t;

typedef struct {
    bool bicolor;       /* true if this is a green/red bicolor LED */
    led_pin_t a;        /* single color LED, or green half of a bicolor LED */
    led_pin_t b;        /* red half of a bicolor LED */
} led_pair_t;

/* See UM10503 ch. 16 table 187 for pin numbering */
static const led_pair_t led_pairs[] = {
#ifdef HW_IS_PG2SDR
    [0] = { .bicolor = false,
            .a = { .valid = true, .pingrp = 1, .pinnum = 1,  .gpioport = 0, .gpiopin = 8  },    /* P1_1, GPIO0[8] (2.2k pullup), D2 (yellow LED) */
            .b = { .valid = false }, },
    [1] = { .bicolor = true,
            .a = { .valid = true, .pingrp = 1, .pinnum = 16, .gpioport = 0, .gpiopin = 3  },    /* P1_16, GPIO0[3],  DS1A (DS1, green LED) */
            .b = { .valid = true, .pingrp = 1, .pinnum = 18, .gpioport = 0, .gpiopin = 13 }, }, /* P1_18, GPIO0[13], DS1B (DS1, red LED) */
    [2] = { .bicolor = true,
            .a = { .valid = true, .pingrp = 1, .pinnum = 17, .gpioport = 0, .gpiopin = 12 },    /* P1_17, GPIO0[12], DS2A (DS2, green LED) */
            .b = { .valid = true, .pingrp = 1, .pinnum = 20, .gpioport = 0, .gpiopin = 15 }, }, /* P1_20, GPIO0[15], DS2B (DS2, red LED) */
#endif

#ifdef HW_IS_AIRSPY
    [0] = { .bicolor = false,
            .a = { .valid = true, .pingrp = 1, .pinnum = 17, .gpioport = 0, .gpiopin = 12 },
            .b = { .valid = false }, },
#endif
};
#define NUM_LEDS (sizeof(led_pairs) / sizeof(led_pairs[0]))

static void led_pin_init(const led_pin_t *led)
{
    if (led->valid) {
        Chip_SCU_PinMuxSet(led->pingrp, led->pinnum, SCU_MODE_INACT | SCU_MODE_FUNC0);
        Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, led->gpioport, led->gpiopin);
        Chip_GPIO_SetPinState(LPC_GPIO_PORT, led->gpioport, led->gpiopin, false);
    }
}

static void led_pin_set(const led_pin_t *led, bool onoff)
{
    if (led->valid)
        Chip_GPIO_SetPinState(LPC_GPIO_PORT, led->gpioport, led->gpiopin, onoff);
}

void pg2sdr_gpio_init(void)
{
    Chip_GPIO_Init(LPC_GPIO_PORT);

    /* Set pin functions and initial GPIO state for pins we care about.  */
    for (unsigned i = 0; i < NUM_LEDS; ++i) {
        led_pin_init(&led_pairs[i].a);
        led_pin_init(&led_pairs[i].b);
    }

    /* SW1 (BOOT2) on P2_8 / GPIO5[7], external 2.2k pullup to VDD or direct connection to GND */
    Chip_SCU_PinMuxSet(2, 8, SCU_MODE_INACT | SCU_MODE_INBUFF_EN | SCU_MODE_FUNC4);
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, 5, 7);

    /* RFEN pin / GPIO */
    Chip_SCU_PinMuxSet(HW_RFEN_PINGRP, HW_RFEN_PINNUM, SCU_MODE_INACT | SCU_MODE_FUNC0);
    Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, HW_RFEN_GPIO_PORT, HW_RFEN_GPIO_PIN);
    Chip_GPIO_SetPinState(LPC_GPIO_PORT, HW_RFEN_GPIO_PORT, HW_RFEN_GPIO_PIN, false);

#ifdef HW_IS_PG2SDR
    /* SW2 on P2_13 / GPIO1[13], floating or direct connection to GND, no external pullup so use the internal pullup */
    Chip_SCU_PinMuxSet(2, 13, SCU_MODE_PULLUP | SCU_MODE_INBUFF_EN | SCU_MODE_FUNC0);
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, 1, 13);
#endif

    /* test pattern, cycle all the LEDS */
    for (unsigned i = 0; i < NUM_LEDS; ++i) {
        pg2sdr_led_set(i, C_ON);
        StopWatch_DelayMs(250);
        pg2sdr_led_set(i, C_OFF);
    }
}

void pg2sdr_led_set(unsigned led_id, Color c)
{
    if (led_id >= NUM_LEDS)
        return;

    const led_pair_t *pair = &led_pairs[led_id];
    if (!pair->bicolor) {
        led_pin_set(&pair->a, (c != C_OFF));
        return;
    }

    switch (c) {
    case C_OFF:
        led_pin_set(&pair->a, false);
        led_pin_set(&pair->b, false);
        break;

    case C_RED:
        led_pin_set(&pair->a, false);
        led_pin_set(&pair->b, true);
        break;

    case C_GREEN:
        led_pin_set(&pair->a, true);
        led_pin_set(&pair->b, false);
        break;

    case C_YELLOW:
    case C_ON:
    default:
        led_pin_set(&pair->a, true);
        led_pin_set(&pair->b, true);
        break;
    }
}

bool pg2sdr_read_sw1(void)
{
    return Chip_GPIO_GetPinState(LPC_GPIO_PORT, 5, 7);
}

bool pg2sdr_read_sw2(void)
{
#if defined(HW_IS_PG2SDR)
    return Chip_GPIO_GetPinState(LPC_GPIO_PORT, 1, 13);
#else
    return true;
#endif
}

void pg2sdr_set_rfen(bool onoff)
{
    Chip_GPIO_SetPinState(LPC_GPIO_PORT, HW_RFEN_GPIO_PORT, HW_RFEN_GPIO_PIN, onoff);
}
