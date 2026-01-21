/*
sudo qemu-system-arm \
  -M vexpress-a9 \
  -cpu cortex-a9 \
  -m 128M \
  -kernel ./bootloader.elf \
  -nographic \
  -s -S \
  -audiodev none,id=snd0 \
  -sd ../sdcard.img

arm-none-linux-gnueabihf-objdump -d bootloader.elf --disassembler-color=on   --visualize-jumps=color | less -R

sudo cfdisk ../sdcard.img
sudo mkfs.fat -F 32 --offset=2048 ../sdcard.img
sudo dd if=../sdcard.img bs=512 count=1 | hexdump -C | less -R
*/

.global _start
_start:
    /* Load stack pointer to the end of RAM (approx 128MB offset from base) */
    ldr sp, =_stack_top
    bl uart_init
    bl sd_init
    bl main
hang:
    b hang