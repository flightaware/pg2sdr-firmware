#include "lpcsdr_common.h"
#include "lpcsdr_usb.h"
#include "lpcsdr_gpio.h"
#include "lpcsdr_spifi.h"
#include "lpcsdr_hsadc.h"
#include "lpcsdr_dma.h"
#include "lpcsdr_ipc.h"
#include "lpcsdr_panic.h"

#include "chip.h"
#include "usbd_rom_api.h"

#include <string.h>

/* Use 72kB local SRAM at 1008 0000 .. 1009 1FFF for USB stack workspace and buffers */
#define USB_MEM_BASE   0x10080000
#define USB_MEM_SIZE   0x00012000

const USBD_API_T* g_pUsbApi;
static USBD_HANDLE_T usb_handle;

static bool ep1_enabled;

/*
 * USB descriptors, used during enumeration.
 */
static const ALIGNED(4) USB_DEVICE_DESCRIPTOR usb_device_desc = {
        .bLength = USB_DEVICE_DESC_SIZE,
        .bDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE,
        .bcdUSB = 0x0200,
        .bDeviceClass = 0,
        .bDeviceSubClass = 0,
        .bDeviceProtocol = 0,
        .bMaxPacketSize0 = USB_MAX_PACKET0,
        .idVendor = 0xDEAD, /* 0x1FC9, */
        .idProduct = 0xBEEF,
        .bcdDevice = 0x0100,
        .iManufacturer = 1,
        .iProduct = 2,
        .iSerialNumber = 3,
        .bNumConfigurations = 1,
};

PRE_PACK struct POST_PACK _HS_DESC {
    USB_CONFIGURATION_DESCRIPTOR config;
    USB_INTERFACE_DESCRIPTOR interface_1;
    USB_ENDPOINT_DESCRIPTOR interface_1_endpoint_1;
    uint8_t terminator;
};

static const ALIGNED(4) struct _HS_DESC usb_hs_config_desc = {
        .config = {
                .bLength = USB_CONFIGURATION_DESC_SIZE,
                .bDescriptorType = USB_CONFIGURATION_DESCRIPTOR_TYPE,
                .wTotalLength = (USB_CONFIGURATION_DESC_SIZE + USB_INTERFACE_DESC_SIZE * 1 + USB_ENDPOINT_DESC_SIZE * 1),
                .bNumInterfaces = 1,
                .bConfigurationValue = 1,
                .iConfiguration = 0,
                .bmAttributes = USB_CONFIG_BUS_POWERED,
                .bMaxPower = USB_CONFIG_POWER_MA(500)
        },

        .interface_1 = {
                .bLength = USB_INTERFACE_DESC_SIZE,
                .bDescriptorType = USB_INTERFACE_DESCRIPTOR_TYPE,
                .bInterfaceNumber = 0,
                .bAlternateSetting = 0,
                .bNumEndpoints = 1,
                .bInterfaceClass = 0xFF,
                .bInterfaceSubClass = 0,
                .bInterfaceProtocol = 0,
                .iInterface = 0
        },

        .interface_1_endpoint_1 = {
                .bLength = USB_ENDPOINT_DESC_SIZE,
                .bDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE,
                .bEndpointAddress = USB_ENDPOINT_IN(1),
                .bmAttributes = USB_ENDPOINT_TYPE_BULK,
                .wMaxPacketSize = 512,
                .bInterval = 0 /* check this (ch. 5) - NAK rate? */
        },

        .terminator = 0
};

PRE_PACK struct POST_PACK _FS_DESC {
    USB_CONFIGURATION_DESCRIPTOR config;
    USB_INTERFACE_DESCRIPTOR interface_1;
    /* No endpoints on the fullspeed interface */
    uint8_t terminator;
};

