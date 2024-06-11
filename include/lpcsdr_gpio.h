#ifndef LPCSDR_GPIO_H
#define LPCSDR_GPIO_H

#include <stdbool.h>
#include "error.h"

ErrorCode_t lpcsdr_gpio_init(void);
void lpcsdr_led_set(bool onoff);
void lpcsdr_led_toggle(void);

bool lpcsdr_read_sw1(void);
bool lpcsdr_read_sw2(void);

#endif /* LPCSDR_USB_H */
