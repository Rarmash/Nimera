#include <nimera/console.h>

// The common console layer knows only the console contract. The current
// platform supplies these operations with its QEMU virt PL011 driver.
extern void uart_putc(char c);
extern void uart_puts(const char *text);
extern char uart_getc(void);
extern int uart_try_getc(char *result);

void console_putc(char c)
{
	uart_putc(c);
}

void console_write(const char *text)
{
	uart_puts(text);
}

char console_getc(void)
{
	return uart_getc();
}

int console_try_getc(char *result)
{
	return uart_try_getc(result);
}
