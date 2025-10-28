#ifndef PG2SDR_M4CLOCK_H
#define PG2SDR_M4CLOCK_H

#include "lpcsdr_common.h"
#include "lpcsdr_protocol.h"

void pg2sdr_m4clock_init();
void pg2sdr_m4clock_set_freq(uint32_t new_freq, bool first_time_init);
void pg2sdr_m4clock_status(ep0_in_board_status_t *status);

/* Works like __WFI(), pausing until an interrupt becomes pending, but also updates
 * the internal accounting of idle time. Should be called with interrupts disabled.
 */
void pg2sdr_m4clock_wfi();

#endif
