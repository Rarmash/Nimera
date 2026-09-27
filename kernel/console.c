#include <nimera/console.h>

// The common console layer knows only the console contract. The current
// platform supplies these operations with its QEMU virt PL011 driver.
extern void uart_putc(char c);
extern void uart_puts(const char *text);
extern char uart_getc(void);
extern int uart_try_getc(char *result);

static console_putc_sink output_putc;
static console_write_sink output_write;

void console_putc(char c)
{
	if (output_putc != (console_putc_sink)0) output_putc(c);
	else uart_putc(c);
}

void console_write(const char *text)
{
	if (output_write != (console_write_sink)0) output_write(text);
	else uart_puts(text);
}

char console_getc(void)
{
	return uart_getc();
}

int console_try_getc(char *result)
{
	return uart_try_getc(result);
}

void console_set_output(console_putc_sink putc, console_write_sink write)
{
	output_putc = putc;
	output_write = write;
}

void console_reset_output(void)
{
	output_putc = (console_putc_sink)0;
	output_write = (console_write_sink)0;
}

void debug_console_putc(char character)
{
	uart_putc(character);
}

void debug_console_write(const char *text)
{
	uart_puts(text);
}
