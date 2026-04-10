/*
 *  pg2sdr_m4clock.c - PG2 firmware, SysTick and M4 core clock setup
 *
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

#include "pg2sdr_m4clock.h"
#include "pg2sdr_tuner.h"
#include "pg2sdr_led.h"
#include "pg2sdr_isr.h"

#include "chip.h"
#include "stopwatch.h"

/* M4 clock management & load stats */

/* allowed M4 clock range, Hz */
#define MIN_M4_FREQ 24000000      /* 12MHz seems to tickle some USB race conditions, don't go that slow */
#define MAX_M4_FREQ 204000000

/* assume the bootloader starts our code with the M4 clock at 96MHz */
#define M4_INIT_FREQ 96000000
static uint32_t m4_current_freq = M4_INIT_FREQ;      /* current M4 frequency, Hz */

/* number of M4 clock cycles per systick interrupt */
#define SYSTICK_INTERVAL (MIN_M4_FREQ/4)

/* number of systick interrupts per measurement period */
static uint32_t systick_max_count;

/* number of systick interrupts processed so far in the current measurement period */
static uint32_t systick_count;

/* total idle CPU cycles in last & current measurement period */
static volatile uint32_t idle_cycles_last;
static uint32_t idle_cycles;
static volatile uint32_t idle_cycles_accumulator; /* updated by pg2sdr_m4clock_wfi() */

/* min idle CPU cycles in last & current measurement period */
static volatile uint32_t min_idle_cycles_last;
static uint32_t min_idle_cycles;

/* lpcopen requires these symbols: */
const uint32_t ExtRateIn = 0;              /* external clock signal (unused on the PG2SDR) */
const uint32_t OscRateIn = 12000000;       /* external crystal frequency (Y1, 12MHz) */

static void m4clock_changed(uint32_t new_freq);

void pg2sdr_m4clock_init()
{
    /* switch the M4 clock to use the crystal immediately */
    Chip_SetupCoreClock(CLKIN_CRYSTAL, MIN_M4_FREQ, false);
    Chip_Clock_SetBaseClock(CLK_BASE_APB1, CLKIN_MAINPLL, true, false);
    Chip_Clock_SetBaseClock(CLK_BASE_APB3, CLKIN_MAINPLL, true, false);

    /* reset internal state, initialize timers */
    m4clock_changed(MIN_M4_FREQ);

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

    ++pg2sdr_interrupts.systick;
}

void pg2sdr_m4clock_set_freq(uint32_t new_freq)
{
    if (new_freq < MIN_M4_FREQ)
        new_freq = MIN_M4_FREQ;
    if (new_freq > MAX_M4_FREQ)
        new_freq = MAX_M4_FREQ;

    /* force frequency to a multiple of 12MHz, so MAINPLL can stay in integer mode */
    new_freq = (new_freq + 11999999) / 12000000 * 12000000;

    if (new_freq == m4_current_freq) {
      /* nothing to do */
      return;
    }

    Chip_SetupCoreClock(CLKIN_CRYSTAL, new_freq, false);
    m4clock_changed(new_freq);
}

static void m4clock_changed(uint32_t new_freq)
{
    m4_current_freq = new_freq;
    SystemCoreClockUpdate();
    StopWatch_Init();
    pg2sdr_tuner_clock_update();
    pg2sdr_led_clock_update();

    WITH_DISABLED_INTERRUPTS {
        systick_count = 0;
        systick_max_count = m4_current_freq / SYSTICK_INTERVAL;  /* update once a second */
        idle_cycles_last = idle_cycles = 0;
        min_idle_cycles_last = min_idle_cycles = SYSTICK_INTERVAL;
        SysTick->VAL = 0;                   /* restart SysTick */
    }
}

void pg2sdr_m4clock_status(ep0_in_board_status_t *status)
{
    status->m4_freq = m4_current_freq;
    status->m4_mean_idle = idle_cycles_last;
    status->m4_mean_idle_scale = SYSTICK_INTERVAL * systick_max_count;
    status->m4_min_idle = min_idle_cycles_last;
    status->m4_min_idle_scale = SYSTICK_INTERVAL;
}

void pg2sdr_m4clock_wfi()
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
