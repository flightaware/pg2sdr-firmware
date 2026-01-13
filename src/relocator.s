        @ This code is used from try_boot_image to replace the
        @ currently running firmware with a new copy.
        @
        @ Because this code will itself be relocated and
        @ executed at an address that's not the address it
        @ was linked at, it must be position-independent.

        .cpu cortex-m4
        .arch armv7m
        .syntax unified
        .thumb

        .text

        .p2align 2
        relocator_start = .
        .global relocator_start

        @ relocate(reset_arg,dest,src,len)
        @
        @ given a new firmware image at "src":
        @   copy the image from src->dest
        @     (probably overwriting the code that called us!)
        @   reset SP using the new image's stack-top value
        @   jump to the new image's reset ISR
        @
        @ r0 = arg to pass through to ResetISR
        @ r1 = dest
        @ r2 = src
        @ r3 = len (in words)
        .thumb_func
relocate:
        ldr sp, [r2]          @ load stack top from src[0]
        ldr r4, [r2, #4]      @ load reset ISR address from src[1]

1:                            @ copy src to dest, a word at a time
        ldr r5, [r2], #4      @  x = *src++
        str r5, [r1], #4      @  *dest++ = x
        subs r3, r3, #1       @  --len
        bne 1b                @  loop

        mov lr, #0xFFFFFFFF   @ make LR be the expected on-reset value
        bx r4                 @ branch to reset ISR, preserving r0 from our caller

		.p2align 2
        relocator_end = .
        .global relocator_end
