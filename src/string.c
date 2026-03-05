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
