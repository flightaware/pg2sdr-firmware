#ifndef LPCSDR_PROTOCOL_H
#define LPCSDR_PROTOCOL_H

/* This header will be compiled by both the host and the firmware,
 * so avoid hardware-specific inclusions here
 */

#include <stdbool.h>
#include <stdint.h>

/* --- Control endpoint EP0 --- */

/* vendor requests, IN (lpc -> host) */

typedef enum {
    EP0_IN_COMMS_CHECK = 0x01,        /* basic comms check */
    EP0_IN_FLASH_DEVICE_ID = 0x02,    /* read device/manufacturer ID from SPI flash */
    EP0_IN_FLASH_UNIQUE_ID = 0x03,    /* read unique ID from SPI flash */
    EP0_IN_FLASH_READ = 0x04,         /* read data from SPI flash; valueAndIndex = start address */
    EP0_IN_FLASH_READ_QUAD = 0x05,    /* read data from SPI flash, quad mode; valueAndIndex = start address */
    EP0_IN_MEMORY_READ = 0x0B,        /* read arbitrary memory; valueAndIndex = start address */
    EP0_IN_TUNER_READ = 0x0C,         /* read tuner regs; value = first reg to read; index = cache mode (0=use cache if possible, 1=bypass cache, 2=refresh cache) */
    EP0_IN_BOARD_STATUS = 0x0D,       /* read misc board status; valueAndIndex != 0 to also measure clock frequencies (takes longer) */
    EP0_IN_TUNER_LOCK = 0x0E,         /* update vco_current, wait for PLL to lock, return PLL status. value = vco_current to set (0..7), index = timeout in ms */
} ep0_in_request_t;

/* structures returned from IN transfers */

#define COMMS_CHECK_MAGIC 0xDEADBEEF
typedef struct {
    uint32_t magic;
} ep0_in_comms_check_t;

typedef struct {
    uint16_t device_id;
} ep0_in_flash_device_id_t;

typedef struct {
    uint64_t unique_id;
} ep0_in_flash_unique_id_t;

/* Various status flags */
/* #define STATUS_FAST_CPU          1 */ /* CPU is running at fast speed */
#define STATUS_SW1_USBBOOT        2  /* SW1 is closed (boot-from-USB mode) */
#define STATUS_SW2_PRESSED        4  /* SW2 is depressed */
#define STATUS_RF_POWER_ON        8  /* RF power is on */
#define STATUS_HSADC_RUN         16  /* HSADC conversion is running */
#define STATUS_DMA_RUN           32  /* DMA is running */
#define STATUS_EP1_ENABLED       64  /* USB EP1 is enabled (not stalled) */
#define STATUS_TUNER_I2C_ERROR  128  /* Saw an I2C error while talking to the tuner */
#define STATUS_TUNER_PLL_LOCK   256  /* Tuner PLL has lock */
#define STATUS_PLL0AUDIO_RUN    512  /* PLL0AUDIO PLL (HSADC clock) is programmed and running */
#define STATUS_IS_LPCSDR       1024  /* firmware built for lpcsdr hardware */
#define STATUS_IS_AIRSPY       2048  /* firmware built for airspy hardware */
typedef struct {
    /* Flags from STATUS_xxx */
    uint32_t flags;

    /* HSADC clock (PLL0AUDIO) */
    uint32_t hsadc_frequency;
    uint32_t pll_stat;
    uint32_t pll_ctrl;
    uint32_t pll_mdiv;
    uint32_t pll_np_div;
    uint32_t pll_frac;
    uint32_t idiv_e_ctrl;

    /* HSADC status */
    uint32_t adchs_fifo_cfg;
    uint32_t adchs_config;
    uint32_t adchs_adc_speed;
    uint32_t adchs_power_control;
    uint32_t adchs_int0_status;
    uint32_t adchs_fifo_sts;
    uint32_t adchs_dscr_sts;

    /* DMA status */
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

    /* Tuner status */
    uint8_t tuner_regs[32];

    /* USB status */
    uint32_t usb_free_buffers;
    uint32_t usb_filled_buffers;
    uint32_t usb_samples_per_block;
    uint32_t usb_bytes_per_block;

    /* CPU clock / load info */
    uint32_t m4_freq;               /* M4 clock frequency, Hz */
    uint32_t m4_mean_idle;          /* Recent idle CPU time, in CPU cycles */
    uint32_t m4_mean_idle_scale;    /* Divisor for m4_mean_idle, total busy+idle CPU cycles in the measurement period */
    uint32_t m4_min_idle;           /* Recent minimum idle CPU time, in CPU cycles */
    uint32_t m4_min_idle_scale;     /* Divisor for m4_min_idle, total busy+idle CPU cycles in the minimum period */

    /* Measured clock frequencies (only if requested) */
    uint32_t clock_32k;
    uint32_t clock_irc;
    uint32_t clock_pll0usb;
    uint32_t clock_pll0audio;
    uint32_t clock_pll1;
    uint32_t clock_idiv_a;
    uint32_t clock_idiv_b;
    uint32_t clock_idiv_c;
    uint32_t clock_idiv_d;
    uint32_t clock_idiv_e;

    uint32_t tuner_xtal; /* R860T crystal frequency */

    uint64_t serial_number; /* unique 64-bit ID from flash chip */
} ep0_in_board_status_t;


