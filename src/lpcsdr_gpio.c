#include "lpcsdr_gpio.h"
#include "lpcsdr_hardware.h"

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
#ifdef HW_IS_LPCSDR
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

void lpcsdr_gpio_init(void)
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

#ifdef HW_IS_LPCSDR
    /* SW2 on P2_13 / GPIO1[13], floating or direct connection to GND, no external pullup so use the internal pullup */
    Chip_SCU_PinMuxSet(2, 13, SCU_MODE_PULLUP | SCU_MODE_INBUFF_EN | SCU_MODE_FUNC0);
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, 1, 13);
#endif

    /* test pattern, cycle all the LEDS */
    for (unsigned i = 0; i < NUM_LEDS; ++i) {
        lpcsdr_led_set(i, C_ON);
        StopWatch_DelayMs(250);
        lpcsdr_led_set(i, C_OFF);
    }
}

void lpcsdr_led_set(unsigned led_id, Color c)
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

bool lpcsdr_read_sw1(void)
{
    return Chip_GPIO_GetPinState(LPC_GPIO_PORT, 5, 7);
}

bool lpcsdr_read_sw2(void)
{
#if defined(HW_IS_LPCSDR)
    return Chip_GPIO_GetPinState(LPC_GPIO_PORT, 1, 13);
#else
    return true;
#endif
}

void lpcsdr_set_rfen(bool onoff)
{
    Chip_GPIO_SetPinState(LPC_GPIO_PORT, HW_RFEN_GPIO_PORT, HW_RFEN_GPIO_PIN, onoff);
}
