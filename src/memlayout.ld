ENTRY(_start)

MEMORY
{
    /* VExpress-A9 RAM starts at 0x60000000 */
    RAM (rwx) : ORIGIN = 0x60010000, LENGTH = 128M
}

SECTIONS
{
    . = ORIGIN(RAM);
    .text : {
        *(.text)
    } > RAM

    .rodata : {
        *(.rodata)
    } > RAM

    .data : {
        *(.data)
    } > RAM

    .bss : {
        __bss_start = .;
        *(.bss)
        __bss_end = .;
    } > RAM

    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}