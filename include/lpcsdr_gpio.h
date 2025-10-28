#ifndef PG2SDR_GPIO_H
#define PG2SDR_GPIO_H

#include <stdbool.h>
#include "error.h"

/* for single color LEDs, anything other than "off" is considered "on" */
typedef enum Color {
    C_OFF,
    C_RED,
    C_GREEN,
    C_YELLOW,
    C_ON,
} Color;

void pg2sdr_gpio_init(void);
void pg2sdr_led_set(unsigned led_id, Color c);

void pg2sdr_set_rfen(bool onoff);

bool pg2sdr_read_sw1(void);
bool pg2sdr_read_sw2(void);

#endif /* PG2SDR_GPIO_H */
