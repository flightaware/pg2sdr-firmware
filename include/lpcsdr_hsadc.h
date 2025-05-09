#ifndef LPCSDR_HSADC_H
#define LPCSDR_HSADC_H

#include "lpc_types.h"
#include "lpcsdr_protocol.h"

void lpcsdr_hsadc_init(void);

bool lpcsdr_hsadc_clock_start(uint32_t n_divisor,     /* PLL0AUDIO pre-divisor (0 = bypass divider */
                              uint32_t m_divisor,     /* PLL0AUDIO feedback divisor, fixed point, 15 bit fractional part */
                              uint32_t p_divisor,     /* PLL0AUDIO post-divisor (0 = bypass divider */
                              uint32_t idiv_divisor); /* IDIV_E divisor (0 = don't use IDIV_E) */
void lpcsdr_hsadc_clock_stop(void);

bool lpcsdr_hsadc_conversion_start(void);
void lpcsdr_hsadc_conversion_stop(void);

void lpcsdr_hsadc_status(ep0_in_board_status_t *status);

void lpcsdr_hsadc_set_config(bool dcinpos, bool dcinneg, bool twos);

#endif
