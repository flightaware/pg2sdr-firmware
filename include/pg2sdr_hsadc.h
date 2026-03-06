#ifndef PG2SDR_HSADC_H
#define PG2SDR_HSADC_H

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

#include "lpc_types.h"
#include "pg2sdr_protocol.h"

void pg2sdr_hsadc_init(void);

bool pg2sdr_hsadc_clock_start(uint32_t n_divisor,     /* PLL0AUDIO pre-divisor (0 = bypass divider */
                              uint32_t m_divisor,     /* PLL0AUDIO feedback divisor, fixed point, 15 bit fractional part */
                              uint32_t p_divisor,     /* PLL0AUDIO post-divisor (0 = bypass divider */
                              uint32_t idiv_divisor); /* IDIV_E divisor (0 = don't use IDIV_E) */
void pg2sdr_hsadc_clock_stop(void);

bool pg2sdr_hsadc_conversion_start(void);
void pg2sdr_hsadc_conversion_stop(void);

void pg2sdr_hsadc_status(ep0_in_board_status_t *status);

void pg2sdr_hsadc_set_config(bool dcinpos, bool dcinneg, bool twos);

uint32_t pg2sdr_hsadc_get_sampling_rate();

#endif
