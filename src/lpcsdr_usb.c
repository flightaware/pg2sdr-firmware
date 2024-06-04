#include "lpcsdr_usb.h"
#include "lpcsdr_gpio.h"
#include "lpcsdr_spifi.h"

#include "chip.h"
#include "usbd_rom_api.h"

#include <string.h>

#define static_assert _Static_assert

/* Use 32kB AHB SRAM at 2000 0000 .. 2000 7FFF for USB buffers */
#define USB_MEM_BASE   0x20000000
#define USB_MEM_SIZE   0x00008000

const USBD_API_T* g_pUsbApi;
static USBD_HANDLE_T usb_handle;

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

		/* [3] = serial (later: mutate this) */
		2 + 2*8,                    /* bLength */
		USB_STRING_DESCRIPTOR_TYPE, /* bDescriptorType */
		'0', 0,
		'0', 0,
		'0', 0,
		'0', 0,
		'0', 0,
		'0', 0,
		'0', 0,
		'0', 0,
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
}

/* Endpoint transfer descriptor and queue head structure; see UM10503 section 24.9 */

typedef volatile struct ALIGNED(32) {
	volatile uint32_t next_link_pointer_terminate;
#define DTD_TERMINATE _BIT(0)

	volatile uint32_t total_bytes_ioc_multo_status;
#define DTD_TOTAL_BYTES(x) ((x) << 16)
#define DTD_IOC _BIT(15)
#define DTD_MULTO_0 0
#define DTD_MULTO_1 _BIT(10)
#define DTD_MULTO_2 _BIT(11)
#define DTD_MULTO_3 (_BIT(10) | _BIT(11))
#define DTD_STATUS_ACTIVE _BIT(7)
#define DTD_STATUS_HALTED _BIT(6)
#define DTD_STATUS_BUFERR _BIT(5)
#define DTD_STATUS_TXNERR _BIT(4)

	volatile uint32_t page0_curr_offs;
	volatile uint32_t page1_frame_n;
	volatile uint32_t page2;
	volatile uint32_t page3;
	volatile uint32_t page4;
	volatile uint32_t pad;         /* pad to 32 bytes */
} USB_DTD_T;

