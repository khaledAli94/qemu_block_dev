#include <stdint.h>

#define UART0_BASE      0x10009000
#define UART0_DR        (*(volatile uint32_t *)(UART0_BASE + 0x000))
#define UART0_FR        (*(volatile uint32_t *)(UART0_BASE + 0x018))

void uart_putc(char c);
void uart_print(const char *s);
void uart_print_hex(uint32_t n);