static const ALIGNED(4) struct _FS_DESC usb_fs_config_desc = {
        .config = {
                .bLength = USB_CONFIGURATION_DESC_SIZE,
                .bDescriptorType = USB_CONFIGURATION_DESCRIPTOR_TYPE,
                .wTotalLength = (USB_CONFIGURATION_DESC_SIZE + USB_INTERFACE_DESC_SIZE * 1 + USB_ENDPOINT_DESC_SIZE * 0),
                .bNumInterfaces = 1,
                .bConfigurationValue = 1,
                .iConfiguration = 0,
                .bmAttributes = USB_CONFIG_BUS_POWERED,
                .bMaxPower = USB_CONFIG_POWER_MA(500)
        },

        .interface_1 = {
                .bLength = USB_INTERFACE_DESC_SIZE,
                .bDescriptorType = USB_INTERFACE_DESCRIPTOR_TYPE,
                .bInterfaceNumber = 0,
                .bAlternateSetting = 0,
                .bNumEndpoints = 0,
                .bInterfaceClass = 0xFF,
                .bInterfaceSubClass = 0,
                .bInterfaceProtocol = 0,
                .iInterface = 0
        },

        .terminator = 0
};

static const ALIGNED(4) USB_DEVICE_QUALIFIER_DESCRIPTOR usb_device_qualifier_desc = {
        .bLength = USB_DEVICE_QUALI_SIZE,
        .bDescriptorType = USB_DEVICE_QUALIFIER_DESCRIPTOR_TYPE,
        .bcdUSB = 0x0200,
        .bDeviceClass = 0,
        .bDeviceSubClass = 0,
        .bDeviceProtocol = 0,
        .bMaxPacketSize0 = USB_MAX_PACKET0,
        .bNumConfigurations = 1,
        .bReserved = 0
};

/* string descriptors are hard to express as structs, since they're variable-length;
 * just encode them directly.
 *
 * not const as we will want to update the serial# later
 */
static ALIGNED(4) uint8_t usb_string_desc[] = {
        /* [0] = lang ID */
        2 + 2,                      /* bLength */
        USB_STRING_DESCRIPTOR_TYPE, /* bDescriptorType */
        WBVAL(0x0409),              /* en_US */

        /* [1] = manufacturer */
        2 + 2*11,                   /* bLength */
        USB_STRING_DESCRIPTOR_TYPE, /* bDescriptorType */
        'F', 0,
        'l', 0,
        'i', 0,
        'g', 0,
        'h', 0,
        't', 0,
        'A', 0,
        'w', 0,
        'a', 0,
        'r', 0,
        'e', 0,

        /* [2] = product */
        2 + 2*6,                    /* bLength */
        USB_STRING_DESCRIPTOR_TYPE, /* bDescriptorType */
        'L', 0,
        'P', 0,
        'C', 0,
        'S', 0,
        'D', 0,
        'R', 0,

        /* [3] = serial (nb: this is a placeholder value that is mutated later) */
        2 + 2*16,                    /* bLength */
        USB_STRING_DESCRIPTOR_TYPE, /* bDescriptorType */
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,
        '0', 0,

        0, /* zero-length terminator */
};

/* workaround for USBROM.2 errata */
static USB_EP_HANDLER_T errata_usbrom2_orig_handler;
static ErrorCode_t errata_usbrom2_ep0_handler(USBD_HANDLE_T hUsb, void *data, uint32_t event)
{
    static bool busy = false;

    switch (event) {
    case USB_EVT_OUT_NAK:
        if (busy) {
            // prevent nested NAK handling
            return LPC_OK;
        }

        busy = true;
        break;

    case USB_EVT_SETUP:
    case USB_EVT_OUT:
        busy = false;
        break;
    }

    return errata_usbrom2_orig_handler(hUsb, data, event);
}

static void errata_usbrom2_patch(USBD_HANDLE_T hUsb)
{
    USB_CORE_CTRL_T *pCtrl = (USB_CORE_CTRL_T *) hUsb;
    errata_usbrom2_orig_handler = pCtrl->ep_event_hdlr[0];
    pCtrl->ep_event_hdlr[0] = errata_usbrom2_ep0_handler;
}

