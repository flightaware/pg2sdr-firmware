MEMORY
{
    RAM (rwx) : ORIGIN = 0x10000000, LENGTH = 0x0D800 /* Firmware space, limited to ~56kB to leave more space for USB buffers */
    IPC       : ORIGIN = 0x1000D800, LENGTH = 0x00800 /* Inter-core mailboxes */
    USB1      : ORIGIN = 0x1000E000, LENGTH = 0x12000 /* First 72kB area for USB buffers */
    USB2      : ORIGIN = 0x10080000, LENGTH = 0x12000 /* Second 72kB area for USB buffers */
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

    /* 4kB stack with 8-byte alignment */
    .stack (NOLOAD) : ALIGN(8)
    {
        . += 4096;
        _vStackTop = .;
    } > RAM

    /* Mailboxes for IPC at a well-known address */
    .mailboxes (NOLOAD) : ALIGN(4)
    {
        *(.mailboxes);
    } > IPC

    /* USB and DMA pools */
    .usb1 (NOLOAD) :
    {
        __usb1_start = ORIGIN(USB1);
        __usb1_end = ORIGIN(USB1) + LENGTH(USB1);
    } > USB1

    .usb2 (NOLOAD) :
    {
        __usb2_start = ORIGIN(USB2);
        __usb2_end = ORIGIN(USB2) + LENGTH(USB2);
    } > USB2
}