typedef struct {
    uint8_t pll_locked;    /* 0 = not locked, 1 = locked */
} ep0_in_tuner_lock_t;

/* vendor requests, OUT (host -> lpc) */
typedef enum {
    EP0_OUT_COMMS_CHECK = 0x01,       /* basic comms check */
    EP0_OUT_FLASH_WRITE = 0x10,       /* SPI flash page write; valueAndIndex = starting address; write must be contained within a single 256-byte page */
    EP0_OUT_FLASH_ERASE = 0x11,       /* SPI flash sector erase; valueAndIndex = sector address; address must be 4096-byte aligned */
    EP0_OUT_START_TRANSFER = 0x13,    /* Start ADC and DMA, start transferring data to USB EP1 */
    EP0_OUT_STOP_TRANSFER = 0x14,     /* Stop ADC, DMA, EP1 transfers */
    EP0_OUT_SET_RF_POWER = 0x15,      /* Set RF power state; valueAndIndex = 0 (RF power off) / 1 (RF power on) / 2 (power off, then power on -- resets tuner) */
    EP0_OUT_TUNER_WRITE = 0x16,       /* Write tuner registers; value = index of first register to write; index = cache policy (0=write through, 1=bypass) */
    EP0_OUT_TUNER_UPDATE = 0x17,      /* Update tuner registers; value = index of first updated register; see code for formatting of the data payload */
    EP0_OUT_CONFIG_ADC = 0x2C,        /* Change ADC configuration. value = bitwise-or of 1 (DCINNEG) / 2 (DCINPOS) / 4 (TWOS) */
    EP0_OUT_RESET = 0x2D,             /* force device reset */
    EP0_OUT_WATCHDOG_TEST = 0x2E,     /* Trigger watchdog tests */
    EP0_OUT_UART_TEST = 0x2F,         /* Trigger UART tests */
} ep0_out_request_t;

typedef struct {
    uint32_t magic;
} ep0_out_comms_check_t;

typedef struct {
    uint32_t n_divisor;     /* PLL0AUDIO pre-divisor (0 = bypass divider */
    uint32_t m_divisor;     /* PLL0AUDIO feedback divisor, fixed point, 15 bit fractional part */
    uint32_t p_divisor;     /* PLL0AUDIO post-divisor (0 = bypass divider */
    uint32_t idiv_divisor;  /* IDIV_E divisor (0 = don't use IDIV_E) */
} ep0_out_start_transfer_t;

/* ---  Bulk endpoint EP1   --- */

/* header we're going to put on each USB buffer that we send */

typedef struct {
    uint32_t magic;       /* BLOCK_MAGIC */
    uint32_t block_len;   /* Total length of this (and every) block, in bytes, multiple of 512 */
    uint32_t samples;     /* Number of samples in this (and every) block, multiple of 8 */
    uint32_t sequence;    /* Block sequence for this block. Non-sequential sequence numbers indicate a discontinuity */
    uint32_t status;      /* Status bits for this block */
} ep1_header_t;

#define BLOCK_MAGIC 0xDEADBEEF

/* status bits for ep1_header_t.status */

/* ADC FIFO overrun, data was dropped */
#define BLOCK_STATUS_ADC_OVERRUN       1
/* DMA error seen */
#define BLOCK_STATUS_DMA_ERROR         2
/* Packing overrun, main loop did not copy/pack data in time before the buffer was reused by ADC DMA */
#define BLOCK_STATUS_PACKING_OVERRUN   4
/* Host overrun, data not transferred over USB fast enough */
#define BLOCK_STATUS_USB_OVERRUN       8

/* ADC range overflow happened */
#define BLOCK_STATUS_ADC_OVF         256
/* ADC range underflow happened */
#define BLOCK_STATUS_ADC_UNF         512
/* Tuner frequency is changing */
#define BLOCK_STATUS_FREQ_CHANGE    1024


#endif /* LPCSDR_PROTOCOL_H */
