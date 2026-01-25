/*
qemu-system-arm -cpu cortex-a7 -m 1G -M orangepi-pc  -nographic -kernel ./bootloader.elf -s -S -serial mon:stdio -sd ../sdcard.img \
 -trace enable=allwinner_sdhost_read \
 -trace enable=allwinner_sdhost_write \
 -trace enable=allwinner_sdhost_update_irq \
 -trace enable=allwinner_sdhost_process_desc \
 -trace enable=allwinner_sdhost_set_inserted \
 

arm-none-linux-gnueabihf-objdump -d bootloader.elf --disassembler-color=on   --visualize-jumps=color | less -R

sudo cfdisk ../sdcard.img
sudo mkfs.fat -F 32 --offset=2048 ../sdcard.img
sudo dd if=../sdcard.img bs=512 count=1 | hexdump -C | less -R
*/

.global _start
_start:
    b   reset_handler // 0x00 Reset
    b   .             // 0x04 Undefined Instruction
    b   .             // 0x08 SVC
    b   .             // 0x0C Prefetch Abort
    b   .             // 0x10 Data Abort
    b   .             // 0x14 Reserved
    b   .             // 0x18 IRQ
    b   .             // 0x1C FIQ

reset_handler:
    /* Load stack pointer to the end of RAM (approx 128MB offset from base) */
    ldr sp, =_stack_top
    bl sys_uart_init
    bl malloc_init
    bl sd_init
    bl main
    b .