/* cr_startup_lpc43xx.c (which defines the interrupt vector table) expects a function
 * with this name; if not provided, the default interrupt handler will halt the system
 * if a USB0 interrupt _does_ arrive.
 */
void USB0_IRQHandler(void)
{
    /* use the ROM API's interrupt handler */
    USBD_API->hw->ISR(usb_handle);
    ++pg2sdr_interrupts.usb0;
}

/* Pointers to the dTDs and corresponding data buffers */
static USB_DTD_T *usb_dtds[NUM_DTDS];

static volatile USB_DTD_T *dtd_active_head = NULL; /* active dTD list, head */
static volatile USB_DTD_T *dtd_active_tail = NULL; /* active dTD list, tail */
static volatile USB_DTD_T *dtd_free_head = NULL;   /* dTD freelist, head */

static void dtd_set_buffer(USB_DTD_T *dtd, void *user_buffer, uint32_t length)
{
    /* Assign dTD page values pointing to the pages of the user buffer.
     *
     * Buffer 0 can start at an arbitrary address (bits 11:0 may be non-zero). Buffer 1..4 must be page aligned (bits 11:0 must be zero)
     * so we allocate the user buffer to the buffer pointers like this:
     *
     *     === page boundary ===
     *     ... other data (user buffer doesn't need to start at the start of a page)
     *       user buffer starts here          <- buffer 0 pointer
     *     ... <= 4k data ...
     *     === page boundary ===              <- buffer 1 pointer
     *     ... 4k data
     *     === page boundary ===              <- buffer 2 pointer
     *     ... 4k data
     *     === page boundary ===              <- buffer 3 pointer
     *     ... 4k data
     *     === page boundary ===              <- buffer 4 pointer
     *     ... <= 4k data
     *       end of user buffer
     *     ...
     *     === page boundary ===
     * For shorter user buffers, the data just ends mid-page as determined by total_bytes,
     * and any subsequent buffer pointers are zeroed.
     */
    uint32_t buffer = (uint32_t) user_buffer;
    uint32_t end = buffer + length;
    for (unsigned i = 0; i < 5; ++i) {
        dtd->pages[i] = (buffer >= end) ? 0 : buffer;
        buffer = (buffer + 4096) & ~4095; /* advance to start of next page */
    }
}

/* extract the next pointer from a dTD, using the hardware's convention of DTD_TERMINATE meaning NULL */
static inline USB_DTD_T *dtd_get_next(USB_DTD_T *dtd)
{
    uint32_t link = dtd->next_link_pointer_terminate; // volatile, only read it once
    return (link & DTD_TERMINATE) ? NULL : (USB_DTD_T *)link;
}

/* set the next pointer of a dTD, using the hardware's convention of DTD_TERMINATE meaning NULL */
static inline void dtd_set_next(USB_DTD_T *dtd, USB_DTD_T *next)
{
    dtd->next_link_pointer_terminate = next ? (uint32_t)next : DTD_TERMINATE;
}

/* move all dTDs onto the freelist. For busy dTDs, mark them for cancellation. */
static void reset_dtd_lists_interrupts_disabled()
{
    dtd_active_head = dtd_active_tail = NULL;
    dtd_free_head = NULL;
    for (unsigned i = 0; i < NUM_DTDS; ++i) {
        USB_DTD_T *dtd = usb_dtds[i];
        if (dtd->total_bytes_ioc_multo_status == DTD_STATUS_BUSY)   /* Being filled by the main loop, can't free it yet.. */
            dtd->total_bytes_ioc_multo_status = DTD_STATUS_CANCEL;  /* .. so mark it for reclamation later */
        else {
            dtd->total_bytes_ioc_multo_status = DTD_STATUS_FREE;
            dtd_set_next(dtd, dtd_free_head);
            dtd_free_head = dtd;
        }
    }
}

