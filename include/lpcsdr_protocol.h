#ifndef LPCSDR_PROTOCOL_H
#define LPCSDR_PROTOCOL_H

#include "lpc_types.h"

/* header we're going to put on each USB buffer that we send */

typedef struct {
    uint32_t magic;       /* 0xDEADBEEF */
    uint32_t block_len;   /* Total length of this (and every) block, in bytes, multiple of 512 */
    uint32_t samples;     /* Number of samples in this (and every) block, multiple of 8 */
    uint32_t sequence;    /* Block sequence for this block. Non-sequential sequence numbers indicate a discontinuity */
    uint32_t status;      /* Status bits for this block */
} usb_header_t;

/* status bits for usb_header_t.status */

/* ADC FIFO overrun, data was dropped */
#define BLOCK_STATUS_ADC_OVERRUN _BIT(0)
/* DMA error seen */
#define BLOCK_STATUS_DMA_ERROR _BIT(1)
/* Packing overrun, main loop did not copy/pack data in time before the buffer was reused by ADC DMA */
#define BLOCK_STATUS_PACKING_OVERRUN _BIT(2)
/* Host overrun, data not transferred over USB fast enough */
#define BLOCK_STATUS_USB_OVERRUN _BIT(3)

/* ADC range overflow happened */
#define BLOCK_STATUS_ADC_OVF _BIT(8)
/* ADC range underflow happened */
#define BLOCK_STATUS_ADC_UNF _BIT(9)
/* Tuner frequency is changing */
#define BLOCK_STATUS_FREQ_CHANGE _BIT(10)


#endif /* LPCSDR_PROTOCOL_H */
