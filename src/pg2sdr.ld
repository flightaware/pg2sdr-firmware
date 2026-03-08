MEMORY
{
    RAM (rwx) : ORIGIN = 0x10000000, LENGTH = 0xD000 /* limit firmware image to first 56kB, leaving 72kB for USB buffers */
}

ENTRY(ResetISR)

SECTIONS
{
    /* Main text (code) section */
    .text : ALIGN(4)
    {
        FILL(0xff)

        /* vector table, must be at the very start */
        KEEP(*(.isr_vector))

        *(.text*)

        . = ALIGN(4);
        _etext = .;
    } > RAM

    /* Initialized data section */
    .data : ALIGN(4)
    {
       FILL(0xff)
       _data = . ;

       /* BSS table, used to zero out BSS sections on boot */
       . = ALIGN(4) ;
       __bss_section_table = .;
       LONG(ADDR(.bss));
       LONG(SIZEOF(.bss));
       __bss_section_table_end = .;

       *(.rodata .rodata.* .constdata .constdata.*)
       *(.data*)

       /* pad loaded image to 512-byte boundary for consistency with the image header */
       . = ALIGN(512);
       _edata = .;
    } > RAM

    /* BSS (zeroed data) section */
    .bss (NOLOAD) : ALIGN(4)
    {
        _bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4) ;
        _ebss = .;
    } > RAM

    /* put an 8kB stack at the end of the memory region we're using */
    PROVIDE(_vStackTop = ORIGIN(RAM) + LENGTH(RAM));
}
