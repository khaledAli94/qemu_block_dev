#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <uart.h>
#include <stdarg.h>

#define GPIO_BASE      0x01c20800
#define CCU_BASE       0x01c20000
#define RESET_BASE     0x01c20200
#define UART0_BASE     0x01c28000

#define GPIO_CFG0      ((volatile uint32_t *)(GPIO_BASE + 0x00))
#define CCU_UART_GATE  ((volatile uint32_t *)(CCU_BASE  + 0x6c))
#define CCU_UART_RESET ((volatile uint32_t *)(RESET_BASE + 0xd8))

#define UART_RBR       ((volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_THR       ((volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_DLL       ((volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_DLH       ((volatile uint32_t *)(UART0_BASE + 0x04))
#define UART_IER       ((volatile uint32_t *)(UART0_BASE + 0x04))
#define UART_FCR       ((volatile uint32_t *)(UART0_BASE + 0x08))
#define UART_LCR       ((volatile uint32_t *)(UART0_BASE + 0x0c))
#define UART_LSR       ((volatile uint32_t *)(UART0_BASE + 0x7c))

#define GR "\033[32m"
#define RS  "\033[0m"
#define BG_YEL "\033[1;32m"

void sys_uart_init(void);
char sys_uart_getc(void);
void sys_uart_putc(char c);
void sys_uart_puts(const char *str);


int printf(const char *fmt, ...);
#endif
