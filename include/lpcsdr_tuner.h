#ifndef LPCSDR_TUNER_H
#define LPCSDR_TUNER_H

#include "lpc_types.h"
#include "lpcsdr_protocol.h"

void lpcsdr_tuner_init(void);
void lpcsdr_tuner_clock_update(void);
void lpcsdr_tuner_handle_poweron(void);
void lpcsdr_tuner_handle_poweroff(void);

/* general rule: these functions return false on failure, and internally record any I2C errors */

bool lpcsdr_tuner_shadow_from_chip();

/* read `count` registers directly from the tuner, starting from register 0, and place the results in `regs[0..count-1]` */
bool lpcsdr_tuner_read_regs_direct(uint8_t *regs, unsigned count);
/* write `count` registers directly to the tuner, starting from register `offset`, with values from `regs[0..count-1]` */
bool lpcsdr_tuner_write_regs_direct(unsigned offset, const uint8_t *regs, unsigned count);

/* read one register at index `index` using the shadow cache where possible and place the resulting value in `*value` */
bool lpcsdr_tuner_read_reg(unsigned index, uint8_t *value);
/* read many registers, starting at register `offset`, using the cache where possible, placing results in `regs[0..count-1]` */
bool lpcsdr_tuner_read_regs(unsigned offset, uint8_t *regs, unsigned count);
/* write many registers, starting at register `offset`, with values from `regs[0..count-1]`, also updating the shadow cache */
bool lpcsdr_tuner_write_regs(unsigned offset, const uint8_t *regs, unsigned count);
/* update many registers, starting at register `offset`, reading and updating the cache and writing through to the chip, where the update logic is:
 *   register[offset+N] = (register[offset+N] & ~mask[N]) | (bits[N] & ~mask[N])
 * ie: set bits in mask[0..count-1] indicates which bits should be modified
 * and bits[0..count-1] provides the new bit values for those bits being modified
 */
bool lpcsdr_tuner_update_regs(unsigned offset, const uint8_t *bits, const uint8_t *mask, unsigned count);

/* write a given value to vco_current, then wait for up to "timeout" ms for PLL lock
 * returns 0 (PLL not locked), 1 (PLL locked), or <0 (communication error)
 */
int lpcsdr_tuner_lock(uint8_t vco_current, uint32_t timeout);

void lpcsdr_tuner_status(ep0_in_board_status_t *status);

#endif /* LPCSDR_TUNER_H */