/* remove a free dTD from the freelist and return it, or NULL if none are available */
USB_DTD_T *pg2sdr_usb_get_dtd()
{
    USB_DTD_T *head;
    WITH_DISABLED_INTERRUPTS {
        if (!pg2sdr_usb_is_ready()) {
            head = NULL;
        } else {
            head = dtd_free_head;
            if (head) {
                dtd_free_head = dtd_get_next(head);
                head->total_bytes_ioc_multo_status = DTD_STATUS_BUSY;
            }
        }
    }
    return head;
}

/* return a dTD to the freelist */
static void free_dtd_interrupts_disabled(USB_DTD_T *dtd)
{
    if (dtd->total_bytes_ioc_multo_status == DTD_STATUS_FREE)
        return; /* this is a bug if it happens, but at least avoid totally screwing up the freelist */

    bool was_empty = (dtd_free_head == NULL);
    dtd->total_bytes_ioc_multo_status = DTD_STATUS_FREE;
    dtd_set_next(dtd, dtd_free_head);
    dtd_free_head = dtd;

    if (was_empty) /* tell main loop when space becomes available */
        pg2sdr_usb_space_available();
}

void pg2sdr_usb_free_dtd(USB_DTD_T *dtd)
{
    WITH_DISABLED_INTERRUPTS {
        free_dtd_interrupts_disabled(dtd);
    }
}

/* schedule a dTD to be sent over USB */
static bool queue_dtd_interrupts_disabled(unsigned ep, USB_DTD_T *dtd, uint32_t bytes)
{
    /* caller should ensure interrupts are disabled */

    if (bytes > DTD_BUFFER_SIZE) {
        /* wat? */
        free_dtd_interrupts_disabled(dtd);
        return false;
    }

    if (dtd->total_bytes_ioc_multo_status == DTD_STATUS_CANCEL) {
        /* We had a USB reset or SetConfiguration while the main loop was busy working with
         * this dTD. It's not safe to return the dTD to the freelist immediately, so we
         * mark it with CANCEL and when the main loop eventually tries to send the dTD
         * we'll instead discard and free the dTD here.
         */
        free_dtd_interrupts_disabled(dtd);
        return false;
    }

    if (!pg2sdr_usb_is_ready() || !ep1_enabled) {
        /* EP1 not configured yet */
        free_dtd_interrupts_disabled(dtd);
        return false;
    }

    /* (re)prepare page pointers. These are clobbered when transmitted, so we need to re-set them each time
     * even though the buffer is unchanged
     */
    dtd_set_buffer(dtd, dtd->buffer, DTD_BUFFER_SIZE);

    /* append dTD to active list, fill in length */
    dtd->total_bytes_ioc_multo_status = DTD_TOTAL_BYTES(bytes) | DTD_STATUS_ACTIVE | DTD_IOC;
    dtd->next_link_pointer_terminate = DTD_TERMINATE;
    dtd_set_next(dtd, NULL);

    USB_DTD_T *old_tail = dtd_active_tail;
    if (old_tail)
        dtd_set_next(old_tail, dtd);
    dtd_active_tail = dtd;
    if (!dtd_active_head)
        dtd_active_head = dtd;

    /* work out if we need to re-prime the endpoint */
    const unsigned ep_bit = EP_IN_BIT(ep);

    if (old_tail) {
        // UM10503 24.10.11.3 - "linked list is not empty"

        if (LPC_USB0->ENDPTPRIME & ep_bit)  // Endpoint priming already requested, we are done
            return true;

        // set the tripwire bit, read transmit buffer status
        // if hardware clears the tripwire bit, we need to re-check the status
        bool etbr;
        do {
            LPC_USB0->USBCMD_D |= USBCMD_ATDTW;
            etbr = (LPC_USB0->ENDPTSTAT & ep_bit) != 0;
        } while (!(LPC_USB0->USBCMD_D & USBCMD_ATDTW));
        LPC_USB0->USBCMD_D &= ~USBCMD_ATDTW;

        if (etbr)   // hardware reports ndpoint transmit buffer ready, no need to re-prime
            return true;

        // Endpoint not ready and not priming, we need to prime it.
        // This happens when there is a race between
        //   * the USB hardware reaching the end of the old tail of the transmit list and halting the endpoint, and
        //   * software updating the next-link-pointer of the old tail to add the new dTD
    }

    // endpoint not primed, prime it to send the new dTD we added
    // UM10503 24.10.11.3 - "linked list is empty"
    USB_DQH_T *dqh_list = (USB_DQH_T *)LPC_USB0->ENDPOINTLISTADDR;
    USB_DQH_T *dqh = &dqh_list[EP_IN_INDEX(ep)];
    dqh->o_next_link_pointer_terminate = (uint32_t) dtd;
    dqh->o_total_bytes_ioc_multo_status &= ~(DTD_STATUS_ACTIVE | DTD_STATUS_HALTED);
    LPC_USB0->ENDPTPRIME |= ep_bit;

    return true;
}

