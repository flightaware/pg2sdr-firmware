@ relocator.s - PG2 firmware, helper for relocating loaded firmware
@
@  Copyright (c) 2026 FlightAware All rights reserved.
@
@ Redistribution and use in source and binary forms, with or without
@ modification, are permitted provided that the following conditions are
@ met:
@
@ 1. Redistributions of source code must retain the above copyright
@ notice, this list of conditions and the following disclaimer.
@
@ 2. Redistributions in binary form must reproduce the above copyright
@ notice, this list of conditions and the following disclaimer in the
@ documentation and/or other materials provided with the distribution.
@
@ THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
@ "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
@ LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
@ A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
@ HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
@ SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
@ LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
@ DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
@ THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
@ (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
@ OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
