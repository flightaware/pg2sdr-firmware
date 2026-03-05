#ifndef PG2SDR_UART_H
#define PG2SDR_UART_H

#include "pg2sdr_hardware.h"

void pg2sdr_uart_init(void);
void pg2sdr_uart_write(const char *data, unsigned len);
void pg2sdr_uart_flush(void);
void _debug_printf(const char* format, ...);

#if defined(DEBUG)
#define debug_printf _debug_printf
#else
/* with a release build, don't even call _debug_printf
 * so that string constants don't even get created
 */
#define debug_printf(x, ...) do {} while(0)
#endif

#endif /* PG2SDR_UART_H */
