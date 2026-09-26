// No libc, allocator, or runtime are needed for this milestone.

#include <nimera/console.h>
#include <nimera/exception.h>
#include <nimera/format.h>
#include <nimera/heap.h>
#include <nimera/memory.h>
#include <nimera/mmu.h>
#include <nimera/panic.h>
#include <nimera/pmm.h>
#include <nimera/shell.h>
#include <nimera/timer.h>

#define NULL ((void *)0)

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

#if NIMERA_HEAP_TEST
static void fill_pattern(unsigned char *data, u64 size, unsigned char value)
{
	for (u64 index = 0ULL; index < size; ++index) {
		data[index] = value;
	}
}

static void check_pattern(const unsigned char *data, u64 size,
				  unsigned char value)
{
	for (u64 index = 0ULL; index < size; ++index) {
		if (data[index] != value) {
			panic("heap pattern verification failed");
		}
	}
}

static void print_pointer(const char *label, const void *pointer)
{
	console_write(label);
	format_u64_hex((u64)(unsigned long)pointer);
	console_write("\r\n");
}

static void heap_test(void)
{
	unsigned char *one;
	unsigned char *small;
	unsigned char *medium;
	unsigned char *large;
	unsigned char *growth;
	unsigned char *reused;
	u64 pmm_before = pmm_free_pages();

	console_write("Nimera heap test\r\nMMU: ");
	console_write(mmu_enabled() != 0ULL ? "enabled\r\n" : "disabled\r\n");
	console_write("PMM free before heap growth: ");
	format_u64_decimal(pmm_before);
	console_write("\r\n");
	one = (unsigned char *)kmalloc(1ULL);
	small = (unsigned char *)kmalloc(32ULL);
	medium = (unsigned char *)kmalloc(100ULL);
	large = (unsigned char *)kmalloc(1000ULL);
	growth = (unsigned char *)kmalloc(4000ULL);
	console_write("PMM free after heap growth: ");
	format_u64_decimal(pmm_free_pages());
	console_write("\r\n");
	console_write("Allocated:\r\n");
	print_pointer("  1 byte   -> ", one);
	print_pointer("  32 bytes -> ", small);
	print_pointer("  100 bytes -> ", medium);
	print_pointer("  1000 bytes -> ", large);
	print_pointer("  4000 bytes -> ", growth);

	if (one == NULL || small == NULL || medium == NULL || large == NULL ||
	    growth == NULL ||
	    (((u64)(unsigned long)one | (u64)(unsigned long)small |
	      (u64)(unsigned long)medium | (u64)(unsigned long)large |
	      (u64)(unsigned long)growth) & 15ULL) != 0ULL) {
		panic("heap allocation or alignment test failed");
	}
	fill_pattern(one, 1ULL, 0x11U);
	fill_pattern(small, 32ULL, 0x22U);
	fill_pattern(medium, 100ULL, 0x33U);
	fill_pattern(large, 1000ULL, 0x44U);
	fill_pattern(growth, 4000ULL, 0x55U);
	check_pattern(one, 1ULL, 0x11U);
	check_pattern(small, 32ULL, 0x22U);
	check_pattern(medium, 100ULL, 0x33U);
	check_pattern(large, 1000ULL, 0x44U);
	check_pattern(growth, 4000ULL, 0x55U);
	console_write("Patterns verified.\r\n");

	kfree(medium);
	console_write("Freed block: ");
	format_u64_hex((u64)(unsigned long)medium);
	console_write("\r\nAllocated again: ");
	reused = (unsigned char *)kmalloc(80ULL);
	if (reused == NULL || reused != medium) {
		panic("heap free block was not reused");
	}
	format_u64_hex((u64)(unsigned long)reused);
	console_write("\r\n");

	kfree(NULL);
	kfree(one);
	kfree(small);
	kfree(reused);
	kfree(large);
	kfree(growth);
	console_write("Heap reserved: ");
	format_u64_decimal(heap_reserved_bytes());
	console_write(" bytes\r\nHeap allocated: ");
	format_u64_decimal(heap_allocated_bytes());
	console_write(" bytes\r\nHeap reusable: ");
	format_u64_decimal(heap_reusable_bytes());
	console_write(" bytes\r\nHeap test complete.\r\n");
}
#endif

#if NIMERA_MMU_TEST
static void mmu_test(void)
{
	unsigned char *memory = (unsigned char *)kmalloc(64ULL);

	console_write("Nimera MMU test\r\nSCTLR before: ");
	format_u64_hex(mmu_initial_sctlr());
	console_write("\r\nPage tables built: ");
	format_u64_decimal(mmu_page_table_pages());
	console_write("\r\nSCTLR after: ");
	format_u64_hex(mmu_current_sctlr());
	console_write("\r\nMMU after: ");
	console_write(mmu_enabled() != 0ULL ? "enabled\r\n" : "disabled\r\n");
	if (memory == NULL) {
		panic("MMU RAM access test allocation failed");
	}
	for (u64 index = 0ULL; index < 64ULL; ++index) {
		memory[index] = (unsigned char)(index ^ 0xa5U);
	}
	for (u64 index = 0ULL; index < 64ULL; ++index) {
		if (memory[index] != (unsigned char)(index ^ 0xa5U)) {
			panic("MMU RAM access test failed");
		}
	}
	console_write("RAM access: OK\r\nUART access: OK\r\n");
	(void)timer_uptime_ms();
	console_write("Timer access: OK\r\nMMU test complete.\r\n");
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

#if NIMERA_MMU_TEST
	console_write("MMU before: ");
	console_write(mmu_enabled() != 0ULL ? "enabled\r\n" : "disabled\r\n");
#endif
	mmu_init(&map);
	heap_init();

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

#if NIMERA_HEAP_TEST
	heap_test();
	return;
#endif

#if NIMERA_MMU_FAULT_TEST
	console_write("Nimera MMU fault test\r\n");
	mmu_fault_test();
#endif

#if NIMERA_MMU_TEST
	mmu_test();
	return;
#endif

#if NIMERA_EXCEPTION_TEST
	console_write("Nimera exception test\r\nCurrentEL: ");
	format_u64_decimal(exception_current_el());
	console_write("\r\n");
	exception_test_trigger();
#endif

	console_write("Nimera booting...\r\n");
	console_write("MMU: enabled\r\n");
	console_write("kernel: starting shell\r\n");
	shell_run();
}
