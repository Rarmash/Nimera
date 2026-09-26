#include <nimera/memory.h>
#include <nimera/panic.h>

typedef unsigned char u8;
typedef unsigned int u32;

#define QEMU_VIRT_DTB_ADDRESS 0x40000000ULL
#define FDT_HEADER_SIZE 40U
#define FDT_MAX_RESERVATIONS NIMERA_MEMORY_MAX_RESERVED_RANGES

#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROP 3U
#define FDT_NOP 4U
#define FDT_END 9U

extern char __kernel_start;
extern char __kernel_end;

static u32 read_be32(const u8 *address)
{
	return ((u32)address[0] << 24) |
	       ((u32)address[1] << 16) |
	       ((u32)address[2] << 8) |
	       (u32)address[3];
}

static u64 read_be64(const u8 *address)
{
	return ((u64)read_be32(address) << 32) | (u64)read_be32(address + 4U);
}

static u64 checked_end(struct memory_range range)
{
	if (range.size > ~0ULL - range.base) {
		panic("memory range overflows address space");
	}
	return range.base + range.size;
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
	if (value > 0xfffffffcu) {
		panic("Device Tree alignment overflows");
	}
	return (value + 3U) & ~3U;
}

static void add_reserved(struct memory_map *map, struct memory_range range)
{
	u64 physical_end = checked_end(map->physical);
	u64 range_end;
	unsigned int index;

	if (range.size == 0ULL) {
		return;
	}
	range_end = checked_end(range);
	if (range_end <= map->physical.base || range.base >= physical_end) {
		return;
	}
	if (range.base < map->physical.base) {
		range.base = map->physical.base;
	}
	if (range_end > physical_end) {
		range_end = physical_end;
	}
	range.size = range_end - range.base;

	if (map->reserved_count == NIMERA_MEMORY_MAX_RESERVED_RANGES) {
		panic("too many reserved memory ranges");
	}
	index = map->reserved_count++;
	while (index != 0U && map->reserved[index - 1U].base > range.base) {
		map->reserved[index] = map->reserved[index - 1U];
		--index;
	}
	map->reserved[index] = range;
}

static void merge_reserved(struct memory_map *map)
{
	unsigned int index = 0U;

	while (index + 1U < map->reserved_count) {
		struct memory_range *left = &map->reserved[index];
		struct memory_range *right = &map->reserved[index + 1U];
		u64 left_end = checked_end(*left);

		if (left_end < right->base) {
			++index;
			continue;
		}
		if (checked_end(*right) > left_end) {
			left->size = checked_end(*right) - left->base;
		}
		for (unsigned int move = index + 1U;
		     move + 1U < map->reserved_count; ++move) {
			map->reserved[move] = map->reserved[move + 1U];
		}
		--map->reserved_count;
	}
}

static void calculate_totals(struct memory_map *map)
{
	u64 cursor = map->physical.base;
	u64 physical_end = checked_end(map->physical);

	map->reserved_size = 0ULL;
	for (unsigned int index = 0U; index < map->reserved_count; ++index) {
		map->reserved_size += map->reserved[index].size;
	}

	map->usable_count = 0U;
	map->usable_size = 0ULL;
	for (unsigned int index = 0U; index < map->reserved_count; ++index) {
		struct memory_range reserved = map->reserved[index];

		if (cursor < reserved.base) {
			if (map->usable_count == NIMERA_MEMORY_MAX_USABLE_RANGES) {
				panic("too many usable memory ranges");
			}
			map->usable[map->usable_count++] =
				(struct memory_range){cursor, reserved.base - cursor};
		}
		if (checked_end(reserved) > cursor) {
			cursor = checked_end(reserved);
		}
	}
	if (cursor < physical_end) {
		if (map->usable_count == NIMERA_MEMORY_MAX_USABLE_RANGES) {
			panic("too many usable memory ranges");
		}
		map->usable[map->usable_count++] =
			(struct memory_range){cursor, physical_end - cursor};
	}
	for (unsigned int index = 0U; index < map->usable_count; ++index) {
		map->usable_size += map->usable[index].size;
	}
}

