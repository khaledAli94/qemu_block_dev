#include <uart.h>

void uart_putc(char c) {
    while (UART0_FR & (1 << 5)); // Wait if TX FIFO full
    UART0_DR = c;
}

void uart_print(const char *s) {
    while (*s) uart_putc(*s++);
}

void uart_print_hex(uint32_t n) {
    const char *hex = "0123456789ABCDEF";
    uart_print("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(n >> i) & 0xF]);
    }
}