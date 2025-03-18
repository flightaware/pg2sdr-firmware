#ifndef LPCSDR_PROTOCOL_H
#define LPCSDR_PROTOCOL_H

#include "lpc_types.h"

/* --- Control endpoint EP0 --- */

/* vendor requests, IN (lpc -> host) */

/* Basic comms check, always returns magic = DEADBEEF */
#define EP0_IN_COMMS_CHECK 0x01
typedef struct {
    uint32_t magic;       /* always 0xDEADBEEF */
} ep0_in_comms_check_t;

/* Read device/manufacturer ID from SPI EEPROM */
#define EP0_IN_FLASH_DEVICE_ID 0x02
typedef struct {
    uint16_t device_id;
} ep0_in_flash_device_id_t;

/* Read unique ID from SPI EEPROM */
#define EP0_IN_FLASH_UNIQUE_ID 0x03
typedef struct {
    uint64_t unique_id;
} ep0_in_flash_unique_id_t;

/* Read data from SPI EEPROM
 * valueAndIndex: start address
 * length: 1..256
 */
#define EP0_IN_FLASH_READ 0x04

/* Read data from SPI EEPROM (using quad mode)
 * valueAndIndex: start address
 * length: 1..256
 */
#define EP0_IN_FLASH_READ_QUAD 0x05

/* Read current switch settings as a bitvector */
#define EP0_IN_SWITCH_STATE 0x06
typedef struct {
    uint32_t switch_state; /* bitwise OR of SWITCH_SW* values */
#define SWITCH_SW1 0x01
#define SWITCH_SW2 0x02
} ep0_in_switch_state_t;

/* Measure the frequency, in Hz, of a given clock input
 * valueAndIndex: CHIP_CGU_CLKIN_T enum value to measure
 */
#define EP0_IN_INPUT_FREQ 0x07
typedef struct {
    uint32_t frequency;
} ep0_in_input_freq_t;

/* Measure the frequency, in Hz, of the base clock for a given clock input
 * valueAndIndex: CHIP_CGU_CLKIN_T enum value to measure
 */
#define EP0_IN_BASE_FREQ 0x08
typedef struct {
    uint32_t frequency;
} ep0_in_base_freq_t;

/* Read PLL0AUDIO-related registers */
#define EP0_IN_PLL0AUDIO_REGS 0x09
typedef struct {
    uint32_t pll_stat;
    uint32_t pll_ctrl;
    uint32_t pll_mdiv;
    uint32_t pll_np_div;
    uint32_t pll_frac;
    uint32_t idiv_e_ctrl;
} ep0_in_pll0audio_regs_t;

/* Read ADC- and DMA- related registers and internal state */
#define EP0_IN_ADC_DMA_STATUS 0x0A
typedef struct {
    uint32_t adchs_config;
    uint32_t adchs_int0_status;
    uint32_t adchs_fifo_sts;
    uint32_t adchs_dscr_sts;
    uint32_t gpdma_config;
    uint32_t gpdma_enbldchns;
    uint32_t gpdma_rawinttcstat;
    uint32_t gpdma_rawinterrstat;
    uint32_t gpdma0_config;
    uint32_t gpdma0_control;
    uint32_t gpdma0_srcaddr;
    uint32_t gpdma0_destaddr;
    uint32_t gpdma0_lli;
    uint32_t current_lli;
    uint32_t next_sequence;
} ep0_in_adc_dma_status_t;

/* Read arbitary memory.
 * valueAndIndex: start address
 * length: 1..256
 */
#define EP0_IN_MEMORY_READ 0x0B

/* Read R860T registers, starting from register 0
 * length: 4..36
 */
#define EP0_IN_TUNER_READ 0x0C

/* vendor requests, OUT (host -> lpc) */

/* communication check, stall if magic != DEADBEEF */
#define EP0_OUT_COMMS_CHECK 0x01
typedef struct {
    uint32_t magic;
} ep0_out_comms_check_t;

/* Write data to SPI EEPROM. Destination page should have been previously erased.
 * The written region should be entirely contained within a single
 * 256-byte page.
 *
 * valueAndIndex: start address
 * length: 1..256
 */
#define EP0_OUT_FLASH_WRITE 0x10

/* Erase SPI EEPROM sector (4k, 16 pages)
 * valueAndIndex: start address (should be 4096-byte sector aligned)
 */
#define EP0_OUT_FLASH_ERASE 0x11

/* Program and start HSADC clock. */
#define EP0_OUT_START_HSADC 0x12
typedef struct {
    uint32_t n_divisor;     /* PLL0AUDIO pre-divisor (0 = bypass divider */
    uint32_t m_divisor;     /* PLL0AUDIO feedback divisor, fixed point, 15 bit fractional part */
    uint32_t p_divisor;     /* PLL0AUDIO post-divisor (0 = bypass divider */
    uint32_t idiv_divisor;  /* IDIV_E divisor (0 = don't use IDIV_E) */
} ep0_out_start_hsadc_t;

/* Start HSADC conversion and USB bulk transfer on EP1 */
#define EP0_OUT_START_CONVERSION 0x13

/* Stop HSADC conversion, stall EP1 */
#define EP0_OUT_STOP_CONVERSION 0x14

/* Force power mode */
#define EP0_OUT_SET_POWER 0x15
typedef struct {
    uint8_t high_power_mode;
} ep0_out_set_power_t;

/* Write R860T registers
 * valueAndIndex: index of first register to write (>= 4)
 */
#define EP0_OUT_TUNER_WRITE 0x16

/* ---  Bulk endpoint EP1   --- */

/* header we're going to put on each USB buffer that we send */

typedef struct {
    uint32_t magic;       /* 0xDEADBEEF */
    uint32_t block_len;   /* Total length of this (and every) block, in bytes, multiple of 512 */
    uint32_t samples;     /* Number of samples in this (and every) block, multiple of 8 */
    uint32_t sequence;    /* Block sequence for this block. Non-sequential sequence numbers indicate a discontinuity */
    uint32_t status;      /* Status bits for this block */
} ep1_header_t;

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
