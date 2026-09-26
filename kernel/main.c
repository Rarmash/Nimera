// No headers, libc, allocator, or runtime are needed for this milestone.

extern void uart_putc(char c);
extern char uart_getc(void);
extern void uart_puts(const char *text);

void kernel_main(void)
{
	uart_puts("Hello from kernel\r\n");
	uart_puts("Echo mode enabled. Type characters:\r\n");

	for (;;) {
		char c = uart_getc();

		// Terminals commonly send CR for Enter, while some send LF. Normalize
		// either form to CRLF so the serial terminal starts a clean new line.
		if (c == '\r' || c == '\n') {
			uart_puts("\r\n");
		} else {
			uart_putc(c);
		}
	}
}
