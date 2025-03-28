#ifndef LPCSDR_GPIO_H
#define LPCSDR_GPIO_H

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

void lpcsdr_gpio_init(void);
void lpcsdr_led_set(unsigned led_id, Color c);

void lpcsdr_set_rfen(bool onoff);

bool lpcsdr_read_sw1(void);
bool lpcsdr_read_sw2(void);

#endif /* LPCSDR_GPIO_H */
