/*
 *  string.c - PG2 firmware, minimal mem{cpy,set,move}
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

/* Pulling in newlib (and its awkwardly giant chain of licenses) just
 * for the sake of mem{cpy,move,set} is overkill. Let's build our own
 * trivial versions here.
 */

#include <stdint.h>
#include <string.h>

/* copy n bytes from src to dest, source and destination regions do not overlap */
void *memcpy(void *dest, const void *src, size_t n)
{
    if (dest == src)
        return dest;

    uint8_t *dest8 = dest;
    const uint8_t *src8 = src;

    while (n--)
        *dest8++ = *src8++;

    return dest;
}

/* copy n bytes from src to dest, source and destination regions may overlap */
void *memmove(void *dest, const void *src, size_t n)
{
    if (dest == src)
        return dest;

    uint8_t *dest8 = dest;
    const uint8_t *src8 = src;

    if (dest <= src) {
        /* forward copy */
        while (n--)
            *dest8++ = *src8++;
    } else {
        /* reverse copy */
        dest8 += n;
        src8 += n;

        while (n--)
            *--dest8 = *--src8;
    }

    return dest;
}

/* set n bytes at s to value c */
void *memset(void *s, int c, size_t n)
{
    uint8_t *s8 = s;

    while (n--)
        *s8++ = c;

    return s;
}
