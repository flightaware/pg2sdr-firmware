#ifndef PG2SDR_UART_H
#define PG2SDR_UART_H

void pg2sdr_uart_init(void);
void pg2sdr_uart_write(const char *data, unsigned len);
void pg2sdr_uart_flush(void);
void debug_printf(const char* format, ...);

#endif /* PG2SDR_UART_H */
