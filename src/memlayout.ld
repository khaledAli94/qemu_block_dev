ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 512M
}

SECTIONS
{
    . = ORIGIN(RAM);

    .text : {
        start.o (.text)
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

    .heap ALIGN(8) (NOLOAD) : {
        __heap_start = .;
        . = . + 128M;
        __heap_end = .;
    } > RAM


    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}