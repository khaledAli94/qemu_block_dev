#include <stdint.h>
#include <uart.h>
#include <sdhc.h>


uint8_t buffer[512];

void main() {
    uart_print("\n--- QEMU Bare Metal SD Read Demo ---\n");

    sd_init();

    // Read Sector 0 (First 512 bytes of sdcard.img)
    sd_read_sector(0, buffer);

    uart_print("First 64 bytes of Sector 0:\n");
    for (int i = 0; i < 64; i++) {
        uart_print_hex(buffer[i]);
        uart_print(" ");
        if ((i + 1) % 16 == 0) uart_print("\n");
    }

    uart_print("\nDone.\n");
    while (1);
}