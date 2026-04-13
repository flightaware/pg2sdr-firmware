#ifndef PG2SDR_PROTOCOL_H
#define PG2SDR_PROTOCOL_H

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

/* This header will be compiled by both the host and the firmware,
 * so avoid hardware-specific inclusions here
 */

#include <stdbool.h>
#include <stdint.h>

/* VID/PID of ROM bootloader */
#define VID_ROM 0x1fc9
#define PID_ROM 0x000c

/* VID/PID of normal firmware */
#define VID_PG2SDR 0xDEAD
#define PID_PG2SDR 0xBEEF

#define _VERSION(a,b,c,d) ((uint32_t)(((a) << 24) | ((b) << 16) | ((c) << 8) | (d)))
#define PG2_CURRENT_VERSION _VERSION(0,9,6,0)
#define PG2_COMPAT_VERSION _VERSION(0,9,0,0)

#define PG2_MAX_CONTROL_TRANSFER 512

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
    EP0_IN_METADATA = 0x0F,           /* return firmware_metadata_t structure */
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

/* Various status flags for ep0_in_board_status_t.flags */
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
#define STATUS_IS_PG2SDR       1024  /* firmware built for pg2sdr hardware */
#define STATUS_IS_AIRSPY       2048  /* firmware built for airspy hardware */

/* Reset reasons stored in ep0_in_board_status_t.reset_reason */
#define RESET_POR 0                  /* Power-on-reset. All unknown codes get mapped to this. */
#define RESET_UNEXPECTED 0x554EAAB1  /* Unexpected reset without firmware intervention (watchdog timer or hard fault) */
#define RESET_FIRMWARE 0x4649B9B6    /* Firmware was asked to reset itself */
#define RESET_PANIC 0x5041AFBE       /* Firmware panic causing a reset, reset code stores the panic blink code */
#define RESET_LOAD 0x4C4FB3B0        /* New firmware image was loaded from RAM */

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

    /* interrupt counters */
    uint32_t intr_systick;
    uint32_t intr_dma;
    uint32_t intr_usart0;
    uint32_t intr_usb0;
    uint32_t intr_wwdt;
    uint32_t intr_m0app;
    uint32_t intr_m4;

    /* details of last reset */
    uint32_t reset_reason;  /* RESET_xxx */
    uint32_t reset_code;    /* for RESET_PANIC, the blink code passed to pg2sdr_panic() */
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
    EP0_OUT_LOAD_IMAGE = 0x20,        /* load (part of) image to memory. valueAndIndex = start address */
    EP0_OUT_PANIC_TEST = 0x2A,        /* (debug build, 0.9.5.0+) Trigger firmware panic */
    EP0_OUT_LED_PATTERN = 0x2B,       /* (0.9.4.0+) set LED override pattern, valueAndIndex = pattern or 0 for no override */
    EP0_OUT_CONFIG_ADC = 0x2C,        /* (debug build) Change ADC configuration. value = bitwise-or of 1 (DCINNEG) / 2 (DCINPOS) / 4 (TWOS) */
    EP0_OUT_RESET = 0x2D,             /* force device reset */
    EP0_OUT_WATCHDOG_TEST = 0x2E,     /* (debug build) Trigger watchdog tests */
    EP0_OUT_UART_TEST = 0x2F,         /* (debug build) Trigger UART tests */
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

/* possible modes for EP0_OUT_SET_RF_POWER */
typedef enum {
    RF_POWER_OFF = 0,
    RF_POWER_ON = 1,
    RF_POWER_RESET = 2
} rf_power_mode_t;

/* possible modes for EP0_IN_TUNER_READ */
typedef enum {
    CACHE_NORMAL = 0,
    CACHE_BYPASS = 1,
    CACHE_REFRESH = 2
} tuner_cache_mode_t;

/* firmware metadata returned by EP0_IN_METADATA */
typedef struct firmware_metadata_s {
    uint32_t version;
    uint32_t compat;
    uint16_t max_control_transfer;
    uint16_t control_timeout_ms;
    char build_type[128];
} firmware_metadata_t;

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


#endif /* PG2SDR_PROTOCOL_H */
