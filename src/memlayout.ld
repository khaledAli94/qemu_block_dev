ENTRY(_start)

MEMORY
{
    /* Start at 64KB offset (0x60010000).
       Total RAM is 128MB.
       We must SUBTRACT the 64KB offset from LENGTH 
       so the end doesn't go off the cliff.
       128MB - 64KB = 128M - 65536
    */
    /* RAM (rwx) : ORIGIN = 0x60010000, LENGTH = 128M - 64K */
    RAM (xrw) : ORIGIN = 0x60000000, LENGTH = 128M
}

SECTIONS
{
    . = ORIGIN(RAM);

    .text : {
        *(.text)
        *(.rodata)   /* Explicitly keep this safe */
        *(.rodata.*)
    } > RAM

    .data : { *(.data) } > RAM

    .bss : {
        __bss_start = .;
        *(.bss)
        __bss_end = .;
    } > RAM

    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}