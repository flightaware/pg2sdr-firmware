#include "lpcsdr_m4clock.h"
#include "lpcsdr_tuner.h"

#include "chip.h"
#include "stopwatch.h"

/* M4 clock management & load stats */

/* allowed M4 clock range, Hz */
#define MIN_M4_FREQ 24000000      /* 12MHz seems to tickle some USB race conditions, don't go that slow */
#define MAX_M4_FREQ 204000000

/* current M4 frequency, Hz */
static uint32_t current_freq;

/* number of M4 clock cycles per systick interrupt */
#define SYSTICK_INTERVAL (MIN_M4_FREQ/4)

/* number of systick interrupts per measurement period */
static uint32_t systick_max_count;

/* number of systick interrupts processed so far in the current measurement period */
static uint32_t systick_count;

/* total idle CPU cycles in last & current measurement period */
static volatile uint32_t idle_cycles_last;
static uint32_t idle_cycles;
static volatile uint32_t idle_cycles_accumulator; /* updated by lpcsdr_m4clock_wfi() */

/* min idle CPU cycles in last & current measurement period */
static volatile uint32_t min_idle_cycles_last;
static uint32_t min_idle_cycles;

void lpcsdr_m4clock_init()
{
    /* switch the M4 clock to use the crystal immediately */
    Chip_SetupCoreClock(CLKIN_CRYSTAL, MIN_M4_FREQ, false);
    Chip_Clock_SetBaseClock(CLK_BASE_APB1, CLKIN_MAINPLL, true, false);
    Chip_Clock_SetBaseClock(CLK_BASE_APB3, CLKIN_MAINPLL, true, false);

    /* reset internal state, initialize timers */
    lpcsdr_m4clock_set_freq(current_freq, true);

    /* start systick */
    SysTick->CTRL = SysTick_CTRL_ENABLE_Msk | SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk;   /* use processor clock source, do generate interrupts */
    SysTick->LOAD = SYSTICK_INTERVAL - 1;
}

void SysTick_Handler(void)
{
    if (idle_cycles_accumulator < min_idle_cycles)
        min_idle_cycles = idle_cycles_accumulator;

    idle_cycles += idle_cycles_accumulator;
    idle_cycles_accumulator = 0;

    if (++systick_count >= systick_max_count) {
        /* end of this measurement period, update last values from current values, reset current values */
        systick_count = 0;
        idle_cycles_last = idle_cycles;
        min_idle_cycles_last = min_idle_cycles;
        idle_cycles = 0;
        min_idle_cycles = SYSTICK_INTERVAL;
    }
}

void lpcsdr_m4clock_set_freq(uint32_t new_freq, bool first_time_init)
{
    if (new_freq < MIN_M4_FREQ)
        new_freq = MIN_M4_FREQ;
    if (new_freq > MAX_M4_FREQ)
        new_freq = MAX_M4_FREQ;

    /* force frequency to a multiple of 12MHz, so MAINPLL can stay in integer mode */
    new_freq = (new_freq + 11999999) / 12000000 * 12000000;

    if (new_freq == current_freq && !first_time_init) {
      /* nothing to do */
      return;
    }

    WITH_DISABLED_INTERRUPTS {
        /* Temporary fix for interrupt storm causing START_TRANSFER to timeout:
         * disable interrupts entirely while we reconfigure the CPU clock, so
         * the delay loops in Chip_SetupCoreClock are not greatly lengthened by
         * handling a lot of USB NAK interrupts
         */
        if (!first_time_init) {
            Chip_SetupCoreClock(CLKIN_CRYSTAL, new_freq, false);
        }

        current_freq = new_freq;
        SystemCoreClockUpdate();
        StopWatch_Init();
        lpcsdr_tuner_clock_update();

        /* this is the only bit that actually _needs_ interrupts to be disabled */
        systick_count = 0;
        systick_max_count = current_freq / SYSTICK_INTERVAL;  /* update once a second */
        idle_cycles_last = idle_cycles = 0;
        min_idle_cycles_last = min_idle_cycles = SYSTICK_INTERVAL;
        SysTick->VAL = 0;                   /* restart SysTick */
    }
}

void lpcsdr_m4clock_status(ep0_in_board_status_t *status)
{
    status->m4_freq = current_freq;
    status->m4_mean_idle = idle_cycles_last;
    status->m4_mean_idle_scale = SYSTICK_INTERVAL * systick_max_count;
    status->m4_min_idle = min_idle_cycles_last;
    status->m4_min_idle_scale = SYSTICK_INTERVAL;
}

void lpcsdr_m4clock_wfi()
{
    /* called with interrupts disabled! */
    uint32_t start_systick = SysTick->VAL;
    __DSB(); /* v7-M architecture requirement, but not strictly necessary on M0/M4 */
    __WFI();
    uint32_t end_systick = SysTick->VAL;

    if (start_systick > end_systick)
        idle_cycles_accumulator = idle_cycles_accumulator + start_systick - end_systick;
    else
        idle_cycles_accumulator = idle_cycles_accumulator + start_systick + SYSTICK_INTERVAL - end_systick;
}
