#include <uart.h>
#include <stdarg.h> // Required for va_list

void uart_init() {
    UART0_CR = 0x0;        // Disable UART
    UART0_LCRH = 0x70;     // Enable FIFO, 8-bit word length
    UART0_CR = 0x301;      // Enable UART, TX enable, RX enable
}

char uart_getc(void)
{
    return (char)(UART0_DR & 0xFF);
}

void uart_putc(char c) {
    while (UART0_FR & (1 << 5)); // Wait if TX FIFO full
    UART0_DR = c;
}

void sys_uart_puts(const char *str){
    if (!str)
        return;

    while (*str) {
        uart_putc(*str++);
    }
}

static void uart_put_uint(unsigned int val, int base)
{
    char buf[16];
    int i = 0;

    if (val == 0) {
        uart_putc('0');
        return;
    }

    while (val > 0 && i < sizeof(buf)) {
        unsigned int digit = val % base;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        val /= base;
    }

    while (i--)
        uart_putc(buf[i]);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {

        if (*fmt != '%') {
            uart_putc(*fmt++);
            continue;
        }

        fmt++;  // skip '%'

        switch (*fmt) {

        case 's': {
            char *s = va_arg(ap, char *);
            sys_uart_puts(s ? s : "(null)");
            break;
        }

        case 'c': {
            char c = (char)va_arg(ap, int);
            uart_putc(c);
            break;
        }

        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) {
                uart_putc('-');
                v = -v;
            }
            uart_put_uint((unsigned)v, 10);
            break;
        }

        case 'u': {
            unsigned v = va_arg(ap, unsigned);
            uart_put_uint(v, 10);
            break;
        }

        case 'x':
        case 'X': {
            unsigned v = va_arg(ap, unsigned);
            uart_put_uint(v, 16);
            break;
        }

        case '%':
            uart_putc('%');
            break;

        default:
            uart_putc('%');
            uart_putc(*fmt);
            break;
        }

        fmt++;
    }

    va_end(ap);
    return 0;
}