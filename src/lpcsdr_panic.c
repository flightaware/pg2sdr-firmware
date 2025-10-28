#include "lpc_types.h"
#include "chip.h"

#include "lpcsdr_uart.h"
#include "morse.h"

/* Reset codes (RESET_* in pg2sdr_protocol.h) are stored in the RTC "regfile" memory.
 * The RTC regfile persists over reset, but not over a full power cycle, so we can
 * use that to distinguish a power-on-reset (where RTC memory has a random value)
 * from other reset causes. The values have no particular meaning other than being
 * unique values that are unlikely to be randomly set.
 */

/* Power-on-reset. All unknown codes get mapped to this. */
#define RESET_POR 0
/* Unexpected reset without firmware intervention (watchdog timer or hard fault) */
#define RESET_UNEXPECTED 0x554EAAB1
/* Firmware was asked to reset itself */
#define RESET_FIRMWARE 0x4649B9B6
/* Firmware panic causing a reset, reset code stores the panic blink code */
#define RESET_PANIC 0x5041AFBE

/* set the LED state, minimal version to avoid risking double-faults
 * if there's something wrong in the GPIO code
 */
static void panic_leds(uint8_t onoff)
{
    LPC_GPIO_PORT->B[0][8] = onoff;
    LPC_GPIO_PORT->B[0][12] = onoff;
    LPC_GPIO_PORT->B[0][13] = onoff;
    LPC_GPIO_PORT->B[0][15] = onoff;
}

/* busy-wait a while */
static inline void panic_delay(uint32_t cycles)
{
    while (--cycles)
        __NOP();

    /* make sure to feed the watchdog periodically */
    LPC_WWDT->FEED = 0xAA;
    LPC_WWDT->FEED = 0x55;
}

void pg2sdr_hard_reset()
{
    LPC_RGU->RESET_CTRL[0] = 1; // CORE_RST=1, reset the whole chip
    while (true)
        __WFI();
}

void pg2sdr_panic(uint32_t pattern)
{
    /* record the source of the upcoming reset */
    LPC_REGFILE->REGFILE[0] = RESET_PANIC;
    LPC_REGFILE->REGFILE[1] = pattern;

    /* blinkenlights */
    const unsigned DIT = 3000000; /* delay cycles for a dit */
    for (uint32_t repeats = 0; repeats < 4; ++repeats) {
        for (uint32_t p = pattern; p; p >>= 8) {
            for (uint32_t letter = p & 255; letter > 1; letter >>= 1) {
                /* dit = on for 1 dit period, off for one dit period
                 * dah = on for 3 dit periods, off for one dit period
                 */
                panic_leds(1);
                panic_delay(letter & 1 ? DIT*3 : DIT*1);
                panic_leds(0);
                panic_delay(DIT);
            }
            panic_delay(DIT*2); // inter-letter spacing = 3 dit periods total
        }
        panic_delay(DIT*4);     // inter-word spacing = 7 dit periods total
    }

    pg2sdr_hard_reset();
}

void pg2sdr_reset()
{
    LPC_REGFILE->REGFILE[0] = RESET_FIRMWARE;
    LPC_REGFILE->REGFILE[1] = 0;
    pg2sdr_hard_reset();
}

/* Reason for the most recent reset (updated when pg2sdr_diagnose_reset is called,
 * early in startup)
 */
uint32_t pg2sdr_reset_reason = RESET_POR;
uint32_t pg2sdr_reset_code;

void pg2sdr_diagnose_reset()
{
    /* record current reason, update reason to UNEXPECTED
     * so that's what is left if we unexpectedly reset.
     *
     * For cases where we reset under the control of the
     * firmware, we'll update the reason to something other
     * than UNEXPECTED.
     */
    uint32_t r0 = LPC_REGFILE->REGFILE[0];
    uint32_t r1 = LPC_REGFILE->REGFILE[1];

    LPC_REGFILE->REGFILE[0] = RESET_UNEXPECTED;
    LPC_REGFILE->REGFILE[1] = 0;

    if ( (r0 == RESET_FIRMWARE && r1 == 0) ||
         (r0 == RESET_UNEXPECTED && r1 == 0) ||
         r0 == RESET_PANIC) {
        pg2sdr_reset_reason = r0;
        pg2sdr_reset_code = r1;
    } else {
        /* Power-on-reset leaves semi-random garbage in the
         * RTC regs, so assume that any values we don't recognize
         * are due to that.
         */
        pg2sdr_reset_reason = RESET_POR;
        pg2sdr_reset_code = 0;
    }

    debug_printf("Reset cause: ");
    switch (pg2sdr_reset_reason) {
    case RESET_FIRMWARE:
        debug_printf("user reset\r\n");
        break;
    case RESET_UNEXPECTED:
        debug_printf("watchdog or hard fault\r\n");
        break;
    case RESET_PANIC:
        debug_printf("firmware panic %08X\r\n", pg2sdr_reset_code);
        break;
    case RESET_POR:
        debug_printf("power-on reset\r\n");
        break;
    default:
        debug_printf("unhandled case?\r\n");
        break;
    }
}

void NMI_Handler(void)
{
    pg2sdr_panic(MORSE_N);
}
void HardFault_Handler(void)
{
    /* Hardfault means things have gone _really_ wrong. Just try to reset immediately. */
    pg2sdr_hard_reset();
}
void MemManage_Handler(void)
{
    pg2sdr_panic(MORSE_M);
}
void BusFault_Handler(void)
{
    pg2sdr_panic(MORSE_B);
}
void UsageFault_Handler(void)
{
    pg2sdr_panic(MORSE_U);
}
void SVC_Handler(void)
{
    pg2sdr_panic(MORSE_S);
}
void DebugMon_Handler(void)
{
    pg2sdr_panic(MORSE_D);
}
void PendSV_Handler(void)
{
    pg2sdr_panic(MORSE_P);
}

/* We can't directly override IntDefaultHandler because of how cr_startup does symbol aliasing;
 * instead, we do the minimum change necessary in cr_startup to have its IntDefaultHandler call `pg2sdr_unexpected_interrupt` instead
 */
void pg2sdr_unexpected_interrupt(void)
{
    uint32_t isr = __get_IPSR() & 255;                       // index of unexpected interrupt
    uint32_t pattern = MORSE_I | (morse_hexbyte(isr) << 8);  // blink 'I', then two hex digits with the unexpected interrupt index
    pg2sdr_panic(pattern);
}

void pg2sdr_assertion_failed(const char *file, unsigned line, const char *assertion)
{
    debug_printf("%s:%d: assertion failed: %s\r\n", file, line, assertion);
    pg2sdr_panic(MORSE_A);
}
