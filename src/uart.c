#include <uart.h>

void sys_uart_init(void)
{
    uint32_t val;

    /* Enable UART0 clock gate */
    val = *CCU_UART_GATE;
    val |= (1 << 16);
    *CCU_UART_GATE = val;

    /* Deassert UART0 reset */
    val = *CCU_UART_RESET;
    val |= (1 << 16);
    *CCU_UART_RESET = val;

    /* Configure UART0: 115200 8N1 */
    *UART_IER = 0x00;
    *UART_FCR = 0xf7;
    *(volatile uint32_t *)(UART0_BASE + 0x10) = 0x00;  // MCR

    /* Enable DLAB */
    val = *UART_LCR;
    val |= (1 << 7);
    *UART_LCR = val;

    /* Set baud rate divisor (0x000D) */
    *UART_DLL = 0x0d & 0xff;
    *UART_DLH = (0x0d >> 8) & 0xff;

    /* Disable DLAB */
    val = *UART_LCR;
    val &= ~(1 << 7);
    *UART_LCR = val;

    /* 8 data bits, no parity, 1 stop bit */
    val = *UART_LCR;
    val &= ~0x1f;
    val |= (3 << 0);  // 8 bits
    *UART_LCR = val;
}

void sys_uart_putc(char c)
{
    *UART_THR = (uint32_t)c;
}

char sys_uart_getc(void)
{
    return (char)(*UART_RBR);
}

void sys_uart_puts(const char *str){
    if (!str)
        return;

    while (*str) {
        sys_uart_putc(*str++);
    }
}




static void uart_put_uint(unsigned int val, int base)
{
    char buf[16];
    int i = 0;

    if (val == 0) {
        sys_uart_putc('0');
        return;
    }

    while (val > 0 && i < sizeof(buf)) {
        unsigned int digit = val % base;
        buf[i++] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        val /= base;
    }

    while (i--)
        sys_uart_putc(buf[i]);
}


int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {

        if (*fmt != '%') {
            sys_uart_putc(*fmt++);
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
            sys_uart_putc(c);
            break;
        }

        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) {
                sys_uart_putc('-');
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
            sys_uart_putc('%');
            break;

        default:
            sys_uart_putc('%');
            sys_uart_putc(*fmt);
            break;
        }

        fmt++;
    }

    va_end(ap);
    return 0;
}