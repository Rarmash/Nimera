// No libc, allocator, or runtime are needed for this milestone.

#include <nimera/console.h>
#include <nimera/panic.h>
#include <nimera/timer.h>

#if NIMERA_TIMER_TEST
static void timer_test(void)
{
	u64 frequency = timer_frequency();
	u64 deadline = timer_ticks() + frequency;

	console_write("Nimera timer test\r\n");

	for (unsigned int count = 0U; count < 3U; ++count) {
		while (timer_ticks() < deadline) {
		}

		console_write("tick\r\n");
		deadline += frequency;
	}

	console_write("Timer test complete.\r\n");
}
#endif

void kernel_main(void)
{
	timer_init();

#if NIMERA_PANIC_TEST
	panic("panic test");
#endif

#if NIMERA_TIMER_TEST
	timer_test();
	return;
#endif

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
