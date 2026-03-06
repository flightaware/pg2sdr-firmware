#ifndef PG2SDR_TUNER_H
#define PG2SDR_TUNER_H

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

void pg2sdr_tuner_init(void);
void pg2sdr_tuner_clock_update(void);
void pg2sdr_tuner_handle_poweron(void);
void pg2sdr_tuner_handle_poweroff(void);

/* general rule: these functions return false on failure, and internally record any I2C errors */

bool pg2sdr_tuner_shadow_from_chip();

/* read `count` registers directly from the tuner, starting from register 0, and place the results in `regs[0..count-1]` */
bool pg2sdr_tuner_read_regs_direct(uint8_t *regs, unsigned count);
/* write `count` registers directly to the tuner, starting from register `offset`, with values from `regs[0..count-1]` */
bool pg2sdr_tuner_write_regs_direct(unsigned offset, const uint8_t *regs, unsigned count);

/* read one register at index `index` using the shadow cache where possible and place the resulting value in `*value` */
bool pg2sdr_tuner_read_reg(unsigned index, uint8_t *value);
/* read many registers, starting at register `offset`, using the cache where possible, placing results in `regs[0..count-1]` */
bool pg2sdr_tuner_read_regs(unsigned offset, uint8_t *regs, unsigned count);
/* write many registers, starting at register `offset`, with values from `regs[0..count-1]`, also updating the shadow cache */
bool pg2sdr_tuner_write_regs(unsigned offset, const uint8_t *regs, unsigned count);
/* update many registers, starting at register `offset`, reading and updating the cache and writing through to the chip, where the update logic is:
 *   register[offset+N] = (register[offset+N] & ~mask[N]) | (bits[N] & ~mask[N])
 * ie: set bits in mask[0..count-1] indicates which bits should be modified
 * and bits[0..count-1] provides the new bit values for those bits being modified
 */
bool pg2sdr_tuner_update_regs(unsigned offset, const uint8_t *bits, const uint8_t *mask, unsigned count);

/* write a given value to vco_current, then wait for up to "timeout" ms for PLL lock
 * returns 0 (PLL not locked), 1 (PLL locked), or <0 (communication error)
 */
int pg2sdr_tuner_lock(uint8_t vco_current, uint32_t timeout);

void pg2sdr_tuner_status(ep0_in_board_status_t *status);

#endif /* PG2SDR_TUNER_H */
