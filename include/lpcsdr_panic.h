#ifndef LPCSDR_PANIC_H
#define LPCSDR_PANIC_H

#include <stdint.h>
#include "chip.h"

/* Reboot / panic related stuff */

/* Unexpected-interrupt handler, will panic the system when called */
void lpcsdr_unexpected_interrupt(void) __attribute__(( noreturn ));

/* Hard-reset the system immediately */
void lpcsdr_hard_reset(void) __attribute__(( noreturn ));

/* Reset the system normally (RESET_FIRMWARE) */
void lpcsdr_reset() __attribute__(( noreturn ));

/* Trigger a firmware panic:
 *   - record the source of the reset as a panic (RESET_PANIC)
 *   - blink the LEDs in the given morse pattern for a while
 *   - reset the chip
 */
void lpcsdr_panic(uint32_t pattern) __attribute__(( noreturn ));

/* Called early during initialization:
 *   - Save the currently stored reset cause so we can inspect it later
 *   - Write some debug output to the UART describing the reset
 *   - Update the currently stored reset cause to "unexpected". We will update this later before any "expected" reset
 */
void lpcsdr_diagnose_reset();

extern uint32_t lpcsdr_reset_reason;
extern uint32_t lpcsdr_reset_code;

#endif
