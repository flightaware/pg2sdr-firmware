#include "lpcsdr_uart.h"
#include "lpcsdr_common.h"
#include "lpcsdr_panic.h"
#include "lpcsdr_m4clock.h"
#include "lpcsdr_hardware.h"
#include "chip.h"
#include <stdarg.h>

#ifdef HW_HAS_UART

#define NANOPRINTF_IMPLEMENTATION
#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS 1
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS 0
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS 0
#define NANOPRINTF_VISIBILITY_STATIC

#include "nanoprintf.h"

static RINGBUFF_T ring;
#define RING_SIZE 4096   /* Send */
#define UART_FIFO_SIZE 16

/* Transmit buffers */
static uint8_t ring_buff[RING_SIZE];

static void handle_thre_interrupt(void)
{
    uint8_t data[UART_FIFO_SIZE];
    int n = RingBuffer_PopMult(&ring, data, UART_FIFO_SIZE);
    if (n <= 0) {
        LPC_USART0->IER &= ~UART_IER_THREINT; /* no further data, disable THRE interrupt */
        return;
    }

    for (int i = 0; i < n; ++i) {
        LPC_USART0->THR = (uint32_t) data[i]; /* push data to tx fifo */
    }
}

void pg2sdr_uart_init(void)
{
    RingBuffer_Init(&ring, ring_buff, 1, RING_SIZE);

    /* configure pins for the UART0 header on the v2 prototype */
    Chip_SCU_PinMuxSet(2, 0, SCU_MODE_PULLDOWN | SCU_MODE_FUNC1);                                      /* P2_0, U0_TXD */
    Chip_SCU_PinMuxSet(2, 1, SCU_MODE_INACT | SCU_MODE_INBUFF_EN | SCU_MODE_ZIF_DIS | SCU_MODE_FUNC1); /* P2_1, U0_RXD */

    /* configure UART0 for 115200 baud, 8N1, using the external 12MHz crystal as the base clock */
    Chip_Clock_SetBaseClock(CLK_BASE_UART0, CLKIN_CRYSTAL, true, false);
    Chip_UART_Init(LPC_USART0);
    Chip_UART_ConfigData(LPC_USART0, UART_LCR_WLEN8 | UART_LCR_SBS_1BIT | UART_LCR_PARITY_DIS); /* 8 data bits, 1 stop bit, no parity */
    Chip_UART_SetBaudFDR(LPC_USART0, 230400);
    Chip_UART_TXEnable(LPC_USART0);

    NVIC_EnableIRQ(USART0_IRQn);
}

void pg2sdr_uart_write(const char *data, unsigned len)
{
    if (!len)
        return;

    WITH_DISABLED_INTERRUPTS {
        RingBuffer_InsertMult(&ring, data, len);
        if ((LPC_USART0->LSR & UART_LSR_THRE) != 0) {
            /* tx FIFO is currently empty */
            handle_thre_interrupt();             /* synthesize an interrupt to fill the tx FIFO */
            LPC_USART0->IER |= UART_IER_THREINT; /* enable THRE interrupt to push more data later */
        }
    }
}

void pg2sdr_uart_flush()
{
    if (interrupts_disabled()) {
        /* Can't safely wait here, just immediately return */
        return;
    }

    while (true) {
        __disable_irq();

        uint32_t lsr = LPC_USART0->LSR;
        if (RingBuffer_IsEmpty(&ring) && (lsr & UART_LSR_THRE) != 0) {
            /* Ring and transmit FIFO are empty */
            if ((lsr & UART_LSR_TEMT) != 0) {
                /* transmit shift register is also empty, flush is complete */
                __enable_irq();
                return;
            }

            /* Ring/FIFO are empty but shift register is not empty, busy-wait until TEMT is set */
        } else {
            /* Ring or FIFO are not empty, sleep until interrupt */
            __DSB(); /* v7-M architecture requirement, but not strictly necessary on M0/M4 */
            __WFI();
        }

        /* let pending interrupts execute */
        __enable_irq();
        __ISB(); /* v7-M architecture requirement, but not strictly necessary on M0/M4 */
    }
}

void UART0_IRQHandler(void)
{
    uint32_t iir = LPC_USART0->IIR;

    if ((iir & UART_IIR_INTSTAT_PEND) == 0) { /* interrupt pending flag, active-low */
        switch (iir & UART_IIR_INTID_MASK) {
        case UART_IIR_INTID_THRE:
            handle_thre_interrupt();
            break;

        default:
            pg2sdr_unexpected_interrupt();
        }
    }

    ++pg2sdr_interrupts.usart0;
}

void debug_printf(const char *format, ...)
{
    if (!format)
        return;

    char buf[128];

    va_list va;
    va_start(va, format);
    int n = npf_vsnprintf(buf, sizeof(buf), format, va);;
    va_end(va);

    if (n > sizeof(buf)) {
        n = sizeof(buf);
        buf[n-1] = '!'; /* truncation indicator */
    }
    pg2sdr_uart_write(buf, n);
}

#else /* ! HW_HAS_UART */

void pg2sdr_uart_init(void)
{
    /* nothing */
}

void pg2sdr_uart_write(const char *data, unsigned len)
{
    /* nothing */
}

void pg2sdr_uart_flush(void)
{
    /* nothing */
}

void debug_printf(const char *format, ...)
{
    /* nothing */
}

#endif /* HW_HAS_UART */
