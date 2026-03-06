#ifndef MORSE_H_INCLUDED
#define MORSE_H_INCLUDED

/*
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

/* Return a two-letter hex pattern for value `x` */
static inline uint32_t morse_hexbyte(uint8_t x) {
    static const uint8_t morse_hexdigits[16] = {
            MORSE_0, MORSE_1, MORSE_2, MORSE_3, MORSE_4, MORSE_5, MORSE_6, MORSE_7,
            MORSE_8, MORSE_9, MORSE_A, MORSE_B, MORSE_C, MORSE_D, MORSE_E, MORSE_F
    };
    return (morse_hexdigits[x >> 4]) | (morse_hexdigits[x & 15] << 8);
}

#endif /* MORSE_H_INCLUDED */
