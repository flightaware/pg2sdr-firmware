#ifndef LPCSDR_COMMON_H
#define LPCSDR_COMMON_H

#include "lpc_types.h"

__attribute__ ((always_inline)) static inline uint32_t disable_interrupts(void)
{
    /* disable interrupts; return the old PRIMASK bit */
    uint32_t result;
    __asm__ volatile (
            "mrs %0, primask\n\t"
            "cpsid i\n\t"
            : /* output */  "=r" (result)
            : /* input */
            : /* clobber */ "memory" );
    return result;
}

__attribute__ ((always_inline)) static inline void enable_interrupts(uint32_t old_mask)
{
    /* re-enable interrupts by restoring the old primask value saved by disable_interrupts */
    __asm__ volatile (
            "msr primask, %0"
            : /* output */
            : /* input */   "r" (old_mask)
            : /* clobber */ "memory");
}

#define WITH_DISABLED_INTERRUPTS for (uint32_t __once = 1, __save = disable_interrupts(); __once; enable_interrupts(__save), __once = 0)

__attribute__ ((always_inline)) static inline uint32_t align_to(uint32_t addr, uint32_t alignment)
{
    return (addr + alignment - 1) & ~(alignment - 1);
}


#endif
