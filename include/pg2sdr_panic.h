#ifndef PG2SDR_PANIC_H
#define PG2SDR_PANIC_H

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

#include <stdint.h>
#include "chip.h"
#include "morse.h"

/* Reboot / panic related stuff */

/* Unexpected-interrupt handler, will panic the system when called */
void pg2sdr_unexpected_interrupt(void) __attribute__(( noreturn ));

/* Hard-reset the system immediately */
void pg2sdr_hard_reset(void) __attribute__(( noreturn ));

/* Reset the system normally (RESET_FIRMWARE) */
void pg2sdr_reset() __attribute__(( noreturn ));

/* Trigger a firmware panic:
 *   - record the source of the reset as a panic (RESET_PANIC)
 *   - blink the LEDs in the given morse pattern for a while
 *   - reset the chip
 */
void pg2sdr_panic(uint32_t pattern) __attribute__(( noreturn ));

/* Called early during initialization:
 *   - Save the currently stored reset cause so we can inspect it later
 *   - Write some debug output to the UART describing the reset
 *   - Update the currently stored reset cause to "unexpected". We will update this later before any "expected" reset
 */
void pg2sdr_diagnose_reset();

void _pg2sdr_assertion_failed(const char *file, unsigned line, const char *assertion) __attribute__(( noreturn ));

#define pg2sdr_assertion_failed(message) _pg2sdr_assertion_failed(__FILE__, __LINE__, message)

#define panic_assert(_x) do {                                    \
  if (!(_x)) pg2sdr_assertion_failed(#_x);                       \
} while(0)

extern uint32_t pg2sdr_reset_reason;
extern uint32_t pg2sdr_reset_code;

#endif
