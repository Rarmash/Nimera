// No libc, allocator, or runtime are needed for this milestone.

#include <nimera/console.h>
#include <nimera/exception.h>
#include <nimera/format.h>
#include <nimera/heap.h>
#include <nimera/irq.h>
#include <nimera/memory.h>
#include <nimera/mmu.h>
#include <nimera/panic.h>
#include <nimera/pmm.h>
#include <nimera/shell.h>
#include <nimera/scheduler.h>
#include <nimera/timer.h>
#include <nimera/vfs.h>

#define NULL ((void *)0)

extern int uart_overflow_test(void);

#if NIMERA_PROTECTION_TEST
static volatile u64 protection_data = 0x4e696d657261ULL;
#endif

#if NIMERA_UART_IRQ_TEST
static void uart_irq_test(void)
{
	struct irq_platform_info info = irq_platform_discover();
	char received[6];
	unsigned int index;

	console_write("Nimera UART IRQ test\r\nUART base: ");
	format_u64_hex(info.uart_base);
	console_write("\r\nUART INTID: ");
	format_u64_decimal(info.uart_intid);
	console_write("\r\n");
	console_write("RX IRQ enabled\r\nType 5 characters:\r\n");
	for (index = 0U; index < 5U; ++index) {
		received[index] = console_getc();
	}
	received[5] = '\0';
	irq_disable();
	arch_timer_irq_stop();
	console_write("\r\nReceived via IRQ: ");
	console_write(received);
	console_write("\r\nUART RX IRQs: ");
	format_u64_decimal(irq_uart_count());
	console_write("\r\nDropped bytes: ");
	format_u64_decimal(irq_uart_dropped_bytes());
	console_write("\r\nUART IRQ test complete.\r\n");
}
#endif

#if NIMERA_UART_OVERFLOW_TEST
static void uart_ring_overflow_test(void)
{
	console_write("Nimera UART ring overflow test\r\n");
	if (uart_overflow_test() != 0) {
		panic("UART ring overflow test failed");
	}
	console_write("Unread bytes preserved: yes\r\nDropped bytes: ");
	format_u64_decimal(irq_uart_dropped_bytes());
	console_write("\r\nUART ring overflow test complete.\r\n");
}
#endif

#if NIMERA_VFS_TEST
static int vfs_name_is(const struct vfs_node *node, const char *name)
{
	unsigned int index = 0U;

	while (vfs_node_name(node)[index] != '\0' && name[index] != '\0') {
		if (vfs_node_name(node)[index] != name[index]) {
			return 0;
		}
		++index;
	}
	return vfs_node_name(node)[index] == '\0' && name[index] == '\0';
}

static void vfs_test(u64 heap_before)
{
	static const char *directories[] = {
		"system", "apps", "users", "volumes", "devices", "config",
		"var", "tmp"
	};
	struct vfs_node *root = vfs_root();
	struct vfs_node *node;
	char contents[64];
	u64 size;

	console_write("Nimera VFS test\r\nRoot mount: RAMFS\r\n\r\n");
	console_write("Root directories:\r\n");
	for (unsigned int index = 0U; index < 8U; ++index) {
		if (vfs_lookup(root, directories[index], &node) != VFS_OK ||
		    vfs_node_type(node) != VFS_NODE_DIRECTORY) {
			panic("VFS root directory test failed");
		}
		console_write("  ");
		console_write(vfs_node_name(node));
		console_write("\r\n");
	}
	if (vfs_resolve(root, "/system/version", &node) != VFS_OK ||
	    vfs_node_type(node) != VFS_NODE_FILE ||
	    vfs_resolve(root, "system", &node) != VFS_OK ||
	    vfs_resolve(node, "../apps", &node) != VFS_OK ||
	    !vfs_name_is(node, "apps")) {
		panic("VFS path resolution test failed");
	}
	if (vfs_resolve(root, ".", &node) != VFS_OK || node != root ||
	    vfs_resolve(root, "..", &node) != VFS_OK || node != root) {
		panic("VFS dot test failed");
	}
	console_write("Path resolution: OK\r\n");
	if (vfs_mkdir(root, "tmp/vfs-test", &node) != VFS_OK ||
	    vfs_resolve(root, "./tmp/vfs-test", &node) != VFS_OK ||
	    vfs_mkdir(root, "tmp/vfs-test", (struct vfs_node **)0) !=
		VFS_ALREADY_EXISTS ||
	    vfs_resolve(root, "/missing", &node) != VFS_NOT_FOUND) {
		panic("VFS mkdir or lookup test failed");
	}
	console_write("Relative paths: OK\r\nDot/dot-dot: OK\r\n");
	console_write("mkdir: OK\r\nDuplicate mkdir guard: OK\r\n");
	if (vfs_resolve(root, "/system/version", &node) != VFS_OK ||
	    vfs_read(node, contents, sizeof(contents), &size) != VFS_OK ||
	    size != 14ULL) {
		panic("VFS file read test failed");
	}
	console_write("File read: OK\r\n\r\n/system/version:\r\n");
	for (u64 index = 0ULL; index < size; ++index) {
		console_putc(contents[index]);
	}
	console_write("\r\n\r\nHeap allocated before filesystem: ");
	format_u64_decimal(heap_before);
	console_write(" bytes\r\nHeap allocated after filesystem: ");
	format_u64_decimal(heap_allocated_bytes());
	console_write(" bytes\r\nVFS test complete.\r\n");
}
#endif

