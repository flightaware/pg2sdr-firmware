#ifndef PG2SDR_USB_H
#define PG2SDR_USB_H

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
#include "error.h"

/* Endpoint transfer descriptor and queue head structure; see UM10503 section 24.9 */

typedef volatile struct ALIGN(32) _USB_DTD_T {
    volatile uint32_t next_link_pointer_terminate;   /* address of next dTD in endpoint list, or end-of-dTD-list if TERMINATE is set */
#define DTD_TERMINATE _BIT(0)

    volatile uint32_t total_bytes_ioc_multo_status;
#define DTD_TOTAL_BYTES(x) ((x) << 16)     /* total bytes to transfer for this dTD */
#define DTD_IOC _BIT(15)                   /* Interrupt On Completion */
#define DTD_MULTO_0 0                      /* isochrononous endpoint stuff, not used */
#define DTD_MULTO_1 _BIT(10)
#define DTD_MULTO_2 _BIT(11)
#define DTD_MULTO_3 (_BIT(10) | _BIT(11))
#define DTD_STATUS_ACTIVE _BIT(7)          /* dTD is waiting to be processed */
#define DTD_STATUS_HALTED _BIT(6)          /* set on dTD transfer error */
#define DTD_STATUS_BUFERR _BIT(5)          /* set on dTD transfer error */
#define DTD_STATUS_TXNERR _BIT(3)          /* set on dTD transfer error */
#define DTD_STATUS_CANCEL _BIT(2)          /* h/w: reserved. s/w: this dTD is in use by the app layer, and a USB reset/reconfig happened asynchronously */
#define DTD_STATUS_BUSY _BIT(1)            /* h/w: reserved. s/w: this dTD is in use by the app layer */
#define DTD_STATUS_FREE _BIT(0)            /* h/w: reserved. s/w: this dTD is on the freelist */

    volatile uint32_t pages[5];            /* special meanings for page 0 and page 1 (see docs) */

    uint8_t *buffer;                       /* h/w: unused. s/w: pointer to associated buffer */
} USB_DTD_T;

typedef volatile struct ALIGN(64) {
    volatile uint32_t caps;
    volatile uint32_t current_dtd;
    volatile uint32_t o_next_link_pointer_terminate;
    volatile uint32_t o_total_bytes_ioc_multo_status;
    volatile uint32_t o_page0_curr_offs;
    volatile uint32_t o_page1_frame_n;
    volatile uint32_t o_page2;
    volatile uint32_t o_page3;
    volatile uint32_t o_page4;
    volatile uint32_t reserved;
    volatile uint32_t setup[2];
    volatile uint32_t pad[4];     /* pad to 64 bytes */
} USB_DQH_T;

/* Index into ENDPOINTLIST for a given endpoint */
#define EP_OUT_INDEX(x) ((x) * 2)
#define EP_IN_INDEX(x) ((x) * 2 + 1)

/* Prime bit in ENDPTPRIME / ENDPTSTATUS for a given endpoint */
#define EP_OUT_BIT(x) _BIT(x)
#define EP_IN_BIT(x) _BIT((x) + 16)

/* ADTDW bit in USBCMD */
#define USBCMD_ATDTW _BIT(14)


/* Number and size of USB buffers/dTDs to allocate.
 *
 * We have two pools of memory available to place the buffers:
 *  72kB in the first SRAM bank, above the firmware code (1000E000 - 1001FFFF)
 *  72kB in the second SRAM bank (10080000 - 10091FFF)
 *
 * Into that space we want to fit many USB buffers of the same size with some constraints:
 *  - buffer size must be a multiple of 512
 *    (larger buffer sizes are possible, but the buffer cannot span more than 5 4kB-aligned pages, which makes
 *     allocation trickier)
 *  - each buffer needs a header of 20 bytes, followed by sample data in multiples of 12 bytes, followed by unused padding space
 *
 * N=7, buffer: 10240 (20 * 512) bytes = 6808 samples + 28 bytes header, total: 71680, spare: 2048   <== use this
 * Out of the available options, we use this:
 *
 *   buffer size: 12288 bytes (= 24 * 512); 6 buffers exactly fits into 72kB
 *        header:    20 bytes
 *       samples: 12264 bytes = 8176 samples per buffer
 *           pad:     4 bytes
 *
 * 6 buffers of 12288 bytes each, totalling exactly 72kB
 */

#define DTDS_PER_POOL 6
#define DTD_BUFFER_SIZE 12288
#define NUM_DTDS (DTDS_PER_POOL * 2)

/* initialize the full USB stack */
void pg2sdr_usb_init(uint64_t serial_number);

/* Get a free dTD and associated buffer.
 * Returns a dTD, or NULL if none are available.
 * DTD_BUFFER_SIZE bytes of buffer space are available at `dtd->buffer`
 * The dTD should later be passed to either pg2sdr_usb_queue_dtd or pg2sdr_usb_free_dtd
 */
USB_DTD_T *pg2sdr_usb_get_dtd(void);

/* Put a dTD, previously allocated by pg2sdr_usb_get_dtd, on the queue to be sent via EP 1 IN to the host.
 * The first `length` bytes of the associated buffer will be sent.
 *
 * Returns true if it was successfully enqueued, false if something went wrong (concurrent reset, bad length)
 * Either way, the dTD is consumed by the USB stack and should not be used further by the caller.
 */
bool pg2sdr_usb_queue_dtd(USB_DTD_T *dtd, uint32_t length);

/* Release a dTD previously allocated by pg2sdr_usb_get_dtd without sending it */
void pg2sdr_usb_free_dtd(USB_DTD_T *dtd);

/* Callback, called to notify that the freelist was previously empty but now has a free dTD.
 *
 * i.e.
 *   call pg2sdr_usb_get_dtd(), returns NULL
 *   at some point later, pg2sdr_usb_space_available() is called by the USB code
 *   now pg2sdr_usb_get_dtd() will return non-NULL (at least once)
 *
 * Called with interrupts disabled from an ISR, don't do anything blocking.
 */
void pg2sdr_usb_space_available(void);

/* Disable EP1, clear pending transfers, return STALL to any further IN requests */
void pg2sdr_usb_ep1_disable(void);

/* Re-enable EP1, clear STALL state */
void pg2sdr_usb_ep1_enable(void);

/* Callback, called to notify that the USB connection/configuration state changed */
void pg2sdr_usb_state_changed(void);

/* Returns true if the USB layer is ready for use */
bool pg2sdr_usb_is_ready(void);

/*
 * Control endpoint handling. Most of the work is done from the main loop,
 * the ISR just arranges to fill the buffer (for OUT transfers) then passes an
 * IPC message with the details. The main loop then uses these functions to
 * complete the control transfer. This avoids keeping interrupts disabled for an
 * extended period for control transfers that take some time to complete.
 */

/* Shared control-transfer buffer */
extern uint8_t pg2sdr_usb_control_buffer[512];

/* Respond to an IN control transfer, providing some data */
void pg2sdr_usb_ep0_data_in(const uint8_t *buf, uint32_t length);

/* Respond to an OUT control transfer, successfully completing the transfer (STATUS stage) */
void pg2sdr_usb_ep0_out_ack();

/* Respond to an IN or OUT control transfer, stalling the endpoint to indicate an error */
void pg2sdr_usb_ep0_stall();

/* Fill the board status message with USB-related things */
void pg2sdr_usb_status(ep0_in_board_status_t *status);

/* Disconnect from USB bus */
void pg2sdr_usb_disconnect();

#endif /* PG2SDR_USB_H */
