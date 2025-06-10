#ifndef LPCSDR_HARDWARE_H
#define LPCSDR_HARDWARE_H

#if defined(HW_IS_LPCSDR)

# define HW_USES_I2C0
# define HW_TUNER_XTAL 28800000
/* RF_EN output on P2_12 / GPIO1[12], external pulldown */
# define HW_RFEN_PINGRP 2
# define HW_RFEN_PINNUM 12
# define HW_RFEN_GPIO_PORT 1
# define HW_RFEN_GPIO_PIN 12
# define HW_HAS_UART

#else

# error set one of HW_IS_...

#endif

#endif /* LPCSDR_HARDWARE_H */