bool pg2sdr_usb_queue_dtd(USB_DTD_T *dtd, uint32_t bytes)
{
    bool result;
    WITH_DISABLED_INTERRUPTS {
        result = queue_dtd_interrupts_disabled(1, dtd, bytes);
    }
    return result;
}

/* Flush pending data on EP1, set stall */
void pg2sdr_usb_ep1_disable(void)
{
    WITH_DISABLED_INTERRUPTS {
        ep1_enabled = false;
        USBD_API->hw->ResetEP(usb_handle, /* EP 1 IN */ 0x81);
        USBD_API->hw->SetStallEP(usb_handle, /* EP 1 IN */ 0x81);
        reset_dtd_lists_interrupts_disabled();
    }
}

/* (re-)enable EP1, clear stall  */
void pg2sdr_usb_ep1_enable(void)
{
    WITH_DISABLED_INTERRUPTS {
        USBD_API->hw->ClrStallEP(usb_handle, /* EP 1 IN */ 0x81);
        ep1_enabled = true;
    }
}

static void retire_completed_dtds()
{
    WITH_DISABLED_INTERRUPTS {
        while (dtd_active_head && (dtd_active_head->total_bytes_ioc_multo_status & DTD_STATUS_ACTIVE) == 0) {
            // head DTD is completed, remove it from the active list
            USB_DTD_T *old_head = dtd_active_head;
            dtd_active_head = dtd_get_next(old_head);
            if (!dtd_active_head)
                dtd_active_tail = NULL;
            free_dtd_interrupts_disabled(old_head);
        }
    }
}

/* Callback on USB reset */
static ErrorCode_t reset_handler(USBD_HANDLE_T handle)
{
    pg2sdr_usb_state_changed();
    pg2sdr_usb_ep1_disable();
    return LPC_OK;
}

/* Callback on USB configuration (after enumeration, when the host sets the configuration to use).
 * This is the point where we can start drawing >100mA
 */
static ErrorCode_t configure_handler(USBD_HANDLE_T handle)
{
    pg2sdr_usb_state_changed();
    pg2sdr_usb_ep1_disable();
    return LPC_OK;
}

/* EP1 event callback. We'll get a USB_EVT_IN whenever a dTD is completely sent */
static ErrorCode_t ep1_in_handler(USBD_HANDLE_T handle, void *data, uint32_t event)
{
    switch (event) {
    case USB_EVT_IN:
        retire_completed_dtds();
        return LPC_OK;

    default:
        return LPC_OK;
    }
}

/* Shared buffer for control transfer data */
uint8_t ALIGNED(4) pg2sdr_usb_control_buffer[512];

/* True if a control transfer is currently being processed by the main loop */
static bool ep0_busy;

/* True if we got a second control transfer while one was being processed by the main loop;
 * in this state we should not respond to the first transfer when the main loop eventually
 * comes up with a response.
 */
