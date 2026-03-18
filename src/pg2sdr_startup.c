/*
 *  pg2sdr_startup.c - PG2 firmware, vector table and early init code
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

#include <stdint.h>
#include <string.h>

#include "pg2sdr_common.h"
#include "pg2sdr_isr.h"
#include "pg2sdr_panic.h"
#include "pg2sdr_hardware.h"
#include "pg2sdr_mem.h"

/* our firmware metadata */
extern struct firmware_metadata_s firmware_metadata;

/* in pg2sdr_main: */
int main(void);

/* ARM vector table, which must be located at the very start of the image.
 * The linker script puts the .isr_vector section at the very start of
 * the image; armVectorTable should be the only thing in that section.
 */
__attribute__((section(".isr_vector")))
uint32_t armVectorTable[] = {
        /* initial SP top-of-stack value */
        [0] = (uint32_t) &_vStackTop,

        /* internal exception sources */
        [1] = (uint32_t) &ResetISR,
        [2] = (uint32_t) &NMI_Handler,
        [3] = (uint32_t) &HardFault_Handler,
        [4] = (uint32_t) &MemManage_Handler,
        [5] = (uint32_t) &BusFault_Handler,
        [6] = (uint32_t) &UsageFault_Handler,
        [7] = 0,                               /* reserved for user code checksum (see UM10503 6.4.4.1) */
        [8] = (uint32_t) &firmware_metadata,   /* pointer to firmware metadata (so external tools can find it) */
        [9] = 0,                               /* reserved */
        [10] = 0,                              /* reserved */
        [11] = (uint32_t) &SVC_Handler,
        [12] = (uint32_t) &DebugMon_Handler,
        [13] = 0,                              /* reserved */
        [14] = (uint32_t) &PendSV_Handler,
        [15] = (uint32_t) &SysTick_Handler,

        /* external interrupt sources */
        [16] = (uint32_t) &pg2sdr_unexpected_interrupt, /* DAC */
        [17] = (uint32_t) &M0APP_IRQHandler           , /* M0APP */
        [18] = (uint32_t) &DMA_IRQHandler,              /* DMA */
        [19] = (uint32_t) &pg2sdr_unexpected_interrupt, /* reserved */
        [20] = (uint32_t) &pg2sdr_unexpected_interrupt, /* FLASHEEPROM */
        [21] = (uint32_t) &pg2sdr_unexpected_interrupt, /* ETHERNET */
        [22] = (uint32_t) &pg2sdr_unexpected_interrupt, /* SDIO */
        [23] = (uint32_t) &pg2sdr_unexpected_interrupt, /* LCD */
        [24] = (uint32_t) &USB0_IRQHandler,             /* USB0 */
        [25] = (uint32_t) &pg2sdr_unexpected_interrupt, /* USB1 */
        [26] = (uint32_t) &pg2sdr_unexpected_interrupt, /* SCT */
        [27] = (uint32_t) &RITIMER_IRQHandler,          /* RITIMER */
        [28] = (uint32_t) &pg2sdr_unexpected_interrupt, /* TIMER0 */
        [29] = (uint32_t) &pg2sdr_unexpected_interrupt, /* TIMER1 */
        [30] = (uint32_t) &pg2sdr_unexpected_interrupt, /* TIMER2 */
        [31] = (uint32_t) &pg2sdr_unexpected_interrupt, /* TIMER3 */
        [32] = (uint32_t) &pg2sdr_unexpected_interrupt, /* MCPWM */
        [33] = (uint32_t) &pg2sdr_unexpected_interrupt, /* ADC0 */
        [34] = (uint32_t) &pg2sdr_unexpected_interrupt, /* I2C0 */
        [35] = (uint32_t) &pg2sdr_unexpected_interrupt, /* I2C1 */
        [36] = (uint32_t) &pg2sdr_unexpected_interrupt, /* SPI */
        [37] = (uint32_t) &pg2sdr_unexpected_interrupt, /* ADC1 */
        [38] = (uint32_t) &pg2sdr_unexpected_interrupt, /* SSP0 */
        [39] = (uint32_t) &pg2sdr_unexpected_interrupt, /* SSP1 */
#ifdef HW_HAS_UART
        [40] = (uint32_t) &UART0_IRQHandler,            /* USART0 */
#else
        [40] = (uint32_t) &pg2sdr_unexpected_interrupt, /* USART0 */
#endif
        [41] = (uint32_t) &pg2sdr_unexpected_interrupt, /* UART1 */
        [42] = (uint32_t) &pg2sdr_unexpected_interrupt, /* USART2 */
        [43] = (uint32_t) &pg2sdr_unexpected_interrupt, /* USART3 */
        [44] = (uint32_t) &pg2sdr_unexpected_interrupt, /* I2S0 */
        [45] = (uint32_t) &pg2sdr_unexpected_interrupt, /* I2S1 */
        [46] = (uint32_t) &pg2sdr_unexpected_interrupt, /* SPIFI */
        [47] = (uint32_t) &pg2sdr_unexpected_interrupt, /* SGPIO */
        [48] = (uint32_t) &pg2sdr_unexpected_interrupt, /* PIN_INT0 */
        [49] = (uint32_t) &pg2sdr_unexpected_interrupt, /* PIN_INT1 */
        [50] = (uint32_t) &pg2sdr_unexpected_interrupt, /* PIN_INT2 */
        [51] = (uint32_t) &pg2sdr_unexpected_interrupt, /* PIN_INT3 */
        [52] = (uint32_t) &pg2sdr_unexpected_interrupt, /* PIN_INT4 */
        [53] = (uint32_t) &pg2sdr_unexpected_interrupt, /* PIN_INT5 */
        [54] = (uint32_t) &pg2sdr_unexpected_interrupt, /* PIN_INT6 */
        [55] = (uint32_t) &pg2sdr_unexpected_interrupt, /* PIN_INT7 */
        [56] = (uint32_t) &pg2sdr_unexpected_interrupt, /* GINT0 */
        [57] = (uint32_t) &pg2sdr_unexpected_interrupt, /* GINT1 */
        [58] = (uint32_t) &pg2sdr_unexpected_interrupt, /* EVENTROUTER */
        [59] = (uint32_t) &pg2sdr_unexpected_interrupt, /* C_CAN1 */
        [60] = (uint32_t) &pg2sdr_unexpected_interrupt, /* reserved */
        [61] = (uint32_t) &pg2sdr_unexpected_interrupt, /* ADCHS */
        [62] = (uint32_t) &pg2sdr_unexpected_interrupt, /* ATIMER */
        [63] = (uint32_t) &pg2sdr_unexpected_interrupt, /* RTC */
        [64] = (uint32_t) &pg2sdr_unexpected_interrupt, /* reserved */
        [65] = (uint32_t) &WDT_IRQHandler,              /* WWDT */
        [66] = (uint32_t) &pg2sdr_unexpected_interrupt, /* M0SUB */
        [67] = (uint32_t) &pg2sdr_unexpected_interrupt, /* C_CAN0 */
        [68] = (uint32_t) &pg2sdr_unexpected_interrupt, /* QEI */
};

