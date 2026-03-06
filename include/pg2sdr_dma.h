#ifndef PG2SDR_DMA_H
#define PG2SDR_DMA_H

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

#include "pg2sdr_common.h"
#include "pg2sdr_protocol.h"

/* Bounce buffers for ADCHS, in AHB SRAM */

/* constraints on this:
 *    packed sample data for one buffer has to fit into a USB buffer, leaving space for a 20-byte header (see pg2sdr_usb.h)
 *      (4 bytes in the HSADC buffer becomes 3 bytes in the USB buffer)
 *    HSADC_NUM_BUFFERS must be even (we allocate half the buffers in one AHB SRAM bank and half in the other bank, to reduce contention when
 *      the M4 main loop is reading a buffer and packing samples while the ADC DMA channel writes to the next buffer)
 *    HSADC_NUM_BUFFERS * HSADC_BUFFER_SIZE must be <= 64kB (we have two banks of 32kB AHB SRAM to allocate from)
 *    HSADC_BUFFER_SIZE must be a multiple of 4 (we do word-size transfers)
 */
#define HSADC_NUM_BUFFERS 4
#define HSADC_BUFFER_SIZE 13616    /* 6808 samples = 10212 bytes after packing */

/* base addresses for our buffers: */
#define AHB_SRAM_BANK_0 0x20000000
#define AHB_SRAM_BANK_1 0x20008000

/* transfer descriptors for the ADCHS DMA loop */
typedef struct ALIGN(16) _dma_lli {
    /* read by the DMA controller; these do not change as DMA transfers complete */
    uint32_t srcaddr;
    uint32_t destaddr;
    uint32_t lli;
    uint32_t control;

    /* additional status information for us; these do get updated by the DMA ISR & main loop */
    volatile uint32_t status;         /* status bits, combination of LLI_STATUS_*, updated by both ISR and main loop (careful of clobbering in main loop!) */
#define LLI_STATUS_COPYING   _BIT(0)  /* buffer is being processed by the M4 main loop */
#define LLI_STATUS_CLOBBERED _BIT(1)  /* buffer has started to be filled by DMA */
#define LLI_STATUS_DROPPED   _BIT(2)  /* buffer was given to the main loop, but the main loop had to drop it (e.g. no USB space) */
    volatile uint32_t sequence;       /* completion sequence number, updated by ISR */
} dma_lli_t;

/* initialize common DMA stuff */
void pg2sdr_dma_init(void);

/* start DMA from HSADC to internal DMA buffer space */
void pg2sdr_dma_hsadc_start(void);

/* stop DMA from HSADC to internal DMA buffer space */
void pg2sdr_dma_hsadc_stop(void);

/* callback to be implemented by main code, to indicate that DMA to a HSADC buffer has finished.
 * this code should return promptly, and asynchronously call `pg2sdr_dma_hsadc_copy_complete` when
 * it is done with the buffer.
 *
 * returns true if the buffer was accepted, false to reject the buffer (caller should clean up)
 */
bool pg2sdr_dma_hsadc_buffer_ready(dma_lli_t *completed, uint32_t status_flags);

/* For a buffer previously given to `pg2sdr_dma_hsadc_buffer_ready`,
 * indicate that the callback is done with the buffer.
 *
 * Returns the old buffer status.
 */
static inline uint32_t pg2sdr_dma_hsadc_copy_complete(dma_lli_t *buffer, bool completed)
{
    return test_set_clear_bits(/* set */ (completed ? 0 : LLI_STATUS_DROPPED), /* clear */ LLI_STATUS_COPYING, &buffer->status);
}

void pg2sdr_dma_status(ep0_in_board_status_t *status);

#endif
