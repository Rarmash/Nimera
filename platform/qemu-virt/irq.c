#include <nimera/irq.h>
#include <nimera/panic.h>

typedef unsigned char u8;
typedef unsigned int u32;

#define QEMU_VIRT_DTB_ADDRESS 0x40000000ULL
#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROP 3U
#define FDT_NOP 4U
#define FDT_END 9U

static u32 read_be32(const u8 *address)
{
	return ((u32)address[0] << 24) | ((u32)address[1] << 16) |
	       ((u32)address[2] << 8) | (u32)address[3];
}

static u64 read_cells(const u8 *data, u32 count)
{
	u64 value = 0ULL;

	if (count == 0U || count > 2U) {
		panic("unsupported IRQ Device Tree cell count");
	}
	for (u32 index = 0U; index < count; ++index) {
		value = (value << 32) | (u64)read_be32(data + index * 4U);
	}
	return value;
}

static u32 align4(u32 value)
{
	if (value > 0xfffffffcu) {
		panic("IRQ Device Tree alignment overflows");
	}
	return (value + 3U) & ~3U;
}

static unsigned int string_equals(const u8 *string, const char *expected)
{
	unsigned int index = 0U;

	while (expected[index] != '\0') {
		if (string[index] != (u8)expected[index]) {
			return 0U;
		}
		++index;
	}
	return string[index] == '\0';
}

static unsigned int name_starts_with(const u8 *name, u32 length,
					     const char *prefix)
{
	unsigned int index = 0U;

	while (prefix[index] != '\0' && index < length) {
		if (name[index] != (u8)prefix[index]) {
			return 0U;
		}
		++index;
	}
	return prefix[index] == '\0';
}

static unsigned int compatible_contains(const u8 *data, u32 length,
						const char *expected)
{
	u32 offset = 0U;

	while (offset < length) {
		const u8 *string = data + offset;
		u32 string_length = 0U;

		while (offset + string_length < length && string[string_length] != '\0') {
			++string_length;
		}
		if (offset + string_length >= length) {
			panic("unterminated IRQ compatible property");
		}
		if (string_equals(string, expected)) {
			return 1U;
		}
		offset += string_length + 1U;
	}
	return 0U;
}

struct irq_platform_info irq_platform_discover(void)
{
	const u8 *dtb = (const u8 *)(unsigned long)QEMU_VIRT_DTB_ADDRESS;
	struct irq_platform_info info = {0ULL, 0ULL, 0ULL, 0ULL, 0ULL};
	const u8 *structure;
	const u8 *structure_end;
	const u8 *strings;
	u32 total_size;
	u32 structure_offset;
	u32 structure_size;
	u32 strings_offset;
	u32 strings_size;
	u32 depth = 0U;
	u32 gic_depth = 0U;
	u32 timer_depth = 0U;
	u32 address_cells = 0U;
	u32 size_cells = 0U;
	unsigned int gic_compatible = 0U;
	unsigned int timer_compatible = 0U;
	unsigned int found_end = 0U;

	if (read_be32(dtb) != 0xd00dfeedU) {
		panic("invalid IRQ Device Tree magic");
	}
	total_size = read_be32(dtb + 4U);
	structure_offset = read_be32(dtb + 8U);
	strings_offset = read_be32(dtb + 12U);
	strings_size = read_be32(dtb + 32U);
	structure_size = read_be32(dtb + 36U);
	if (total_size < 40U || structure_offset > total_size ||
	    structure_size > total_size - structure_offset ||
	    strings_offset > total_size || strings_size > total_size - strings_offset) {
		panic("invalid IRQ Device Tree bounds");
	}
	structure = dtb + structure_offset;
	structure_end = structure + structure_size;
	strings = dtb + strings_offset;

