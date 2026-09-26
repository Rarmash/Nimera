// No libc, allocator, or runtime are needed for this milestone.

#include <nimera/console.h>
#include <nimera/exception.h>
#include <nimera/format.h>
#include <nimera/memory.h>
#include <nimera/panic.h>
#include <nimera/shell.h>
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

#if NIMERA_MEMORY_TEST
	/* Keep the standalone memory discovery regression path unchanged. */
	struct memory_info info = memory_discover();
	console_write("Nimera memory test\r\n");
	console_write("Physical base: ");
	format_u64_hex(info.physical_base);
	console_write("\r\nPhysical memory: ");
	format_u64_decimal(info.physical_size);
	console_write(" bytes\r\nPhysical memory: ");
	format_u64_decimal(info.physical_size / (1024ULL * 1024ULL));
	console_write(" MiB\r\nMemory test complete.\r\n");
	return;
#endif

#if NIMERA_EXCEPTION_TEST
	console_write("Nimera exception test\r\nCurrentEL: ");
	format_u64_decimal(exception_current_el());
	console_write("\r\n");
	exception_test_trigger();
#endif

	console_write("Nimera booting...\r\n");
	console_write("kernel: starting shell\r\n");
	shell_run();
}