uint32_t resetisr_r0_value; /* remember the r0 value passed to ResetISR here */

extern uint32_t __bss_section_table;     /* start of BSS table generated by linker script */
extern uint32_t __bss_section_table_end; /* end of BSS table generated by linker script */

/* Reset ISR, the main entry point of the entire firmware.
 * For the load-from-RAM case, we pass a special value in r0, which maps to
 * the first C parameter.
 */
void ResetISR(uint32_t r0)
{
    /* reset happens with interrupts enabled.
     * we're about to mess with a lot of interrupt sources, so
     * defensively disable interrupts while we do that
     */
    __disable_irq();

    /* Reset as much of the MCU as we dare.
     *
     * Notable omission: there's no way to reset the WWDT once armed.
     * So better hope we initialize fast enough, if this is not a
     * hardware reset!
     */
    LPC_RGU->RESET_CTRL[0] =
            (1 << RGU_M0SUB_RST) |
            (1 << RGU_LCD_RST) |
            (1 << RGU_USB0_RST) |
            (1 << RGU_USB1_RST) |
            (1 << RGU_DMA_RST) |
            (1 << RGU_SDIO_RST) |
            (1 << RGU_EMC_RST) |
            (1 << RGU_ETHERNET_RST) |
            (1 << RGU_FLASHA_RST) |
            (1 << RGU_EEPROM_RST) |
            (1 << RGU_FLASHB_RST);
    LPC_RGU->RESET_CTRL[1] =
            (1 << (RGU_TIMER0_RST-32)) |
            (1 << (RGU_TIMER1_RST-32)) |
            (1 << (RGU_TIMER2_RST-32)) |
            (1 << (RGU_TIMER3_RST-32)) |
            (1 << (RGU_RITIMER_RST-32)) |
            (1 << (RGU_SCT_RST-32)) |
            (1 << (RGU_MOTOCONPWM_RST-32)) |
            (1 << (RGU_QEI_RST-32)) |
            (1 << (RGU_ADC0_RST-32)) |
            (1 << (RGU_ADC1_RST-32)) |
            (1 << (RGU_DAC_RST-32)) |
            (1 << (RGU_UART0_RST-32)) |
            (1 << (RGU_UART1_RST-32)) |
            (1 << (RGU_UART2_RST-32)) |
            (1 << (RGU_UART3_RST-32)) |
            (1 << (RGU_I2C0_RST-32)) |
            (1 << (RGU_I2C1_RST-32)) |
            (1 << (RGU_SSP0_RST-32)) |
            (1 << (RGU_SSP1_RST-32)) |
            (1 << (RGU_I2S_RST-32)) |
            (1 << (RGU_SPIFI_RST-32)) |
            (1 << (RGU_CAN1_RST-32)) |
            (1 << (RGU_CAN0_RST-32)) |
            (1 << (RGU_M0APP_RST-32)) |
            (1 << (RGU_SGPIO_RST-32)) |
            (1 << (RGU_SPI_RST-32)) |
            (1 << (RGU_ADCHS_RST-32));

    /* disable all NVIC interrupt sources and SysTick */
    for (unsigned i = 0; i < 8; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFF;   /* NVIC ICER0-7, clear-enable all interrupts */
        NVIC->ICPR[i] = 0xFFFFFFFF;   /* NVIC ICPR0-7, clear-pending all interrupts */
    }
    SysTick->CTRL = 0;

    /* ensure VTOR points directly to our vector table, rather than an aliased address
     * via M4MEMMAP
     */
    SCB->VTOR = (uint32_t) &armVectorTable;

    /* we're done messing with interrupt sources now */
    __enable_irq();

    /* turn on the FPU */
    fpuInit();

    /* The linker script constructs a table of (address of section, size of section)
     * between __bss_section_table and __bss_section_table_end containing all the
     * BSS sections (i.e. data that should be zero-initialized). We're responsible
     * for walking that table and zeroing each section.
     */
    const uint32_t *bss_section = &__bss_section_table;
    while (bss_section < &__bss_section_table_end) {
        uint32_t bss_start = *bss_section++;
        uint32_t bss_size = *bss_section++;
        memset((void*)bss_start, 0, bss_size);
    }

    /* no need to relocate/copy the data sections, we never run directly from flash */

    memory_barrier(); /* extra paranoia since we're messing with globals behind the compiler's back */

    /* remember our original r0 value. This must happen _after_ BSS initialization,
     * or else we'll just clobber the value!
     */
    resetisr_r0_value = r0;

    /* now all the basics are set up, run main() to kick everything off */
    main();
}
