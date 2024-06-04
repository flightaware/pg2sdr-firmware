/* Minimal header required for USB ROM headers */

#ifndef APP_USBD_CFG_H
#define APP_USBD_CFG_H

#define USB_MAX_IF_NUM          8		/*!< Max interface number used for building USBD ROM. DON'T CHANGE. */
#define USB_MAX_EP_NUM          6		/*!< Max number of EP used for building USBD ROM. DON'T CHANGE. */
#define USB_MAX_PACKET0         64		/*!< Max EP0 packet size used for building USBD ROM. DON'T CHANGE. */
#define USB_FS_MAX_BULK_PACKET  64		/*!< MAXP for FS bulk EPs used for building USBD ROM. DON'T CHANGE. */
#define USB_HS_MAX_BULK_PACKET  512		/*!< MAXP for HS bulk EPs used for building USBD ROM. DON'T CHANGE. */
#define USB_DFU_XFER_SIZE       2048	/*!< Max DFU transfer size used for building USBD ROM. DON'T CHANGE. */

#endif
