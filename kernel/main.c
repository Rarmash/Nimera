// No libc, allocator, or runtime are needed for this milestone.

#include <nimera/console.h>

void kernel_main(void)
{
	console_write("Hello from kernel\r\n");
	console_write("Echo mode enabled. Type characters:\r\n");

	for (;;) {
		char c = console_getc();

		// Terminals commonly send CR for Enter, while some send LF. Normalize
		// either form to CRLF so the serial terminal starts a clean new line.
		if (c == '\r' || c == '\n') {
			console_write("\r\n");
		} else {
			console_putc(c);
		}
	}
}
