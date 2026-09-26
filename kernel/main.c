// No libc, allocator, or runtime are needed for this milestone.

#include <nimera/console.h>
#include <nimera/exception.h>
#include <nimera/format.h>
#include <nimera/memory.h>
#include <nimera/panic.h>
#include <nimera/pmm.h>
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

#if NIMERA_PMM_TEST
static void pmm_test(void)
{
	u64 first;
	u64 second;
	u64 third;
	u64 again;
	u64 initial_free = pmm_free_pages();

	console_write("Nimera PMM test\r\nPage size: ");
	format_u64_decimal(NIMERA_PAGE_SIZE);
	console_write(" bytes\r\nBitmap: ");
	format_u64_decimal(pmm_bitmap_bytes());
	console_write(" bytes in ");
	format_u64_decimal(pmm_metadata_pages());
	console_write(" page(s)\r\nInitial free pages: ");
	format_u64_decimal(initial_free);
	console_write("\r\n\r\nAllocated:\r\n");

	if (pmm_alloc_page(&first) != 0 || pmm_alloc_page(&second) != 0 ||
	    pmm_alloc_page(&third) != 0 || first == second || first == third ||
	    second == third || (first & (NIMERA_PAGE_SIZE - 1ULL)) != 0ULL ||
	    (second & (NIMERA_PAGE_SIZE - 1ULL)) != 0ULL ||
	    (third & (NIMERA_PAGE_SIZE - 1ULL)) != 0ULL) {
		panic("PMM allocation test failed");
	}
	console_write("  ");
	format_u64_hex(first);
	console_write("\r\n  ");
	format_u64_hex(second);
	console_write("\r\n  ");
	format_u64_hex(third);
	console_write("\r\nFreed: ");
	format_u64_hex(second);
	pmm_free_page(second);
	console_write("\r\nFree pages after free: ");
	format_u64_decimal(pmm_free_pages());
	console_write("\r\nAllocated again: ");
	if (pmm_alloc_page(&again) != 0 ||
	    (again & (NIMERA_PAGE_SIZE - 1ULL)) != 0ULL) {
		panic("PMM reallocation test failed");
	}
	format_u64_hex(again);
	pmm_free_page(first);
	pmm_free_page(third);
	pmm_free_page(again);
	console_write("\r\nFinal managed pages: ");
	format_u64_decimal(pmm_total_pages());
	console_write("\r\nFinal free pages: ");
	format_u64_decimal(pmm_free_pages());
	console_write("\r\nFinal allocated pages: ");
	format_u64_decimal(pmm_used_pages());
	console_write("\r\nPMM test complete.\r\n");
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

	struct memory_map map = memory_discover();
	pmm_init(&map);

#if NIMERA_MEMORY_TEST
	console_write("Nimera memory test\r\n");
	memory_print_map(&map);
	console_write("Memory test complete.\r\n");
	return;
#endif

#if NIMERA_PMM_TEST
	pmm_test();
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
