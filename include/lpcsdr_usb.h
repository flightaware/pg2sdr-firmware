#ifndef LPCSDR_USB_H
#define LPCSDR_USB_H

#include "lpcsdr_common.h"
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
 * We will allocate the buffers in the 72kB local SRAM starting at 10080000;
 * we also need some of that SRAM for the dTD structures and the ROM USB memory requirements
 *
 * say N buffers, each a multiple of 512 bytes, with at least 20 bytes of header space, fitting into 70kB (leaving 2kB for other USB stack space)
 *
 * N=5, buffer: 14336 (28 * 512) bytes = 9544 samples + 20 bytes header, total: 71680, spare: 2048
 * N=6, buffer: 11776 (23 * 512) bytes = 6376 samples + 28 bytes header, total: 70656, spare: 3072
 * N=7, buffer: 10240 (20 * 512) bytes = 6808 samples + 28 bytes header, total: 71680, spare: 2048   <== use this
 * N=8, buffer: 8704  (17 * 512) bytes = 5784 samples + 28 bytes header, total: 69632, spare: 4096
 */
#define NUM_DTDS 7
#define DTD_BUFFER_SIZE 10240

/* Set up USB PHY and PLL0USB clock. May be called multiple times, idempotent */
void lpcsdr_usb_clock_init(void);

/* initialize the full USB stack. Returns LPC_OK if all is OK. */
ErrorCode_t lpcsdr_usb_init(void);

/* Get a free dTD and associated buffer.
 * Returns a dTD, or NULL if none are available.
 * DTD_BUFFER_SIZE bytes of buffer space are available at `dtd->buffer`
 * The dTD should later be passed to either lpcsdr_usb_queue_dtd or lpcsdr_usb_free_dtd
 */
USB_DTD_T *lpcsdr_usb_get_dtd(void);

/* Put a dTD, previously allocated by lpcsdr_usb_get_dtd, on the queue to be sent via EP 1 IN to the host.
 * The first `length` bytes of the associated buffer will be sent.
 *
 * Returns true if it was successfully enqueued, false if something went wrong (concurrent reset, bad length)
 * Either way, the dTD is consumed by the USB stack and should not be used further by the caller.
 */
bool lpcsdr_usb_queue_dtd(USB_DTD_T *dtd, uint32_t length);

/* Release a dTD previously allocated by lpcsdr_usb_get_dtd without sending it */
void lpcsdr_usb_free_dtd(USB_DTD_T *dtd);

/* Callback, called to notify that the freelist was previously empty but now has a free dTD.
 *
 * i.e.
 *   call lpcsdr_usb_get_dtd(), returns NULL
 *   at some point later, lpcsdr_usb_space_available() is called by the USB code
 *   now lpcsdr_usb_get_dtd() will return non-NULL (at least once)
 *
 * Called with interrupts disabled from an ISR, don't do anything blocking.
 */
void lpcsdr_usb_space_available(void);

/* Callback, called to notify that the USB connection/configuration state changed */
void lpcsdr_usb_state_changed(void);

/* Returns true if the USB layer is ready for use */
bool lpcsdr_usb_is_ready(void);

#endif /* LPCSDR_USB_H */
