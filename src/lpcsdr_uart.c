#include "lpcsdr_uart.h"

#include "chip.h"

#include <string.h>

void lpcsdr_uart_init(void)
{
    /* configure pins for the UART0 header on the v2 prototype */
    Chip_SCU_PinMuxSet(2, 0, SCU_MODE_PULLDOWN | SCU_MODE_FUNC2);                                      /* P2_0, U0_TXD */
    Chip_SCU_PinMuxSet(2, 1, SCU_MODE_INACT | SCU_MODE_INBUFF_EN | SCU_MODE_ZIF_DIS | SCU_MODE_FUNC2); /* P2_1, U0_RXD */

    /* configure UART0 for 115200 baud, 8N1, using the external 12MHz crystal as the base clock */
    Chip_Clock_SetBaseClock(CLK_BASE_UART0, CLKIN_CRYSTAL, true, false);
    Chip_UART_Init(LPC_USART0);
    Chip_UART_ConfigData(LPC_USART0, UART_LCR_WLEN8 | UART_LCR_SBS_1BIT | UART_LCR_PARITY_DIS); /* configure 8N1 */
    Chip_UART_SetBaudFDR(LPC_USART0, 115200); /* no integer divisor for 12MHz clock -> 115200 baud, so need a fractional divisor */

    lpcsdr_uart_write("hello world\r\n");
}

void lpcsdr_uart_write(const char *str)
{
    Chip_UART_SendBlocking(LPC_USART0,  str, strlen(str));
}
