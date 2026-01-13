#include "lpc_types.h"
#include "chip.h"

#include "pg2sdr_common.h"
#include "pg2sdr_protocol.h"
#include "pg2sdr_uart.h"
#include "morse.h"

/* Reset codes (RESET_* in pg2sdr_protocol.h) are stored in the RTC "regfile" memory.
 * The RTC regfile persists over reset, but not over a full power cycle, so we can
 * use that to distinguish a power-on-reset (where RTC memory has a random value)
 * from other reset causes. The values have no particular meaning other than being
 * unique values that are unlikely to be randomly set.
 */

/* set the LED state, minimal version to avoid risking double-faults
 * if there's something wrong in the GPIO code
 */
static void panic_leds(uint8_t onoff)
{
#ifdef HW_IS_PG2SDR
    LPC_GPIO_PORT->B[0][3] = onoff;
    LPC_GPIO_PORT->B[0][8] = onoff;
    LPC_GPIO_PORT->B[0][12] = onoff;
    LPC_GPIO_PORT->B[0][13] = onoff;
    LPC_GPIO_PORT->B[0][15] = onoff;
#endif

#ifdef HW_IS_AIRSPY
    LPC_GPIO_PORT->B[0][12] = onoff;
#endif
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
    shared_memory_barrier();
    LPC_RGU->RESET_CTRL[0] = 1; // CORE_RST=1, reset the whole chip
    while (true)
        __WFI();
}

void pg2sdr_panic(uint32_t pattern)
{
    static const char *hexdigits = "0123456789ABCDEF";

    disable_interrupts();

    /* record the source of the upcoming reset */
    LPC_REGFILE->REGFILE[0] = RESET_PANIC;
    LPC_REGFILE->REGFILE[1] = pattern;

    /* blinkenlights */
    const unsigned DIT = 3000000; /* delay cycles for a dit */
    /* small initial pause after turning the LEDs off */
    panic_leds(0);
    panic_delay(DIT);
    for (uint32_t repeats = 0; repeats < 4; ++repeats) {
        /* Blindly push some minimal data to the UART, maybe it'll be useful.
         * This fits in the 16-byte tx fifo, and we have a pause between
         * loops, so it should transmit okay.
         */
        LPC_USART0->THR = '!';
        LPC_USART0->THR = hexdigits[(pattern>>28) & 0xF];
        LPC_USART0->THR = hexdigits[(pattern>>24) & 0xF];
        LPC_USART0->THR = hexdigits[(pattern>>20) & 0xF];
        LPC_USART0->THR = hexdigits[(pattern>>16) & 0xF];
        LPC_USART0->THR = hexdigits[(pattern>>12) & 0xF];
        LPC_USART0->THR = hexdigits[(pattern>>8) & 0xF];
        LPC_USART0->THR = hexdigits[(pattern>>4) & 0xF];
        LPC_USART0->THR = hexdigits[(pattern>>0) & 0xF];
        LPC_USART0->THR = '\r';
        LPC_USART0->THR = '\n';
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

extern unsigned int resetisr_r0_value; /* in cr_startup_lpc43xx */

void pg2sdr_diagnose_reset()
{
    /* record current reason, update reason to UNEXPECTED
     * so that's what is left if we unexpectedly reset.
     *
     * For cases where we reset under the control of the
     * firmware, we'll update the reason to something other
     * than UNEXPECTED.
     */
    uint32_t rtc0 = LPC_REGFILE->REGFILE[0];
    uint32_t rtc1 = LPC_REGFILE->REGFILE[1];

    LPC_REGFILE->REGFILE[0] = RESET_UNEXPECTED;
    LPC_REGFILE->REGFILE[1] = 0;

    /* Firmware load-from-RAM via LOAD_IMAGE calls the ResetISR with
     * a specific value in r0 (RESET_LOAD). Use this to detect the
     * load-from-RAM case.
     *
     * Other reset paths will have the ROM bootloader call ResetISR,
     * with no particular value in r0. Any previously-running firmware
     * may have left something for us in the RTC regfile (rtc0/rtc1),
     * which persists across reset; use that to identify the type of reset.
     *
     * Power-on-reset leaves semi-random garbage in the
     * RTC regs, so assume that any rtc values we don't recognize
     * are due to that.
     */
    if (resetisr_r0_value == RESET_LOAD) {
        pg2sdr_reset_reason = RESET_LOAD;
        pg2sdr_reset_code = 0;
    } else if ((rtc0 == RESET_FIRMWARE && rtc1 == 0) ||
               (rtc0 == RESET_UNEXPECTED && rtc1 == 0) ||
               rtc0 == RESET_PANIC) {
        pg2sdr_reset_reason = rtc0;
        pg2sdr_reset_code = rtc1;
    } else {
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
    case RESET_LOAD:
        debug_printf("new firmware image was loaded\r\n");
        break;
    default:
        debug_printf("unhandled case?\r\n");
        break;
    }

    pg2sdr_uart_flush();
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
