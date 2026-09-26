#include <nimera/console.h>
#include <nimera/format.h>
#include <nimera/irq.h>
#include <nimera/memory.h>
#include <nimera/mmu.h>
#include <nimera/pmm.h>
#include <nimera/shell.h>
#include <nimera/timer.h>
#include <nimera/version.h>

#define SHELL_LINE_CAPACITY 128U

static unsigned int text_equals(const char *left, const char *right)
{
	unsigned int index = 0U;

	while (left[index] != '\0' && right[index] != '\0') {
		if (left[index] != right[index]) {
			return 0U;
		}
		++index;
	}

	return left[index] == '\0' && right[index] == '\0';
}

static unsigned int starts_echo(const char *line, unsigned int length)
{
	return length >= 4U && line[0] == 'e' && line[1] == 'c' &&
	       line[2] == 'h' && line[3] == 'o' &&
	       (length == 4U || line[4] == ' ' || line[4] == '\t');
}

static void shell_help(void)
{
	console_write("Available commands:\r\n");
	console_write("  help\r\n");
	console_write("  echo [text]\r\n");
	console_write("  uptime\r\n");
	console_write("  ticks\r\n");
	console_write("  mem\r\n");
	console_write("  version\r\n");
}

static void shell_uptime(void)
{
	u64 uptime = timer_uptime_ms();
	u64 milliseconds = uptime % 1000ULL;

	console_write("Uptime: ");
	format_u64_decimal(uptime / 1000ULL);
	console_putc('.');
	console_putc((char)('0' + (milliseconds / 100ULL)));
	console_putc((char)('0' + ((milliseconds / 10ULL) % 10ULL)));
	console_putc((char)('0' + (milliseconds % 10ULL)));
	console_write(" s\r\n");
}

static void shell_ticks(void)
{
	console_write("Timer IRQ ticks: ");
	format_u64_decimal(irq_timer_ticks());
	console_write("\r\n");
}

static void shell_memory(void)
{
	struct memory_map map = memory_discover();

	memory_print_map(&map);
	console_write("Page size: ");
	format_u64_decimal(NIMERA_PAGE_SIZE);
	console_write(" bytes\r\nManaged pages: ");
	format_u64_decimal(pmm_total_pages());
	console_write("\r\nPMM metadata: ");
	format_u64_decimal(pmm_metadata_pages());
	console_write(" page(s), ");
	format_u64_decimal(pmm_bitmap_bytes());
	console_write(" bytes\r\nFree pages: ");
	format_u64_decimal(pmm_free_pages());
	console_write("\r\nAllocated pages: ");
	format_u64_decimal(pmm_used_pages());
	console_write("\r\nFree memory: ");
	format_u64_decimal(pmm_free_pages() * NIMERA_PAGE_SIZE / 1024ULL);
	console_write(" KiB\r\nMMU: ");
	console_write(mmu_enabled() != 0ULL ? "enabled\r\n" : "disabled\r\n");
	console_write("Page-table pages: ");
	format_u64_decimal(mmu_page_table_pages());
	console_write("\r\nMMU L3 tables: ");
	format_u64_decimal(mmu_l3_table_pages());
	console_write("\r\nKernel text: RO+X\r\nKernel rodata: RO+NX\r\n");
	console_write("Kernel data: RW+NX\r\n");
}

static void shell_execute(char *line, unsigned int length)
{
	unsigned int index = 0U;

	while (index < length && (line[index] == ' ' || line[index] == '\t')) {
		++index;
	}
	line += index;
	length -= index;

	while (length != 0U && (line[length - 1U] == ' ' ||
					line[length - 1U] == '\t')) {
		line[--length] = '\0';
	}

	if (text_equals(line, "help")) {
		shell_help();
	} else if (starts_echo(line, length)) {
		unsigned int argument = 4U;

		while (argument < length &&
		       (line[argument] == ' ' || line[argument] == '\t')) {
			++argument;
		}
		console_write(line + argument);
		console_write("\r\n");
	} else if (text_equals(line, "uptime")) {
		shell_uptime();
	} else if (text_equals(line, "ticks")) {
		shell_ticks();
	} else if (text_equals(line, "mem")) {
		shell_memory();
	} else if (text_equals(line, "version")) {
		console_write(NIMERA_VERSION "\r\n");
	} else if (length != 0U) {
		console_write("Unknown command: ");
		console_write(line);
		console_write("\r\n");
	}
}

__attribute__((noreturn))
void shell_run(void)
{
	char line[SHELL_LINE_CAPACITY];
	unsigned int swallow_lf = 0U;

	for (;;) {
		unsigned int length = 0U;

		console_write("nimera $ ");
		for (;;) {
			char c = console_getc();

			if (c == '\n' && swallow_lf != 0U) {
				swallow_lf = 0U;
				continue;
			}
			if (c == '\r' || c == '\n') {
				console_write("\r\n");
				swallow_lf = c == '\r';
				line[length] = '\0';
				shell_execute(line, length);
				break;
			}
			if (c == '\b' || c == 127) {
				if (length != 0U) {
					--length;
					console_write("\b \b");
				}
				continue;
			}
			if (c >= 32 && c <= 126 && length < SHELL_LINE_CAPACITY - 1U) {
				line[length++] = c;
				console_putc(c);
			}
		}
	}
}
