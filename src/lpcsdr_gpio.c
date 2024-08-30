#include "lpcsdr_gpio.h"

#include "chip.h"
#include "stopwatch.h"

static bool led_state = false;

/* pin / GPIO definitions for LEDs that we will use */
typedef struct {
    uint8_t pingrp;     /* SCU pin group */
    uint8_t pinnum;     /* SCU pin number */
    uint8_t gpioport;   /* GPIO port */
    uint8_t gpiopin;    /* GPIO pin number */
} led_pin_t;

static const led_pin_t led_pins[] = {
        { .pingrp = 1, .pinnum = 1, .gpioport = 0, .gpiopin = 8 },    /* P1_1, GPIO0[8] (2.2k pullup), v2: D2, lpc-link: BOOT0_LED */
        { .pingrp = 1, .pinnum = 16, .gpioport = 0, .gpiopin = 3 },   /* P1_16, GPIO0[3], v2: DS1A */
        { .pingrp = 1, .pinnum = 17, .gpioport = 0, .gpiopin = 12 },  /* P1_17, GPIO0[12], v1: DS1A, v2: DS2A */
        { .pingrp = 1, .pinnum = 18, .gpioport = 0, .gpiopin = 13 },  /* P1_18, GPIO0[13], v1: DS2A, v2: DS1B */
        { .pingrp = 1, .pinnum = 20, .gpioport = 0, .gpiopin = 15 },  /* P1_20, GPIO0[15], v1: DS2B, v2: DS2B */
};
#define NUM_LEDS (sizeof(led_pins) / sizeof(led_pins[0]))

void lpcsdr_gpio_init(void)
{
    /* Set pin functions for pins we care about. See UM10503 ch. 16 table 187 for pin numbering */
    for (unsigned i = 0; i < NUM_LEDS; ++i) {
        Chip_SCU_PinMuxSet(led_pins[i].pingrp, led_pins[i].pinnum, SCU_MODE_INACT | SCU_MODE_FUNC0);
    }

    Chip_GPIO_Init(LPC_GPIO_PORT);
    for (unsigned i = 0; i < NUM_LEDS; ++i) {
        Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, led_pins[i].gpioport, led_pins[i].gpiopin);
        Chip_GPIO_SetPinState(LPC_GPIO_PORT, led_pins[i].gpioport, led_pins[i].gpiopin, false);
    }

    /* v1/v2 prototype board, SW1 (BOOT2) on P2_8 / GPIO5[7], external 2.2k pullup to VDD or direct connection to GND */
    Chip_SCU_PinMuxSet(2, 8, SCU_MODE_INACT | SCU_MODE_INBUFF_EN | SCU_MODE_FUNC4);
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, 5, 7);

    /* v1/v2 prototype board, SW2 on P2_13 / GPIO1[13], floating or direct connection to GND, no external pullup so use the internal pullup */
    Chip_SCU_PinMuxSet(2, 13, SCU_MODE_PULLUP | SCU_MODE_INBUFF_EN | SCU_MODE_FUNC0);
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, 1, 13);

    /* v1/v2 prototype board, RF_EN output on P2_12 / GPIO1[12], external pulldown */
    Chip_SCU_PinMuxSet(2, 12, SCU_MODE_INACT | SCU_MODE_FUNC0);
    Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, 1, 12);
    Chip_GPIO_SetPinState(LPC_GPIO_PORT, 1, 12, false);

    /* test pattern, cycle all the LEDS */
    for (unsigned repeat = 0; repeat < 4; ++repeat) {
        for (unsigned i = 0; i < NUM_LEDS; ++i) {
            Chip_GPIO_SetPinState(LPC_GPIO_PORT, led_pins[i].gpioport, led_pins[i].gpiopin, true);
            StopWatch_DelayMs(250);
            Chip_GPIO_SetPinState(LPC_GPIO_PORT, led_pins[i].gpioport, led_pins[i].gpiopin, false);
        }
    }
}

void lpcsdr_led_set(bool onoff)
{
    led_state = onoff;
    for (unsigned i = 0; i < NUM_LEDS; ++i) {
        Chip_GPIO_SetPinState(LPC_GPIO_PORT, led_pins[i].gpioport, led_pins[i].gpiopin, led_state);
    }
}

void lpcsdr_led_toggle(void)
{
    lpcsdr_led_set(!led_state);
}

bool lpcsdr_read_sw1(void)
{
    return Chip_GPIO_GetPinState(LPC_GPIO_PORT, 5, 7);
}

bool lpcsdr_read_sw2(void)
{
    return Chip_GPIO_GetPinState(LPC_GPIO_PORT, 1, 13);
}

void lpcsdr_set_rfen(bool onoff)
{
    Chip_GPIO_SetPinState(LPC_GPIO_PORT, 1, 12, onoff);
}
