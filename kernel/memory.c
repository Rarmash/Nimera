#include <nimera/console.h>
#include <nimera/format.h>
#include <nimera/memory.h>

extern struct memory_map platform_memory_discover(void);

struct memory_map memory_discover(void)
{
	return platform_memory_discover();
}

static void print_range(const char *label, struct memory_range range)
{
	console_write(label);
	format_u64_hex(range.base);
	console_write(" - ");
	format_u64_hex(range.base + range.size);
	console_write("\r\n");
}

void memory_print_map(const struct memory_map *map)
{
	console_write("Physical memory: ");
	format_u64_decimal(map->physical.size / (1024ULL * 1024ULL));
	console_write(" MiB\r\nReserved memory: ");
	format_u64_decimal(map->reserved_size / 1024ULL);
	console_write(" KiB\r\nUsable memory: ");
	format_u64_decimal(map->usable_size / (1024ULL * 1024ULL));
	console_write(" MiB\r\n");
	print_range("Kernel range: ", map->kernel);
	print_range("DTB range: ", map->dtb);
	console_write("Reserved ranges:\r\n");
	for (unsigned int index = 0U; index < map->reserved_count; ++index) {
		print_range("  ", map->reserved[index]);
	}
}
