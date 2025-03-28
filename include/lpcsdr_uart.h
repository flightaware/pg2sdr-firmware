#ifndef LPCSDR_UART_H
#define LPCSDR_UART_H

void lpcsdr_uart_init(void);
void lpcsdr_uart_write(const char *data, unsigned len);
void lpcsdr_uart_flush(void);
int debug_printf(const char* format, ...);

#endif /* LPCSDR_UART_H */