static bool ep0_clobber;

/* Disable NAK TX interrupt notification for EP0. I don't know why the USB ROM
 * enables this, but it causes interrupt storm problems if the host controller
 * is aggressive about polling for control transfers and we take a while to
 * handle the transfer.
 */
static void disable_ep0_nak_interrupt()
{
    LPC_USB0->ENDPTNAKEN &= ~(1<<16);
}

/* Called from the main loop to stall EP0 in response to a control transfer */
void pg2sdr_usb_ep0_stall()
{
    WITH_DISABLED_INTERRUPTS {
        if (ep0_clobber) {
            ep0_busy = ep0_clobber = false;
        } else if (ep0_busy) {
            ep0_busy = false;
            USBD_API->core->StallEp0(usb_handle);
        }
    }
}

/* Called from the main loop to provide EP0 IN data in response to a control transfer */
void pg2sdr_usb_ep0_data_in(const uint8_t *data, uint32_t length)
{
    USB_CORE_CTRL_T *ctrl = (USB_CORE_CTRL_T *) usb_handle;

    WITH_DISABLED_INTERRUPTS {
        if (ep0_clobber) {
            ep0_busy = ep0_clobber = false;
        } else if (ep0_busy) {
            ep0_busy = false;

            if (!length) {
                // Not sure what we do in the no-data-stage case here ..
                USBD_API->core->StatusOutStage(ctrl);
            } else {
                ctrl->EP0Data.pData = (uint8_t *)data;
                ctrl->EP0Data.Count = length;
                USBD_API->core->DataInStage(ctrl);
            }
        }
    }
}

/* Called from the main loop to complete an EP0 OUT control transfer successfully */
void pg2sdr_usb_ep0_out_ack(void)
{
    USB_CORE_CTRL_T *ctrl = (USB_CORE_CTRL_T *) usb_handle;

    WITH_DISABLED_INTERRUPTS {
        if (ep0_clobber) {
            ep0_busy = ep0_clobber = false;
        } else if (ep0_busy) {
            ep0_busy = false;
            USBD_API->core->StatusInStage(ctrl);
        }
    }
}

/* Handler for SETUP stage on EP0, for vendor requests only.
 */
static ErrorCode_t ep0_setup_handler(USBD_HANDLE_T handle)
{
    USB_CORE_CTRL_T *ctrl = (USB_CORE_CTRL_T *) handle;

    if (ep0_busy) {
        ep0_clobber = true;
        USBD_API->core->StallEp0(handle);
        return LPC_OK;
    }

    if (ctrl->SetupPacket.bmRequestType.BM.Dir == REQUEST_DEVICE_TO_HOST) {
        /* device->host, respond with IN data */
        disable_ep0_nak_interrupt();
        if (!pg2sdr_ipc_send_m4(M4_USB_EP0_IN,
                ctrl->SetupPacket.bRequest,
                (ctrl->SetupPacket.wValue.W | (ctrl->SetupPacket.wIndex.W << 16)),
                ctrl->SetupPacket.wLength)) {
            /* Couldn't queue it */
            USBD_API->core->StallEp0(handle);
            return LPC_OK;
        }

        ep0_busy = true;
        return LPC_OK;
    } else {
        /* host->device, prepare to read OUT data */
        if (ctrl->SetupPacket.wLength > sizeof(pg2sdr_usb_control_buffer)) {
            USBD_API->core->StallEp0(handle);
            return LPC_OK;
        }

        if (!ctrl->SetupPacket.wLength) {
            // No data phase, submit for processing immediately
            disable_ep0_nak_interrupt();
            if (!pg2sdr_ipc_send_m4(M4_USB_EP0_OUT,
                    ctrl->SetupPacket.bRequest,
                    (ctrl->SetupPacket.wValue.W | (ctrl->SetupPacket.wIndex.W << 16)),
                    ctrl->SetupPacket.wLength)) {
                /* Couldn't queue it */
                USBD_API->core->StallEp0(handle);
                return LPC_OK;
            }

            ep0_busy = true;
            return LPC_OK;
        }

        // Set up data phase, wait for data
        ctrl->EP0Data.pData = pg2sdr_usb_control_buffer;
        ctrl->EP0Data.Count = ctrl->SetupPacket.wLength;
        /* We will get a USB_EVT_OUT event later, when the data is ready */
        return LPC_OK;
    }
}

