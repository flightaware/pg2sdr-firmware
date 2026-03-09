#ifndef PG2SDR_MEM_H
#define PG2SDR_MEM_H

/* Special memory layout symbols provided by the linker script.
 * These are generally used by taking the address of the symbol,
 * _not_ its value.
 *
 * They are declared with an incomplete struct type so that
 *  a) attempts to use its value will provoke an error;
 *  b) gcc doesn't try to flag accesses that appear to be past the
 *     end of the struct
 */
struct linker_symbol;

/* Initial top of stack */
extern struct linker_symbol _vStackTop;

/* USB buffer pool boundaries */
extern struct linker_symbol __usb1_start;
extern struct linker_symbol __usb1_end;
extern struct linker_symbol __usb2_start;
extern struct linker_symbol __usb2_end;

/* DMA buffer pool boundaries */
extern struct linker_symbol __dma1_start;
extern struct linker_symbol __dma1_end;
extern struct linker_symbol __dma2_start;
extern struct linker_symbol __dma2_end;

/* Relocator code (see relocator.s) */
extern struct linker_symbol relocator_start;
extern struct linker_symbol relocator_end;

#endif
