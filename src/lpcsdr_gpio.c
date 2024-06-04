#include "lpcsdr_gpio.h"

#include "chip.h"

static bool led_state = false;

/* pin / GPIO definitions for LEDs that we will use */
typedef struct {
    uint8_t pingrp;     /* SCU pin group */
    uint8_t pinnum;     /* SCU pin number */
    uint8_t gpioport;   /* GPIO port */
    uint8_t gpiopin;    /* GPIO pin number */
} led_pin_t;

static const led_pin_t led_pins[] = {
        { .pingrp = 1, .pinnum = 1, .gpioport = 0, .gpiopin = 8 },    /* P1_1,  GPIO0[8],  lpc-link2 board LED */
        { .pingrp = 1, .pinnum = 17, .gpioport = 0, .gpiopin = 12 },  /* P1_17, GPIO0[12], v1 prototype board LED */
        { .pingrp = 1, .pinnum = 18, .gpioport = 0, .gpiopin = 13 },  /* P1_18, GPIO0[13], v1 prototype board LED */
        { .pingrp = 1, .pinnum = 20, .gpioport = 0, .gpiopin = 15 },  /* P1_20, GPIO0[15], v1 prototype board LED */
};
#define NUM_LEDS (sizeof(led_pins) / sizeof(led_pins[0]))

ErrorCode_t lpcsdr_gpio_init(void)
{
    /* Set pin functions for pins we care about. See UM10503 ch. 16 table 187 for pin numbering */
    for (unsigned i = 0; i < NUM_LEDS; ++i) {
        Chip_SCU_PinMuxSet(led_pins[i].pingrp, led_pins[i].pinnum, SCU_MODE_PULLDOWN | SCU_MODE_FUNC0);
    }

    Chip_GPIO_Init(LPC_GPIO_PORT);
    for (unsigned i = 0; i < NUM_LEDS; ++i) {
        Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, led_pins[i].gpioport, led_pins[i].gpiopin);
        Chip_GPIO_SetPinState(LPC_GPIO_PORT, led_pins[i].gpioport, led_pins[i].gpiopin, false);
    }

    return LPC_OK;
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
