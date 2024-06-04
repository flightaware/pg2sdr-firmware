/*
 * @brief Common SystemInit function for LPC18xx/LPC43xx chips
 *
 * @note
 * Copyright 2013-2014, 2019, 2020 NXP
 * All rights reserved.
 *
 * @par
 * NXP Confidential. This software is owned or controlled by NXP and may only be
 * used strictly in accordance with the applicable license terms.
 * 
 * By expressly accepting such terms or by downloading, installing, activating
 * and/or otherwise using the software, you are agreeing that you have read, and
 * that you agree to comply with and are bound by, such license terms.
 * 
 * If you do not agree to be bound by the applicable license terms, then you may not
 * retain, install, activate or otherwise use the software.
 */

 #include "chip.h"

/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

/* Clock/crystal frequencies, required by the chip library */
const uint32_t ExtRateIn = 0;              /* external clock signal (unused on the LPCSDR) */
const uint32_t OscRateIn = 12000000;       /* external crystal frequency (Y1, 12MHz) */

/*****************************************************************************
 * Private functions
 ****************************************************************************/




/*****************************************************************************
 * Public functions
 ****************************************************************************/

/* Set up and initialize hardware prior to call to main */
void SystemInit(void)
{
#if defined(CORE_M4)
    /* set ARM Vector Table Offset Register to point to our vector table */
    extern void *g_pfnVectors;
    unsigned int *VTOR = (unsigned int *) 0xE000ED08;
    *VTOR = (unsigned int) &g_pfnVectors;

    /* do early FPU init */
    fpuInit(); /* in chip library */

    /* derive core clock from external crystal, set chip-default base clocks */
    Chip_SetupCoreClock(CLKIN_CRYSTAL, MAX_CLOCK_FREQ, true);

    /* Reset and enable 32Khz oscillator */
    LPC_CREG->CREG0 &= ~((1 << 3) | (1 << 2));
    LPC_CREG->CREG0 |= (1 << 1) | (1 << 0);
#endif /* defined(CORE_M4) */
}
