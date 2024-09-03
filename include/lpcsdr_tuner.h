#ifndef LPCSDR_TUNER_H
#define LPCSDR_TUNER_H

#include "lpc_types.h"

void lpcsdr_tuner_init(void);
void lpcsdr_i2c_clock_update(void);
void lpcsdr_tuner_handle_poweron(void);
void lpcsdr_tuner_handle_poweroff(void);

bool lpcsdr_tuner_shadow_to_chip(int *status);
bool lpcsdr_tuner_shadow_from_chip(int *status);

bool lpcsdr_tuner_read_regs_direct(uint8_t *regs, unsigned count, int *status);
bool lpcsdr_tuner_write_regs_direct(unsigned offset, const uint8_t *regs, unsigned count, int *status);

bool lpcsdr_tuner_read_reg(unsigned index, uint8_t *value, int *status);
bool lpcsdr_tuner_write_regs(unsigned offset, const uint8_t *regs, unsigned count, int *status);
bool lpcsdr_tuner_update_regs(unsigned offset, const uint8_t *bits, const uint8_t *mask, unsigned count, int *status);

#endif /* LPCSDR_TUNER_H */
