#ifndef LPCSDR_COMMON_H
#define LPCSDR_COMMON_H

#include "lpc_types.h"
#include "cmsis.h"

/* Disable interrupts, return old PRIMASK value */
__attribute__ ((always_inline)) static inline uint32_t disable_interrupts(void)
{
    uint32_t result = __get_PRIMASK();
    __disable_irq();
    return result;
}

/* Restore the PRIMASK mask returned from `disable_interrupts`, re-enabling interrupts
 * if they were previously enabled.
 */
__attribute__ ((always_inline)) static inline void enable_interrupts(uint32_t old_mask)
{
    __set_PRIMASK(old_mask);
}

/* control-structure-like macro for running a block of code with interrupts disabled, and restoring the old state afterwards:
 *
 * WITH_DISABLED_INTERRUPTS {
 *    // do something here
 * }
 *
 * Don't exit the block via control-flow instructions (return/break/goto)!
 */
#define WITH_DISABLED_INTERRUPTS for (uint32_t __once = 1, __save = disable_interrupts(); __once; enable_interrupts(__save), __once = 0)

/* CMSIS __DMB is insufficient to prevent compiler reordering, so add a memory clobber.. */
__attribute__ ((always_inline)) static inline void memory_barrier(void)
{
    __asm__ volatile ("dmb" : : : "memory");
}

/* Return `addr` aligned (rounding up) to a multiple of `alignment` */
__attribute__ ((always_inline)) static inline uint32_t align_to(uint32_t addr, uint32_t alignment)
{
    return (addr + alignment - 1) & ~(alignment - 1);
}

/* Align a type to the given alignment */
#define ALIGN(n) __attribute__(( aligned(n) ))

/* We don't have C11 headers but we do have a C11 compiler, so do this definition ourselves */
#define static_assert _Static_assert

#endif
