#include "lpcsdr_m4clock.h"
#include "lpcsdr_tuner.h"

#include "chip.h"
#include "stopwatch.h"

/* M4 clock management & load stats */

/* current M4 frequency, Hz */
static uint32_t current_freq;

void lpcsdr_m4clock_init()
{
    /* switch the M4 clock to use the crystal immediately */
    Chip_SetupCoreClock(CLKIN_CRYSTAL, 48000000, false);
    Chip_Clock_SetBaseClock(CLK_BASE_APB1, CLKIN_MAINPLL, true, false);
    Chip_Clock_SetBaseClock(CLK_BASE_APB3, CLKIN_MAINPLL, true, false);

    /* reset internal state, initialize timers */
    lpcsdr_m4clock_set_freq(48000000);
}

void lpcsdr_m4clock_set_freq(uint32_t new_freq)
{
    /* force frequency to a multiple of 12MHz, so MAINPLL can stay in integer mode */
    new_freq = (new_freq + 11999999) / 12000000 * 12000000;

    if (new_freq < PLL_MIN_CCO_FREQ/16)
        new_freq = PLL_MIN_CCO_FREQ/16;
    if (new_freq > MAX_CLOCK_FREQ)
        new_freq = MAX_CLOCK_FREQ;

    if (current_freq == new_freq)
        return;
    current_freq = new_freq;

    Chip_SetupCoreClock(CLKIN_CRYSTAL, new_freq, false);
    SystemCoreClockUpdate();
    StopWatch_Init();
    lpcsdr_i2c_clock_update();

void lpcsdr_m4clock_status(ep0_in_board_status_t *status)
{
    status->m4_freq = current_freq;
}
