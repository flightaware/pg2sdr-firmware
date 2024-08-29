#ifndef LPCSDR_GPIO_H
#define LPCSDR_GPIO_H

#include <stdbool.h>
#include "error.h"

void lpcsdr_gpio_init(void);
void lpcsdr_led_set(bool onoff);
void lpcsdr_led_toggle(void);

void lpcsdr_set_rfen(bool onoff);

bool lpcsdr_read_sw1(void);
bool lpcsdr_read_sw2(void);

#endif /* LPCSDR_GPIO_H */