#if NIMERA_VFS_WRITE_TEST
static int vfs_bytes_equal(const char *left, u64 left_size,
				   const char *right, u64 right_size)
{
	if (left_size != right_size) {
		return 0;
	}
	for (u64 index = 0ULL; index < left_size; ++index) {
		if (left[index] != right[index]) {
			return 0;
		}
	}
	return 1;
}

static void vfs_write_test(u64 heap_before)
{
	struct vfs_node *root = vfs_root();
	struct vfs_node *node;
	char contents[64];
	u64 size;
	static const char hello[] = "Hello Nimera";
	static const char world[] = "world";
	static const char suffix[] = "!";

	console_write("Nimera VFS write test\r\nHeap allocated before test: ");
	format_u64_decimal(heap_before);
	console_write(" bytes\r\n");
	if (vfs_touch(root, "/tmp/write-test.txt", &node) != VFS_OK ||
		vfs_read(node, contents, sizeof(contents), &size) != VFS_OK ||
		size != 0ULL) {
		panic("VFS touch test failed");
	}
	console_write("create empty file: OK\r\n");
	if (vfs_write(root, "/tmp/write-test.txt", hello,
			      sizeof(hello) - 1ULL, &node) != VFS_OK ||
		vfs_read(node, contents, sizeof(contents), &size) != VFS_OK ||
		!vfs_bytes_equal(contents, size, hello, sizeof(hello) - 1ULL)) {
		panic("VFS write/read test failed");
	}
	if (vfs_write(root, "/tmp/write-test.txt", world,
			      sizeof(world) - 1ULL, &node) != VFS_OK ||
		vfs_append(root, "/tmp/write-test.txt", suffix,
			       sizeof(suffix) - 1ULL, &node) != VFS_OK ||
		vfs_read(node, contents, sizeof(contents), &size) != VFS_OK ||
		!vfs_bytes_equal(contents, size, "world!", 6ULL)) {
		panic("VFS overwrite/append test failed");
	}
	console_write("write/read/overwrite/append: OK\r\nHeap allocated after create/write: ");
	format_u64_decimal(heap_allocated_bytes());
	console_write(" bytes\r\n");
	if (vfs_write(root, "/tmp/auto.txt", "auto", 4ULL, &node) != VFS_OK ||
		vfs_touch(root, "/tmp/auto.txt", &node) != VFS_OK ||
		vfs_rename(root, "/tmp/write-test.txt", "/tmp/renamed.txt") != VFS_OK ||
		vfs_touch(root, "/tmp/destination.txt", &node) != VFS_OK ||
		vfs_rename(root, "/tmp/renamed.txt", "/tmp/destination.txt") !=
			VFS_ALREADY_EXISTS) {
		panic("VFS rename or duplicate destination test failed");
	}
	console_write("rename and duplicate destination guard: OK\r\n");
	if (vfs_mkdir(root, "/tmp/move-dir", &node) != VFS_OK ||
		vfs_mkdir(root, "/tmp/move-dir/child", &node) != VFS_OK ||
		vfs_remove(node, ".") != VFS_BUSY ||
		vfs_rmdir(node, "..") != VFS_BUSY ||
		vfs_rename(root, "/tmp/move-dir", "/tmp/move-dir/child/loop") !=
			VFS_INVALID_PATH ||
		vfs_rename(root, "/tmp/renamed.txt", "/users/moved.txt") != VFS_OK) {
		panic("VFS move or cycle guard test failed");
	}
	console_write("move across directories and cycle guard: OK\r\n");
	if (vfs_rmdir(root, "/tmp/move-dir") != VFS_NOT_EMPTY) {
		panic("VFS non-empty rmdir test failed");
	}
	if (vfs_remove(root, "/") != VFS_INVALID_PATH) {
		panic("VFS root remove test failed");
	}
	if (vfs_remove(root, "/users/moved.txt") != VFS_OK) {
		panic("VFS moved file remove test failed");
	}
	if (vfs_remove(root, "/tmp/destination.txt") != VFS_OK) {
		panic("VFS destination remove test failed");
	}
	if (vfs_remove(root, "/tmp/auto.txt") != VFS_OK) {
		panic("VFS auto file remove test failed");
	}
	if (vfs_rmdir(root, "/tmp/move-dir/child") != VFS_OK ||
		vfs_rmdir(root, "/tmp/move-dir") != VFS_OK) {
		panic("VFS directory cleanup test failed");
	}
	console_write("remove/rmdir/root guard: OK\r\nHeap allocated after cleanup: ");
	format_u64_decimal(heap_allocated_bytes());
	console_write(" bytes\r\nVFS write test complete.\r\n");
}
#endif