typedef volatile struct ALIGNED(64) {
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

/* Pointers to the dTDs (and corresponding data buffers) that we will allocate in a ring */
#define NUM_DTDS 4
#define DTD_BUFFER_SIZE 4096
USB_DTD_T *usb_dtds[NUM_DTDS];
uint8_t *usb_buffers[NUM_DTDS];

/* Index into ENDPOINTLIST for a given endpoint */
#define EP_OUT_INDEX(x) ((x) * 2)
#define EP_IN_INDEX(x) ((x) * 2 + 1)

/* Prime bit in ENDPTPRIME for a given endpoint */
#define EP_OUT_BIT(x) _BIT(x)
#define EP_IN_BIT(x) _BIT((x) + 16)

volatile unsigned next_dtd_index = 0;

/* Callback on USB reset. Nothing much to do here. */
static ErrorCode_t reset_handler(USBD_HANDLE_T handle)
{
	return LPC_OK;
}

/* Callback on USB configuration (after enumeration, when the host sets the configuration to use).
 * This is the point where we could power things up and start drawing more than 100mA.
 */
static ErrorCode_t configure_handler(USBD_HANDLE_T handle)
{
	/* Set up a loop of dTDs for endpoint 1 IN that continuously send data
	 * whenever the host requests it.
	 */

	const unsigned index = 3; // EP_IN_INDEX(1);
	const uint32_t prime_bit = 1<<17; // EP_IN_BIT(1);

	/* reset all dTDs */
	for (unsigned i = 0; i < NUM_DTDS; ++i) {
		usb_dtds[next_dtd_index]->total_bytes_ioc_multo_status = DTD_TOTAL_BYTES(DTD_BUFFER_SIZE) | DTD_STATUS_ACTIVE | DTD_IOC;
	}
	next_dtd_index = 0;

	/* update the hardware queue head (assumes no pending transfer!). From UM10503:
	 *
	 * 1. Write dQH next pointer AND dQH terminate bit to 0 as a single DWord operation.
	 * 2. Clear active and halt bits in dQH (in case set from a previous error).
	 * 3. Prime endpoint by writing ‘1’ to correct bit position in ENDPTPRIME.
	 */

	USB_DQH_T *dQH = (USB_DQH_T *) LPC_USB0->ENDPOINTLISTADDR;
	dQH[index].o_next_link_pointer_terminate = (uint32_t) usb_dtds[0];                     /* set next pointer, with terminate bit = 0 */
	dQH[index].o_total_bytes_ioc_multo_status &= ~(DTD_STATUS_ACTIVE | DTD_STATUS_HALTED); /* clear active/halted status bits */
	LPC_USB0->ENDPTPRIME |= prime_bit;                                                     /* prime endpoint */

	lpcsdr_led_set(true);

	return LPC_OK;
}

/* EP1 event callback. We'll get a USB_EVT_IN whenever a dTD is completely sent; since we're just sending
 * in a loop, we just keep the DTD in the loop and make it active again.
 */
static ErrorCode_t ep1_in_handler(USBD_HANDLE_T handle, void *data, uint32_t event)
{
	/* toggle LED every 1k interrupts */
	static unsigned counter;
	if (++counter == 1000) {
		counter = 0;
		lpcsdr_led_toggle();
	}

	switch (event) {
	case USB_EVT_IN:
		/* Walk the list for completed dTDs, and make them active again */
		while (!(usb_dtds[next_dtd_index]->total_bytes_ioc_multo_status & DTD_STATUS_ACTIVE)) {
			usb_dtds[next_dtd_index]->total_bytes_ioc_multo_status = DTD_TOTAL_BYTES(DTD_BUFFER_SIZE) | DTD_STATUS_ACTIVE | DTD_IOC;
			next_dtd_index = (next_dtd_index + 1) % NUM_DTDS;
		}
		return LPC_OK;

	default:
		return LPC_OK;
	}
}

/* a little linear congruential PRNG, just to get some randomness in the USB data we transfer */
static uint32_t random_state = 123456789;
static void random_fill(uint8_t *buffer, unsigned size)
{
	for (unsigned i = 0; i < size; ++i) {
		random_state = random_state * 0xD9F5 + 1;
		*buffer++ = (uint8_t) (random_state >> 24);
	}
}

/* helper: while processing a setup request, respond with an endpoint stall */
static ErrorCode_t ep0_stall(USBD_HANDLE_T handle)
{
	USBD_API->core->StallEp0(handle);
	return LPC_OK;
}

/* helper: while processing a setup request (device to host), respond to the request with some data */
static ErrorCode_t ep0_data_in(USBD_HANDLE_T handle, uint8_t *data, uint32_t length)
{
	USB_CORE_CTRL_T *ctrl = (USB_CORE_CTRL_T *) handle;

	if (ctrl->SetupPacket.bmRequestType.BM.Dir != REQUEST_DEVICE_TO_HOST || ctrl->SetupPacket.wLength > length)
		return ep0_stall(handle);

	ctrl->EP0Data.pData = data;
	ctrl->EP0Data.Count = ctrl->SetupPacket.wLength;
	USBD_API->core->DataInStage(ctrl);
	return LPC_OK;
}

/* helper: while processing a setup request (host to device), set up to receive data from the host */
static ErrorCode_t ep0_prepare_data_out(USBD_HANDLE_T handle, uint8_t *data, uint32_t length)
{
	USB_CORE_CTRL_T *ctrl = (USB_CORE_CTRL_T *) handle;

	if (ctrl->SetupPacket.bmRequestType.BM.Dir != REQUEST_HOST_TO_DEVICE || ctrl->SetupPacket.wLength > length)
		return ep0_stall(handle);

	ctrl->EP0Data.pData = data;
	ctrl->EP0Data.Count = ctrl->SetupPacket.wLength;
	USBD_API->core->DataOutStage(ctrl); /* We will get a USB_EVT_OUT event later, when the data is ready */
	return LPC_OK;
}

/* Page buffer holding data SPI control transfers (the built-in EP0 buffer is only 64 bytes, so we need a separate buffer for this) */
static uint8_t spi_buffer[256];

/* Handler for SETUP stage on EP0, for vendor requests only.
 * Return USBD_ERR_UNHANDLED to get default behavior (probably a stall)
 */
static ErrorCode_t ep0_setup_handler(USBD_HANDLE_T handle)
{
	USB_CORE_CTRL_T *ctrl = (USB_CORE_CTRL_T *) handle;

	if (ctrl->SetupPacket.bmRequestType.BM.Dir == REQUEST_DEVICE_TO_HOST) {
		/* device->host, respond with IN data */

		switch (ctrl->SetupPacket.bRequest) {
		case 0x01:
			/* comms check */
			ctrl->EP0Buf[0] = 0xDE;
			ctrl->EP0Buf[1] = 0xAD;
			ctrl->EP0Buf[2] = 0xBE;
			ctrl->EP0Buf[3] = 0xEF;
			return ep0_data_in(handle, ctrl->EP0Buf, 4);

		case 0x02:
			/* SPI: read manufacturer/device ID */
			lpcsdr_spifi_read_manufacturer_device_id(ctrl->EP0Buf);
			return ep0_data_in(handle, ctrl->EP0Buf, 2);

		case 0x03:
			/* SPI: read unique ID */
			lpcsdr_spifi_read_unique_id(ctrl->EP0Buf);
			return ep0_data_in(handle, ctrl->EP0Buf, 8);

		case 0x04: {
			/* SPI: read data */
			if (ctrl->SetupPacket.wLength > sizeof(spi_buffer))
				return ep0_stall(handle);

			uint32_t address = (ctrl->SetupPacket.wIndex.W << 16) | ctrl->SetupPacket.wValue.W;
			if (address > 0x00FFFFFF || address + ctrl->SetupPacket.wLength > 0x01000000)
				return ep0_stall(handle);

			lpcsdr_spifi_read_data(address, spi_buffer, ctrl->SetupPacket.wLength);
			return ep0_data_in(handle, spi_buffer, ctrl->SetupPacket.wLength);
		}

		case 0x05: {
			/* SPI: read data, quad */
			if (ctrl->SetupPacket.wLength > sizeof(spi_buffer))
				return ep0_stall(handle);

			uint32_t address = (ctrl->SetupPacket.wIndex.W << 16) | ctrl->SetupPacket.wValue.W;
			if (address > 0x00FFFFFF || address + ctrl->SetupPacket.wLength > 0x01000000)
				return ep0_stall(handle);

			lpcsdr_spifi_fast_read_quad(address, spi_buffer, ctrl->SetupPacket.wLength);
			return ep0_data_in(handle, spi_buffer, ctrl->SetupPacket.wLength);
		}

		default:
			return ERR_USBD_UNHANDLED;
		}
	} else {
		/* host->device, prepare to read OUT data */
		switch (ctrl->SetupPacket.bRequest) {
		case 0x01:
			/* comms check */
			return ep0_prepare_data_out(handle, ctrl->EP0Buf, sizeof(ctrl->EP0Buf));

		case 0x10: {
			/* SPI: write data */
			return ep0_prepare_data_out(handle, spi_buffer, sizeof(spi_buffer));
		}

		case 0x11: {
			/* SPI: erase sector. We can do this immediately as there's no additional data to receive */
			uint32_t address = (ctrl->SetupPacket.wIndex.W << 16) | ctrl->SetupPacket.wValue.W;
			if (address > 0x00FFFFFF || (address & 0x0FFF) != 0)
				return ep0_stall(handle);

			ErrorCode_t spi_error = lpcsdr_spifi_sector_erase(address); /* up to 300ms */
			if (spi_error != LPC_OK)
				return ep0_stall(handle);

			USBD_API->core->StatusInStage(handle);
			return LPC_OK;
		}

		default:
			return ERR_USBD_UNHANDLED;
		}
	}
}

/* Handler called when we get USB_EVT_OUT on EP0.
 * This happens after the data stage is completed (data is received from the host) following a call to ep0_prepare_data_out()
 */
static ErrorCode_t ep0_out_handler(USBD_HANDLE_T handle)
{
	USB_CORE_CTRL_T *ctrl = (USB_CORE_CTRL_T *) handle;

	/* nb ctrl->EP0Data isn't preserved from the setup phase, but EP0Buf and SetupPacket apparently are .. */

	switch (ctrl->SetupPacket.bRequest) {
	case 0x01:
		/* comms check */
		if (ctrl->SetupPacket.wLength != 4 ||
			ctrl->EP0Buf[0] != 0xDE ||
			ctrl->EP0Buf[1] != 0xAD ||
			ctrl->EP0Buf[2] != 0xBE ||
			ctrl->EP0Buf[3] != 0xEF)
			return ep0_stall(handle);

		USBD_API->core->StatusInStage(handle);
		return LPC_OK;

	case 0x10:
		/* SPI: write data */
		uint32_t address = (ctrl->SetupPacket.wIndex.W << 16) | ctrl->SetupPacket.wValue.W;
		if (address > 0x00FFFFFF || address + ctrl->SetupPacket.wLength > 0x01000000)
			return ep0_stall(handle);

		ErrorCode_t spi_error = lpcsdr_spifi_page_program(address, spi_buffer, ctrl->SetupPacket.wLength); /* up to 3ms */
		if (spi_error != LPC_OK)
			return ep0_stall(handle);

		USBD_API->core->StatusInStage(handle);
		return LPC_OK;

	default:
		return ERR_USBD_UNHANDLED;
	}
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

/* Set up the USB PLL and PHY. Can be called multiple times, only does
 * something the first time. This exists so we can get USB0PLL programmed
 * (for use by SPIFI) before needing to fully set up USB.
 */
void lpcsdr_usb_clock_init(void)
{
	static bool once = false;

	if (once)
		return;
	once = true;

	/* enable clocks and USB PHY/pads */
	Chip_USB0_Init();
}

ErrorCode_t lpcsdr_usb_init(void)
{
	static_assert(sizeof(USB_DTD_T) == 32, "wrong USB_DTD_T size");
	static_assert(sizeof(USB_DQH_T) == 64, "wrong USB_DQH_T size");

	lpcsdr_usb_clock_init();

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

#define ALIGN_TO(x,y) ( ((x)+(y)-1) & ~((y)-1) )
#define CHECK(x) do {} while (!(x))

	/* Allocate space for transfer DTDs */
	for (unsigned i = 0; i < NUM_DTDS; ++i) {
		usb_pool = ALIGN_TO(usb_pool, 32);     /* DTDs must be 32-byte aligned (address bits 4:0 are zero) */
		CHECK((usb_pool & 31) == 0);

		usb_dtds[i] = (USB_DTD_T*) usb_pool;
		usb_pool += sizeof(USB_DTD_T);
	}

	/* Allocate space for the transfer buffers */
	for (unsigned i = 0; i < NUM_DTDS; ++i) {
		usb_pool = ALIGN_TO(usb_pool, 4);      /* word-align buffers */
		usb_buffers[i] = (uint8_t*) usb_pool;
		random_fill(usb_buffers[i], DTD_BUFFER_SIZE);
		usb_pool += DTD_BUFFER_SIZE;
	}

	/* Populate the DTD loop */
	for (unsigned i = 0; i < NUM_DTDS; ++i) {
		USB_DTD_T *dtd = usb_dtds[i];

		dtd->next_link_pointer_terminate = (uint32_t) usb_dtds[(i + 1) % NUM_DTDS];
		dtd->total_bytes_ioc_multo_status = DTD_TOTAL_BYTES(DTD_BUFFER_SIZE) | DTD_STATUS_ACTIVE | DTD_IOC;

		/* Assign buffer pointers pointing to the pages of the user buffer.
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
		 *
		 * For shorter user buffers, the data just ends mid-page as determined by total_bytes,
		 * and any subsequent buffer pointers are zeroed.
		 */
		uint32_t buffer = (uint32_t) usb_buffers[i];
		uint32_t remaining = DTD_BUFFER_SIZE;

		dtd->page0_curr_offs = buffer; /* buffer0 has the start of the buffer, until the next page boundary */
		dtd->page1_frame_n = dtd->page2 = dtd->page3 = dtd->page4 = 0;

		uint32_t size = 4096 - (buffer & 4095);
		buffer += size;
		remaining -= size;
		if (remaining > 0) {
			dtd->page1_frame_n = buffer;
			size = (remaining > 4096 ? 4096 : remaining);
			buffer += size;
			remaining -= size;
		}
		if (remaining > 0) {
			dtd->page2 = buffer;
			size = (remaining > 4096 ? 4096 : remaining);
			buffer += size;
			remaining -= size;
		}
		if (remaining > 0) {
			dtd->page3 = buffer;
			size = (remaining > 4096 ? 4096 : remaining);
			buffer += size;
			remaining -= size;
		}
		if (remaining > 0) {
			dtd->page4 = buffer;
			size = (remaining > 4096 ? 4096 : remaining);
			buffer += size;
			remaining -= size;
		}
	}

	/* Initialize the USB ROM API and patch errata */
	ErrorCode_t ret = USBD_API->hw->Init(&usb_handle, &desc, &usb_param);
	if (ret != LPC_OK)
		return ret;
	errata_usbrom2_patch(usb_handle);

	/* register our setup handler */
	ret = USBD_API->core->RegisterClassHandler(usb_handle, ep0_handler, NULL);
	if (ret != LPC_OK)
		return ret;

	/* register EP1 IN handler */
	ret = USBD_API->core->RegisterEpHandler(usb_handle, EP_IN_INDEX(1), ep1_in_handler, NULL);
	if (ret != LPC_OK)
		return ret;

	/* enable USB interrupts, connect to the host to start enumeration */
	NVIC_EnableIRQ(USB0_IRQn);
	USBD_API->hw->Connect(usb_handle, 1);

	return LPC_OK;
}
