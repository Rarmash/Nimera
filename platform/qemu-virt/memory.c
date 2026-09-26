#include <nimera/memory.h>
#include <nimera/panic.h>

typedef unsigned char u8;
typedef unsigned int u32;

#define QEMU_VIRT_DTB_ADDRESS 0x40000000ULL
#define FDT_HEADER_SIZE 40U

#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROP 3U
#define FDT_NOP 4U
#define FDT_END 9U

static u32 read_be32(const u8 *address)
{
	return ((u32)address[0] << 24) |
	       ((u32)address[1] << 16) |
	       ((u32)address[2] << 8) |
	       (u32)address[3];
}

static u64 read_cells(const u8 *data, u32 cell_count)
{
	u64 value = 0ULL;

	if (cell_count == 0U || cell_count > 2U) {
		panic("unsupported Device Tree cell count");
	}

	for (u32 index = 0U; index < cell_count; ++index) {
		value = (value << 32) | (u64)read_be32(data + index * 4U);
	}

	return value;
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

static unsigned int node_is_memory(const u8 *name, u32 length)
{
	if (length == 6U && string_equals(name, "memory")) {
		return 1U;
	}

	return length >= 7U && name[0] == 'm' && name[1] == 'e' &&
	       name[2] == 'm' && name[3] == 'o' && name[4] == 'r' &&
	       name[5] == 'y' && name[6] == '@';
}

static u32 align4(u32 value)
{
	return (value + 3U) & ~3U;
}

struct memory_info platform_memory_discover(void)
{
	const u8 *dtb = (const u8 *)(unsigned long)QEMU_VIRT_DTB_ADDRESS;
	u32 total_size;
	u32 structure_offset;
	u32 structure_size;
	u32 strings_offset;
	u32 strings_size;
	const u8 *structure;
	const u8 *structure_end;
	const u8 *strings;
	u32 address_cells = 0U;
	u32 size_cells = 0U;
	u32 depth = 0U;
	u32 memory_depth = 0U;

	if (read_be32(dtb) != 0xd00dfeedU) {
		panic("invalid Device Tree magic");
	}

	total_size = read_be32(dtb + 4U);
	if (total_size < FDT_HEADER_SIZE) {
		panic("invalid Device Tree header size");
	}
	structure_offset = read_be32(dtb + 8U);
	strings_offset = read_be32(dtb + 12U);
	structure_size = read_be32(dtb + 36U);
	if (total_size < FDT_HEADER_SIZE ||
	    structure_offset > total_size ||
	    structure_size > total_size - structure_offset ||
	    strings_offset > total_size) {
		panic("invalid Device Tree bounds");
	}
	strings_size = total_size - strings_offset;

	structure = dtb + structure_offset;
	structure_end = structure + structure_size;
	strings = dtb + strings_offset;

	for (const u8 *cursor = structure; cursor < structure_end;) {
		if ((u32)(structure_end - cursor) < 4U) {
			panic("truncated Device Tree token");
		}
		u32 token = read_be32(cursor);
		cursor += 4U;

		switch (token) {
		case FDT_BEGIN_NODE: {
			const u8 *name = cursor;
			u32 name_length = 0U;

			while (cursor < structure_end && *cursor != '\0') {
				++cursor;
				++name_length;
			}
			if (cursor >= structure_end) {
				panic("unterminated Device Tree node");
			}
			++cursor;
			cursor = structure + align4((u32)(cursor - structure));
			if (cursor > structure_end) {
				panic("invalid Device Tree node alignment");
			}

			++depth;
			if (node_is_memory(name, name_length)) {
				memory_depth = depth;
			}
			break;
		}

		case FDT_END_NODE:
			if (depth == 0U) {
				panic("invalid Device Tree node depth");
			}
			if (depth == memory_depth) {
				memory_depth = 0U;
			}
			--depth;
			break;

		case FDT_PROP: {
			u32 length;
			u32 name_offset;
			u32 padded_length;
			u32 property_name_length;
			const u8 *value;
			const u8 *property_name;

			if ((u32)(structure_end - cursor) < 8U) {
				panic("truncated Device Tree property");
			}
			length = read_be32(cursor);
			name_offset = read_be32(cursor + 4U);
			cursor += 8U;
			if (length > (u32)(structure_end - cursor)) {
				panic("invalid Device Tree property length");
			}
			value = cursor;
			padded_length = align4(length);
			if (padded_length > (u32)(structure_end - cursor) ||
			    name_offset >= strings_size) {
				panic("invalid Device Tree property bounds");
			}
			cursor += padded_length;
			property_name = strings + name_offset;
			property_name_length = 0U;
			while (name_offset + property_name_length < strings_size &&
			       property_name[property_name_length] != '\0') {
				++property_name_length;
			}
			if (name_offset + property_name_length >= strings_size) {
				panic("unterminated Device Tree property name");
			}

			if (depth == 1U && string_equals(property_name, "#address-cells")) {
				if (length != 4U) {
					panic("invalid Device Tree address cells");
				}
				address_cells = read_be32(value);
			} else if (depth == 1U && string_equals(property_name, "#size-cells")) {
				if (length != 4U) {
					panic("invalid Device Tree size cells");
				}
				size_cells = read_be32(value);
			} else if (depth == memory_depth && string_equals(property_name, "reg")) {
				struct memory_info info;
				u32 tuple_cells = address_cells + size_cells;

				if (address_cells == 0U || size_cells == 0U ||
				    address_cells > 2U || size_cells > 2U ||
				    length < tuple_cells * 4U) {
					panic("invalid Device Tree memory reg");
				}
				info.physical_base = read_cells(value, address_cells);
				info.physical_size = read_cells(value + address_cells * 4U,
								       size_cells);
				if (info.physical_size == 0ULL) {
					panic("Device Tree reports zero physical memory");
				}
				return info;
			}
			break;
		}

		case FDT_NOP:
			break;

		case FDT_END: {
			panic("Device Tree memory node not found");
		}

		default:
			panic("unknown Device Tree token");
		}
	}

	panic("unterminated Device Tree structure");
}