	for (const u8 *cursor = structure; cursor < structure_end;) {
		u32 token;

		if ((u32)(structure_end - cursor) < 4U) {
			panic("truncated IRQ Device Tree token");
		}
		token = read_be32(cursor);
		cursor += 4U;
		switch (token) {
		case FDT_BEGIN_NODE: {
			const u8 *name = cursor;
			u32 length = 0U;

			while (cursor < structure_end && *cursor != '\0') {
				++cursor;
				++length;
			}
			if (cursor >= structure_end) {
				panic("unterminated IRQ Device Tree node");
			}
			++cursor;
			cursor = structure + align4((u32)(cursor - structure));
			++depth;
			if (name_starts_with(name, length, "intc@")) {
				gic_depth = depth;
				gic_compatible = 0U;
			} else if (string_equals(name, "timer")) {
				timer_depth = depth;
				timer_compatible = 0U;
			}
			break;
		}
		case FDT_END_NODE:
			if (depth == 0U) {
				panic("invalid IRQ Device Tree depth");
			}
			if (depth == gic_depth) {
				if (gic_compatible == 0U || info.gic_distributor_size == 0ULL ||
				    info.gic_cpu_size == 0ULL) {
					panic("incomplete GICv2 Device Tree node");
				}
				gic_depth = 0U;
			}
			if (depth == timer_depth) {
				if (timer_compatible == 0U || info.timer_intid == 0ULL) {
					panic("incomplete timer Device Tree node");
				}
				timer_depth = 0U;
			}
			--depth;
			break;
		case FDT_PROP: {
			u32 length;
			u32 name_offset;
			u32 padded_length;
			const u8 *value;
			const u8 *property_name;

			if ((u32)(structure_end - cursor) < 8U) {
				panic("truncated IRQ Device Tree property");
			}
			length = read_be32(cursor);
			name_offset = read_be32(cursor + 4U);
			cursor += 8U;
			if (length > (u32)(structure_end - cursor) ||
			    name_offset >= strings_size) {
				panic("invalid IRQ Device Tree property");
			}
			value = cursor;
			padded_length = align4(length);
			if (padded_length > (u32)(structure_end - cursor)) {
				panic("invalid IRQ Device Tree property alignment");
			}
			cursor += padded_length;
			property_name = strings + name_offset;

			if (depth == 1U && string_equals(property_name, "#address-cells")) {
				address_cells = read_be32(value);
			} else if (depth == 1U &&
				   string_equals(property_name, "#size-cells")) {
				size_cells = read_be32(value);
			} else if (depth == gic_depth && string_equals(property_name, "compatible")) {
				gic_compatible = compatible_contains(value, length,
									"arm,cortex-a15-gic");
			} else if (depth == gic_depth && string_equals(property_name, "reg")) {
				u32 tuple_cells = address_cells + size_cells;

				if (address_cells == 0U || size_cells == 0U ||
				    address_cells > 2U || size_cells > 2U ||
				    tuple_cells == 0U || length < tuple_cells * 8U) {
					panic("invalid GICv2 reg property");
				}
				info.gic_distributor_base = read_cells(value, address_cells);
				info.gic_distributor_size = read_cells(value + address_cells * 4U,
									 size_cells);
				info.gic_cpu_base = read_cells(value + tuple_cells * 4U,
								      address_cells);
				info.gic_cpu_size = read_cells(value + tuple_cells * 4U +
									     address_cells * 4U, size_cells);
			} else if (depth == timer_depth &&
				   string_equals(property_name, "compatible")) {
				timer_compatible = compatible_contains(value, length,
									"arm,armv8-timer");
			} else if (depth == timer_depth &&
				   string_equals(property_name, "interrupts")) {
				/* GIC specifiers are type, number, flags. The second PPI is
				 * the non-secure EL1 physical timer: PPI 14 plus 16. */
				if (length < 24U || read_be32(value) != 1U ||
				    read_be32(value + 12U) != 1U ||
				    read_be32(value + 16U) == 0U) {
					panic("invalid architected timer interrupts property");
				}
				info.timer_intid = 16ULL + (u64)read_be32(value + 16U);
			}
			break;
		}
		case FDT_NOP:
			break;
		case FDT_END:
			if (depth != 0U) {
				panic("unbalanced IRQ Device Tree structure");
			}
			cursor = structure_end;
			found_end = 1U;
			break;
		default:
			panic("unknown IRQ Device Tree token");
		}
	}

	if (found_end == 0U || info.gic_distributor_size == 0ULL ||
	    info.gic_cpu_size == 0ULL ||
	    info.timer_intid == 0ULL) {
		panic("GICv2 or timer not found in Device Tree");
	}
	return info;
}
