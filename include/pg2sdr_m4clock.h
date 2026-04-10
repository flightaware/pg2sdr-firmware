#ifndef PG2SDR_M4CLOCK_H
#define PG2SDR_M4CLOCK_H

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

#include "pg2sdr_common.h"
#include "pg2sdr_protocol.h"

void pg2sdr_m4clock_init();
void pg2sdr_m4clock_set_freq(uint32_t new_freq);
void pg2sdr_m4clock_status(ep0_in_board_status_t *status);

/* Works like __WFI(), pausing until an interrupt becomes pending, but also updates
 * the internal accounting of idle time. Should be called with interrupts disabled.
 */
void pg2sdr_m4clock_wfi();

/* busy-wait delay functions using the cycle counter.
 * Both of these should be good for delays of up to
 * about
 */
extern uint32_t m4_cycles_per_ms;
extern uint32_t m4_cycles_per_us;
static inline void pg2sdr_delay_ms(uint32_t ms)
{
    const uint32_t cycles = m4_cycles_per_ms * ms;
    const uint32_t start = DWT->CYCCNT;
    uint32_t now;
    do {
        now = DWT->CYCCNT;
    } while ((now - start) < cycles);
}

static inline void pg2sdr_delay_us(uint32_t us)
{
    const uint32_t cycles = m4_cycles_per_us * us;
    const uint32_t start = DWT->CYCCNT;
    uint32_t now;
    do {
        now = DWT->CYCCNT;
    } while ((now - start) < cycles);
}

#endif
