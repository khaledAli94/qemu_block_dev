#ifndef UART_H
#define UART_H
#include <stdint.h>

#define UART0_BASE      0x10009000
#define UART0_DR        (*(volatile uint32_t *)(UART0_BASE + 0x000))
#define UART0_FR        (*(volatile uint32_t *)(UART0_BASE + 0x018))
#define UART0_CR   (*(volatile uint32_t *)(UART0_BASE + 0x030))
#define UART0_LCRH (*(volatile uint32_t *)(UART0_BASE + 0x02C))

void uart_init();
char uart_getc(void);
void uart_putc(char c);

int printf(const char *fmt, ...);
#endif