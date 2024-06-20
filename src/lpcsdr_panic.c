#include "lpc_types.h"

#include "chip.h"

/* Overrides of default interrupt/exception handlers to provide some LED feedback when called,
 * then reset the system
 */

__attribute__((always_inline)) static inline void panic_leds(uint8_t onoff)
{
    LPC_GPIO_PORT->B[0][8] = onoff;
    LPC_GPIO_PORT->B[0][12] = onoff;
    LPC_GPIO_PORT->B[0][13] = onoff;
    LPC_GPIO_PORT->B[0][15] = onoff;
}

__attribute__((always_inline)) static inline void panic_delay(uint32_t cycles)
{
    while (--cycles)
        __NOP();
}


/* These patterns represent short Morse code words, one letter per byte, one
 * word (1-4 letters) per uint32_t.
 *
 * Bytes are shown starting with the least-significant byte. Zero bytes are
 * skipped.
 *
 * Within each byte, bits are read starting from the least-significant bit. A 0 bit
 * is a dit (short blink), a 1 bit is a dah (long blink). The most-significant 1 bit
 * indicates the end of the letter and does not produce another blink.
 *
 * e.g.  00010111 = dah, dah, dah, dit, (end of letter)
 */

#define PATTERN1(d1) ((d1) | (1<<1))
#define PATTERN2(d1,d2) ((d1) | (d2<<1) | (1<<2))
#define PATTERN3(d1,d2,d3) ((d1) | (d2<<1) | (d3<<2) | (1<<3))
#define PATTERN4(d1,d2,d3,d4) ((d1) | (d2<<1) | (d3<<2) | (d4<<3) | (1<<4))
#define PATTERN5(d1,d2,d3,d4,d5) ((d1) | (d2<<1) | (d3<<2) | (d4<<3) | (d5<<4) | (1<<5))

#define MORSE_A PATTERN2(0,1)     // .-
#define MORSE_B PATTERN4(1,0,0,0) // -...
#define MORSE_C PATTERN4(1,0,1,0) // -.-.
#define MORSE_D PATTERN3(1,0,0)   // -..
#define MORSE_E PATTERN1(0)       // .
#define MORSE_F PATTERN4(0,0,1,0) // ..-.
#define MORSE_G PATTERN3(1,1,0)   // --.
#define MORSE_H PATTERN4(0,0,0,0) // ....
#define MORSE_I PATTERN2(0,0)     // ..
#define MORSE_J PATTERN4(0,1,1,1) // .---
#define MORSE_K PATTERN3(1,0,1)   // -.-
#define MORSE_M PATTERN2(1,1)     // --
#define MORSE_N PATTERN2(1,0)     // -.
#define MORSE_O PATTERN3(1,1,1)   // ---
#define MORSE_P PATTERN4(0,1,1,0) // .--.
#define MORSE_Q PATTERN4(1,1,0,1) // --.-
#define MORSE_R PATTERN3(0,1,0)   // .-.
#define MORSE_S PATTERN3(0,0,0)   // ...
#define MORSE_T PATTERN1(1)       // -
#define MORSE_U PATTERN3(0,0,1)   // ..-
#define MORSE_V PATTERN4(0,0,0,1) // ...-
#define MORSE_W PATTERN3(0,1,1)   // .--
#define MORSE_X PATTERN4(1,0,0,1) // -..-
#define MORSE_Y PATTERN4(1,0,1,1) // -.--
#define MORSE_Z PATTERN4(1,1,0,0) // --..

#define MORSE_0 PATTERN5(1,1,1,1,1)  // -----
#define MORSE_1 PATTERN5(0,1,1,1,1)  // .----
#define MORSE_2 PATTERN5(0,0,1,1,1)  // ..---
#define MORSE_3 PATTERN5(0,0,0,1,1)  // ...--
#define MORSE_4 PATTERN5(0,0,0,0,1)  // ....-
#define MORSE_5 PATTERN5(0,0,0,0,0)  // .....
#define MORSE_6 PATTERN5(1,0,0,0,0)  // -....
#define MORSE_7 PATTERN5(1,1,0,0,0)  // --...
#define MORSE_8 PATTERN5(1,1,1,0,0)  // ---..
#define MORSE_9 PATTERN5(1,1,1,1,0)  // ----.

// Return a two-letter hex pattern for value `x`
static inline uint32_t morse_hexbyte(uint8_t x) {
    static const uint8_t morse_hexdigits[16] = {
            MORSE_0, MORSE_1, MORSE_2, MORSE_3, MORSE_4, MORSE_5, MORSE_6, MORSE_7,
            MORSE_8, MORSE_9, MORSE_A, MORSE_B, MORSE_C, MORSE_D, MORSE_E, MORSE_F
    };
    return (morse_hexdigits[x >> 4]) | (morse_hexdigits[x & 15] << 8);
}

// Blink LEDs according to `pattern`, repeat 10 times, reset system.
static void panic_blink(unsigned pattern)
{
    const unsigned DIT = 3000000; // delay cycles for a dit
    while (1) {
        for (uint32_t repeats = 0; repeats < 10; ++repeats) {
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

        LPC_RGU->RESET_CTRL[0] = 1; // CORE_RST=1, reset the whole chip
        panic_delay(DIT);           // wait for the reset to take effect, shouldn't return
    }
}

void NMI_Handler(void)
{
    panic_blink(MORSE_N);
}
void HardFault_Handler(void)
{
    panic_blink(MORSE_H);
}
void MemManage_Handler(void)
{
    panic_blink(MORSE_M);
}
void BusFault_Handler(void)
{
    panic_blink(MORSE_B);
}
void UsageFault_Handler(void)
{
    panic_blink(MORSE_U);
}
void SVC_Handler(void)
{
    panic_blink(MORSE_S);
}
void DebugMon_Handler(void)
{
    panic_blink(MORSE_D);
}
void PendSV_Handler(void)
{
    panic_blink(MORSE_P);
}
void SysTick_Handler(void)
{
    panic_blink(MORSE_T);
}

/* We can't directly override IntDefaultHandler because of how cr_startup does symbol aliasing;
 * instead, we do the minimum change necessary in cr_startup to have its IntDefaultHandler call `unexpected_interrupt` instead
 */
void unexpected_interrupt(void)
{
    uint32_t isr = __get_IPSR() & 255;                       // index of unexpected interrupt
    uint32_t pattern = MORSE_I | (morse_hexbyte(isr) << 8);  // blink 'I', then two hex digits with the unexpected interrupt index
    panic_blink(pattern);
}