struct memory_map platform_memory_discover(void)
{
	const u8 *dtb = (const u8 *)(unsigned long)QEMU_VIRT_DTB_ADDRESS;
	struct memory_map map;
	struct memory_range dtb_reservations[FDT_MAX_RESERVATIONS];
	u64 kernel_end;
	u32 dtb_reservation_count = 0U;
	u32 total_size;
	u32 structure_offset;
	u32 structure_size;
	u32 strings_offset;
	u32 strings_size;
	u32 reservation_offset;
	const u8 *structure;
	const u8 *structure_end;
	const u8 *strings;
	u32 address_cells = 0U;
	u32 size_cells = 0U;
	u32 depth = 0U;
	u32 memory_depth = 0U;
	unsigned int found_end = 0U;

	map.physical = (struct memory_range){0ULL, 0ULL};
	map.kernel = (struct memory_range){0ULL, 0ULL};
	map.dtb = (struct memory_range){0ULL, 0ULL};
	map.reserved_count = 0U;
	map.reserved_size = 0ULL;
	map.usable_count = 0U;
	map.usable_size = 0ULL;

	if (read_be32(dtb) != 0xd00dfeedU) {
		panic("invalid Device Tree magic");
	}
	total_size = read_be32(dtb + 4U);
	structure_offset = read_be32(dtb + 8U);
	strings_offset = read_be32(dtb + 12U);
	reservation_offset = read_be32(dtb + 16U);
	strings_size = read_be32(dtb + 32U);
	structure_size = read_be32(dtb + 36U);
	if (total_size < FDT_HEADER_SIZE ||
	    structure_offset > total_size ||
	    structure_size > total_size - structure_offset ||
	    strings_offset > total_size ||
	    strings_size > total_size - strings_offset ||
	    reservation_offset > total_size ||
	    total_size - reservation_offset < 16U) {
		panic("invalid Device Tree bounds");
	}

	map.kernel.base = (u64)(unsigned long)&__kernel_start;
	kernel_end = (u64)(unsigned long)&__kernel_end;
	if (kernel_end < map.kernel.base) {
		panic("invalid kernel linker boundaries");
	}
	map.kernel.size = kernel_end - map.kernel.base;
	map.dtb = (struct memory_range){QEMU_VIRT_DTB_ADDRESS, total_size};
	checked_end(map.kernel);
	checked_end(map.dtb);

	for (const u8 *cursor = dtb + reservation_offset;; cursor += 16U) {
		u64 base;
		u64 size;

		if ((u32)(dtb + total_size - cursor) < 16U) {
			panic("unterminated Device Tree reservation map");
		}
		base = read_be64(cursor);
		size = read_be64(cursor + 8U);
		if (base == 0ULL && size == 0ULL) {
			break;
		}
		if (dtb_reservation_count == FDT_MAX_RESERVATIONS) {
			panic("too many Device Tree reservations");
		}
		dtb_reservations[dtb_reservation_count++] =
			(struct memory_range){base, size};
	}

	structure = dtb + structure_offset;
	structure_end = structure + structure_size;
	strings = dtb + strings_offset;

	for (const u8 *cursor = structure; cursor < structure_end;) {
		u32 token;

		if ((u32)(structure_end - cursor) < 4U) {
			panic("truncated Device Tree token");
		}
		token = read_be32(cursor);
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
			} else if (depth == 1U &&
				   string_equals(property_name, "#size-cells")) {
				if (length != 4U) {
					panic("invalid Device Tree size cells");
				}
				size_cells = read_be32(value);
			} else if (depth == memory_depth &&
				   string_equals(property_name, "reg")) {
				u32 tuple_cells = address_cells + size_cells;

				if (address_cells == 0U || size_cells == 0U ||
				    address_cells > 2U || size_cells > 2U ||
				    tuple_cells > 4U || length < tuple_cells * 4U) {
					panic("invalid Device Tree memory reg");
				}
				map.physical.base = read_cells(value, address_cells);
				map.physical.size = read_cells(value + address_cells * 4U,
							       size_cells);
				checked_end(map.physical);
			}
			break;
		}
		case FDT_NOP:
			break;
		case FDT_END:
			if (depth != 0U) {
				panic("unbalanced Device Tree structure");
			}
			found_end = 1U;
			break;
		default:
			panic("unknown Device Tree token");
		}
		if (found_end) {
			break;
		}
	}

	if (!found_end || map.physical.size == 0ULL) {
		panic("Device Tree memory node not found");
	}
	add_reserved(&map, map.kernel);
	add_reserved(&map, map.dtb);
	for (u32 index = 0U; index < dtb_reservation_count; ++index) {
		add_reserved(&map, dtb_reservations[index]);
	}
	merge_reserved(&map);
	calculate_totals(&map);
	return map;
}
