#ifndef LPCSDR_COMMON_H
#define LPCSDR_COMMON_H

#include "lpc_types.h"
#include "cmsis.h"

/* A compiler reordering barrier only */
__attribute__ ((always_inline)) static inline void reorder_barrier(void)
{
    __asm__ volatile ("" : : : "memory");
}

/* On a _single core_: ensure that all stores before `memory_barrier` occur before all
 * stores after `memory_barrier`. Not safe if you want the stores to be visible to other
 * cores sharing the memory -- use `shared_memory_barrier` for that.
 */
__attribute__ ((always_inline)) static inline void memory_barrier(void)
{
    /* TODO: Check if DMB is anything more than a no-op on Cortex-M */
    __asm__ volatile ("dmb" : : : "memory");
}

/* Force all stores before `shared_memory_barrier` to actually occur (flush write cache,
 * etc) before executing anything after `shared_memory_barrier`. This is what you want
 * for data that's shared between cores.
 */
__attribute__ ((always_inline)) static inline void shared_memory_barrier(void)
{
    __asm__ volatile ("dsb" : : : "memory");
}


/* Disable interrupts, return old PRIMASK value */
__attribute__ ((always_inline)) static inline uint32_t disable_interrupts(void)
{
    uint32_t old_mask;
    __asm__ volatile (
            "mrs %0, primask\n\t" /* save old interrupt mask to `result  */
            "cpsid i"             /* disable interrupts */
            : /* outputs */ "=r" (old_mask)
            : /* inputs */
            : /* clobber */ "memory");
    return old_mask;
}

/* Restore the PRIMASK mask returned from `disable_interrupts`, re-enabling interrupts
 * if they were previously enabled.
 */
__attribute__ ((always_inline)) static inline void enable_interrupts(uint32_t old_mask)
{
    __asm__ volatile (
            "msr primask, %0"   /* restore old interrupt mask */
            : /* outputs */
            : /* inputs */ "r" (old_mask)
            : /* clobber */ "memory");
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

/* Return `addr` aligned (rounding up) to a multiple of `alignment` */
__attribute__ ((always_inline)) static inline uint32_t align_to(uint32_t addr, uint32_t alignment)
{
    return (addr + alignment - 1) & ~(alignment - 1);
}

/* Atomically test and set one or more bits */
__attribute__ ((always_inline)) static inline uint32_t test_and_set_bits(uint32_t bits, volatile uint32_t *addr)
{
    uint32_t result;
    do {
        result = __LDREXW(addr);
    } while (__STREXW(result | bits, addr));

    return result;
}

/* Atomically test and clear one or more bits */
__attribute__ ((always_inline)) static inline uint32_t test_and_clear_bits(uint32_t bits, volatile uint32_t *addr)
{
    uint32_t result;
    do {
        result = __LDREXW(addr);
    } while (__STREXW(result & ~bits, addr));

    return result;
}

/* Align a type to the given alignment */
#define ALIGN(n) __attribute__(( aligned(n) ))

/* We don't have C11 headers but we do have a C11 compiler, so do this definition ourselves */
#define static_assert _Static_assert

#endif