/* Handler called when we get USB_EVT_OUT on EP0, for vendor requests only.
 * This happens after the data stage is completed (data is received from the host) following a call to DataOutStage()
 */
static ErrorCode_t ep0_out_handler(USBD_HANDLE_T handle)
{
    USB_CORE_CTRL_T *ctrl = (USB_CORE_CTRL_T *) handle;

    if (ep0_clobber) {
        ep0_busy = ep0_clobber = false;
        USBD_API->core->StallEp0(handle);
        return LPC_OK;
    }

    disable_ep0_nak_interrupt();
    if (!pg2sdr_ipc_send_m4(M4_USB_EP0_OUT,
            ctrl->SetupPacket.bRequest,
            (ctrl->SetupPacket.wValue.W | (ctrl->SetupPacket.wIndex.W << 16)),
            ctrl->SetupPacket.wLength)) {
        /* Couldn't queue it */
        ep0_busy = ep0_clobber = false;
        USBD_API->core->StallEp0(handle);
        return LPC_OK;
    }

    ep0_busy = true;
    return LPC_OK;
}

/* EP0 event handler, just delegates events to a per-event-type handler */
static ErrorCode_t ep0_handler(USBD_HANDLE_T handle, void *data, uint32_t event)
{
    USB_CORE_CTRL_T *ctrl = (USB_CORE_CTRL_T *) handle;

    // Handle vendor requests only, everything else use default handling
    if (ctrl->SetupPacket.bmRequestType.BM.Type != REQUEST_VENDOR)
        return ERR_USBD_UNHANDLED;

    switch (event) {
    case USB_EVT_SETUP:
        return ep0_setup_handler(handle);

    case USB_EVT_OUT:
        return ep0_out_handler(handle);

    default:
        return ERR_USBD_UNHANDLED;
    }
}

/* returns true if we're ready to use USB (connected, configured, in highspeed mode) */
bool pg2sdr_usb_is_ready(void)
{
    USB_CORE_CTRL_T *core = (USB_CORE_CTRL_T*) usb_handle;
    return (core->config_value != 0 && core->device_speed == USB_HIGH_SPEED);
}

/* Fill in the USB-related bits of *status */
void pg2sdr_usb_status(ep0_in_board_status_t *status)
{
    WITH_DISABLED_INTERRUPTS {
        if (ep1_enabled)
            status->flags |= STATUS_EP1_ENABLED;

        status->usb_free_buffers = 0;
        for (USB_DTD_T *dtd = dtd_free_head; dtd; dtd = dtd_get_next(dtd))
            ++status->usb_free_buffers;

        status->usb_filled_buffers = 0;
        for (USB_DTD_T *dtd = dtd_active_head; dtd; dtd = dtd_get_next(dtd))
            ++status->usb_filled_buffers;
    }
}

/* Walk through descriptors and return a pointer to the start of the index'th descriptor */
static uint8_t *find_nth_descriptor(uint8_t *pDesc, unsigned index)
{
    while (index-- > 0)
        pDesc += pDesc[0];
    return pDesc;
}

/* write a 64-bit serial number to a USB string descriptor as hex */
static void write_serial(uint8_t *pDesc, uint64_t serial)
{
    static uint8_t hexdigits[16] = {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
    };

    unsigned bLength = pDesc[0]; /* don't exceed length of descriptor */
    for (int i = 15; i >= 0; --i) {
        unsigned nibble = serial & 0x0F;
        serial >>= 4;
        if (2 + i * 2 < bLength)
            pDesc[2 + i * 2] = hexdigits[nibble];
    }
}

