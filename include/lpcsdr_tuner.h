#ifndef LPCSDR_TUNER_H
#define LPCSDR_TUNER_H

#include "lpc_types.h"

void lpcsdr_tuner_init(void);
void lpcsdr_i2c_clock_update(void);
bool lpcsdr_tuner_read_regs(uint8_t *regs, unsigned length, int *status);

#endif /* LPCSDR_TUNER_H */
