// No libc, allocator, or runtime are needed for this milestone.

#include <nimera/console.h>
#include <nimera/memory.h>
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

#if NIMERA_MEMORY_TEST
static void write_u64_hex(u64 value)
{
	static const char digits[] = "0123456789abcdef";
	char reversed[16];
	unsigned int count = 0U;

	console_write("0x");
	do {
		reversed[count++] = digits[value & 0xfULL];
		value >>= 4;
	} while (value != 0ULL);

	while (count != 0U) {
		console_putc(reversed[--count]);
	}
}

static void write_u64_decimal(u64 value)
{
	char reversed[20];
	unsigned int count = 0U;

	do {
		reversed[count++] = (char)('0' + (value % 10ULL));
		value /= 10ULL;
	} while (value != 0ULL);

	while (count != 0U) {
		console_putc(reversed[--count]);
	}
}

static void memory_test(void)
{
	struct memory_info info = memory_discover();

	console_write("Nimera memory test\r\n");
	console_write("Physical base: ");
	write_u64_hex(info.physical_base);
	console_write("\r\nPhysical memory: ");
	write_u64_decimal(info.physical_size);
	console_write(" bytes\r\nPhysical memory: ");
	write_u64_decimal(info.physical_size / (1024ULL * 1024ULL));
	console_write(" MiB\r\nMemory test complete.\r\n");
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

#if NIMERA_MEMORY_TEST
	memory_test();
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