ErrorCode_t pg2sdr_usb_init(uint64_t serial_number)
{
    static_assert(sizeof(USB_DTD_T) == 32, "wrong USB_DTD_T size");
    static_assert(sizeof(USB_DQH_T) == 64, "wrong USB_DQH_T size");

    /* enable clocks and USB PHY/pads */
    Chip_USB0_Init();

    /* initialize USB ROM stack */
    g_pUsbApi = (const USBD_API_T *) LPC_ROM_API->usbdApiBase;

    static USBD_API_INIT_PARAM_T usb_param = {
            .usb_reg_base = LPC_USB0_BASE,
            .mem_base = USB_MEM_BASE,
            .mem_size = USB_MEM_SIZE,
            .max_num_ep = USB_MAX_EP_NUM,
            .USB_Reset_Event = reset_handler,
            .USB_Configure_Event = configure_handler,
    };

    /* update serial number in string descriptor #3 */
    write_serial(find_nth_descriptor(usb_string_desc, 3), serial_number);

    static USB_CORE_DESCS_T desc = {
            .device_desc = (uint8_t *) &usb_device_desc,
            .string_desc = (uint8_t *) usb_string_desc,
            .high_speed_desc = (uint8_t *) &usb_hs_config_desc,
            .full_speed_desc = (uint8_t *) &usb_fs_config_desc,
            .device_qualifier = (uint8_t *) &usb_device_qualifier_desc

    };

    /* Allocate the space that the USB ROM API wants */
    usb_param.mem_size = USBD_API->hw->GetMemSize(&usb_param);

    /* todo: break this out into a reasonable allocator */
    uint32_t usb_pool = USB_MEM_BASE + usb_param.mem_size;
    uint32_t usb_pool_end = USB_MEM_BASE + USB_MEM_SIZE;

    /* Allocate space for transfer dTDs and their associated buffers */
    for (unsigned i = 0; i < NUM_DTDS; ++i) {
        usb_pool = ALIGN_TO(usb_pool, 32);     /* DTDs must be 32-byte aligned (address bits 4:0 are zero) */
        panic_assert(usb_pool + sizeof(USB_DTD_T) <= usb_pool_end);
        usb_dtds[i] = (USB_DTD_T*) usb_pool;
        memset((void*) usb_dtds[i], 0, sizeof(USB_DTD_T));
        usb_pool += sizeof(USB_DTD_T);

        usb_pool = ALIGN_TO(usb_pool, 32);     /* DTDs must be 32-byte aligned (address bits 4:0 are zero) */
        panic_assert(usb_pool + DTD_BUFFER_SIZE <= usb_pool_end);
        usb_dtds[i]->buffer = (uint8_t*) usb_pool;
        usb_pool += DTD_BUFFER_SIZE;
    }

    /* Put everything on the freelist */
    reset_dtd_lists_interrupts_disabled();

    /* Initialize the USB ROM API and patch errata */
    ErrorCode_t ret = USBD_API->hw->Init(&usb_handle, &desc, &usb_param);
    if (ret != LPC_OK)
        return ret;
    errata_usbrom2_patch(usb_handle);

    /* handler for setup packets received on EP0*/
    ret = USBD_API->core->RegisterClassHandler(usb_handle, ep0_handler, NULL);
    if (ret != LPC_OK)
        return ret;

    /* handler for EP1-IN events (specifically, notification when one or more dTDs are completed) */
    ret = USBD_API->core->RegisterEpHandler(usb_handle, EP_IN_INDEX(1), ep1_in_handler, NULL);
    if (ret != LPC_OK)
        return ret;

    /* enable USB interrupts, connect to the host to start enumeration */
    NVIC_EnableIRQ(USB0_IRQn);
    USBD_API->hw->Connect(usb_handle, 1);

    return LPC_OK;
}