#if NIMERA_BLOCKING_TEST
static void blocking_test(void)
{
	u64 worker_before = scheduler_worker_counter();
	u64 switches_before = scheduler_context_switches();
	char received;

	console_write("Nimera blocking test\r\n");
	console_write("Shell state before wait: RUNNING\r\n");
	console_write("Waiting for one character...\r\n");
	received = console_getc();
	console_write("Received: ");
	console_putc(received);
	console_write("\r\nShell wakeup: ");
	console_write(scheduler_thread(0U)->state == THREAD_RUNNING ?
		"OK\r\n" : "FAILED\r\n");
	console_write("Worker progressed while shell slept: ");
	console_write(scheduler_worker_counter() > worker_before &&
		scheduler_worker_saw_shell_waiting() != 0 ? "yes\r\n" : "no\r\n");
	if (scheduler_worker_counter() <= worker_before ||
	    scheduler_context_switches() == switches_before ||
	    scheduler_worker_saw_shell_waiting() == 0) {
		panic("scheduler blocking test failed");
	}
	console_write("Blocking test complete.\r\n");
}
#endif

#if NIMERA_IRQ_TEST
static void irq_test(void)
{
	struct irq_platform_info info = irq_platform_discover();

	console_write("Nimera IRQ test\r\nGIC: v2\r\nDistributor: ");
	format_u64_hex(info.gic_distributor_base);
	console_write("\r\nCPU interface: ");
	format_u64_hex(info.gic_cpu_base);
	console_write("\r\nEL1 physical timer INTID: ");
	format_u64_decimal(info.timer_intid);
	console_write("\r\n");
	irq_init();
	irq_enable();
	console_write("IRQs enabled\r\n");
	while (irq_timer_ticks() < 5ULL) {
		arch_wait_for_event();
	}
	console_write("Timer IRQ ticks: ");
	format_u64_decimal(irq_timer_ticks());
	console_write("\r\n");
	irq_disable();
	arch_timer_irq_stop();
	console_write("IRQ test complete.\r\n");
}
#endif

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
	u64 heap_before_filesystem = heap_allocated_bytes();
	vfs_init();
	u64 heap_after_filesystem = heap_allocated_bytes();
	(void)heap_after_filesystem;

#if NIMERA_VFS_TEST
	vfs_test(heap_before_filesystem);
	return;
#endif
#if NIMERA_VFS_WRITE_TEST
	vfs_write_test(heap_after_filesystem);
	return;
#endif
	(void)heap_before_filesystem;

#if NIMERA_IRQ_TEST
	irq_test();
	return;
#endif
	irq_init();
	scheduler_init();
	irq_enable();

#if NIMERA_SCHED_TEST
	scheduler_test();
	return;
#endif

#if NIMERA_BLOCKING_TEST
	blocking_test();
	return;
#endif

#if NIMERA_UART_IRQ_TEST
	uart_irq_test();
	return;
#endif

#if NIMERA_UART_OVERFLOW_TEST
	uart_ring_overflow_test();
	return;
#endif

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

#if NIMERA_PROTECTION_WRITE_TEST
	console_write("Nimera write-to-text test\r\n");
	mmu_write_text_test();
#endif

#if NIMERA_PROTECTION_EXEC_TEST
	console_write("Nimera execute-from-data test\r\n");
	mmu_execute_data_test();
#endif

#if NIMERA_PROTECTION_TEST
	{
		void *heap = kmalloc(16ULL);

		if (heap == NULL || protection_data == 0ULL ||
		    mmu_validate_protections(heap) != 0) {
			panic("MMU protection validation failed");
		}
		console_write("Nimera protection test\r\n");
		console_write("L3 tables after section splitting: ");
		format_u64_decimal(mmu_l3_table_pages());
		console_write("\r\n");
		console_write(".text: RO + executable\r\n");
		console_write(".rodata: RO + NX\r\n");
		console_write(".data: RW + NX\r\n");
		console_write("heap: RW + NX\r\n");
		console_write("UART: Device + NX\r\n");
		console_write("Protection test complete.\r\n");
	}
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
	console_write("irq: enabled\r\n");
	console_write("sched: enabled\r\n");
	console_write("fs: root mounted\r\n");
	console_write("kernel: starting shell\r\n");
	shell_run();
}
