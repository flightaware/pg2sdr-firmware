#ifndef PG2SDR_HARDWARE_H
#define PG2SDR_HARDWARE_H

/*
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

#if defined(HW_IS_PG2SDR)

# define HW_LABEL "pg2sdr"
# define HW_USES_I2C0
# define HW_TUNER_XTAL 28800000
/* RF_EN output on P2_12 / GPIO1[12], external pulldown */
# define HW_RFEN_PINGRP 2
# define HW_RFEN_PINNUM 12
# define HW_RFEN_GPIO_PORT 1
# define HW_RFEN_GPIO_PIN 12
# define HW_HAS_UART

/* enable CLK0/2 test points in debug builds */
# ifdef DEBUG
#  define HW_HAS_CLKOUT
# else
#  undef HW_HAS_CLKOUT
# endif

#elif defined(HW_IS_AIRSPYMINI)

/* main Airspy Mini hardware differences:
 *  - tuner connected to I2C1, not I2C0
 *  - tuner crystal is 24MHz
 *  - tuner power control GPIO is on P1_14 / GPIO1[7]
 *  - no UART pins or CLK0/2 test points
 */

# define HW_LABEL "airspymini"
# define HW_VID VID_AIRSPYMINI
# define HW_PID PID_AIRSPYMINI
# define HW_USES_I2C1
# define HW_TUNER_XTAL 24000000
# define HW_RFEN_PINGRP 1
# define HW_RFEN_PINNUM 14
# define HW_RFEN_GPIO_PORT 1
# define HW_RFEN_GPIO_PIN 7
# undef HW_HAS_UART
# undef HW_HAS_CLKOUT

#else

# error set one of HW_IS_...

#endif

/* Disable UART in release builds entirely */
#ifndef DEBUG
# undef HW_HAS_UART
#endif

#endif /* PG2SDR_HARDWARE_H */
