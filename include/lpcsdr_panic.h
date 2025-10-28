#ifndef PG2SDR_PANIC_H
#define PG2SDR_PANIC_H

#include <stdint.h>
#include "chip.h"

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

void pg2sdr_assertion_failed(const char *file, unsigned line, const char *assertion) __attribute__(( noreturn ));

#define panic_assert(_x) do {                                    \
    if (!(_x)) pg2sdr_assertion_failed(__FILE__, __LINE__, #_x); \
} while(0)

extern uint32_t pg2sdr_reset_reason;
extern uint32_t pg2sdr_reset_code;

#endif
