#include <nimera/panic.h>
#include <nimera/pmm.h>

struct pmm_range {
	u64 base;
	u64 pages;
};

static struct pmm_range managed_ranges[NIMERA_MEMORY_MAX_USABLE_RANGES];
static unsigned int managed_range_count;
static unsigned char *page_bitmap;
static u64 bitmap_size;
static u64 metadata_page_count;
static u64 total_page_count;
static u64 free_page_count;
static unsigned int initialized;

static u64 align_up(u64 value)
{
	if (value > ~0ULL - (NIMERA_PAGE_SIZE - 1ULL)) {
		panic("page alignment overflows address space");
	}
	return (value + NIMERA_PAGE_SIZE - 1ULL) &
	       ~(NIMERA_PAGE_SIZE - 1ULL);
}

static u64 align_down(u64 value)
{
	return value & ~(NIMERA_PAGE_SIZE - 1ULL);
}

static u64 range_end(struct memory_range range)
{
	if (range.size > ~0ULL - range.base) {
		panic("PMM range overflows address space");
	}
	return range.base + range.size;
}

static u64 bitmap_bytes_for(u64 page_count)
{
	if (page_count > ~0ULL - 7ULL) {
		panic("PMM bitmap size overflows");
	}
	return (page_count + 7ULL) / 8ULL;
}

static u64 metadata_pages_for(u64 page_count)
{
	u64 bytes = bitmap_bytes_for(page_count);

	if (bytes > ~0ULL - (NIMERA_PAGE_SIZE - 1ULL)) {
		panic("PMM metadata size overflows");
	}
	return (bytes + NIMERA_PAGE_SIZE - 1ULL) / NIMERA_PAGE_SIZE;
}

static u64 page_count_for(const struct memory_range *range)
{
	u64 start = align_up(range->base);
	u64 end = align_down(range_end(*range));

	if (end <= start) {
		return 0ULL;
	}
	return (end - start) / NIMERA_PAGE_SIZE;
}

static u64 page_to_bit(u64 physical_address)
{
	u64 bit_index = 0ULL;

	for (unsigned int index = 0U; index < managed_range_count; ++index) {
		struct pmm_range range = managed_ranges[index];
		u64 end = range.base + range.pages * NIMERA_PAGE_SIZE;

		if (physical_address >= range.base && physical_address < end) {
			return bit_index +
			       (physical_address - range.base) / NIMERA_PAGE_SIZE;
		}
		bit_index += range.pages;
	}
	panic("physical page is not managed by PMM");
}

static u64 bit_to_page(u64 bit_index)
{
	for (unsigned int index = 0U; index < managed_range_count; ++index) {
		if (bit_index < managed_ranges[index].pages) {
			return managed_ranges[index].base +
			       bit_index * NIMERA_PAGE_SIZE;
		}
		bit_index -= managed_ranges[index].pages;
	}
	panic("PMM bitmap index is out of range");
}

void pmm_init(const struct memory_map *map)
{
	u64 candidate_pages = 0ULL;
	u64 required_metadata_pages;
	unsigned int metadata_range = NIMERA_MEMORY_MAX_USABLE_RANGES;

	if (initialized != 0U) {
		panic("PMM initialized twice");
	}
	managed_range_count = 0U;
	for (unsigned int index = 0U; index < map->usable_count; ++index) {
		u64 pages = page_count_for(&map->usable[index]);

		if (pages == 0ULL) {
			continue;
		}
		if (managed_range_count == NIMERA_MEMORY_MAX_USABLE_RANGES ||
		    candidate_pages > ~0ULL - pages) {
			panic("too many PMM usable pages");
		}
		managed_ranges[managed_range_count].base =
			align_up(map->usable[index].base);
		managed_ranges[managed_range_count].pages = pages;
		candidate_pages += pages;
		++managed_range_count;
	}
	if (candidate_pages == 0ULL) {
		panic("no page-aligned usable memory");
	}

	/* Size metadata from the real usable RAM, not from a fixed RAM ceiling. */
	required_metadata_pages = metadata_pages_for(candidate_pages);
	for (unsigned int index = 0U; index < managed_range_count; ++index) {
		if (managed_ranges[index].pages >= required_metadata_pages) {
			metadata_range = index;
			break;
		}
	}
	if (metadata_range == NIMERA_MEMORY_MAX_USABLE_RANGES) {
		panic("usable memory cannot hold PMM bitmap");
	}

	metadata_page_count = required_metadata_pages;
	page_bitmap = (unsigned char *)(unsigned long)managed_ranges[metadata_range].base;
	managed_ranges[metadata_range].base +=
		required_metadata_pages * NIMERA_PAGE_SIZE;
	managed_ranges[metadata_range].pages -= required_metadata_pages;
	if (managed_ranges[metadata_range].pages == 0ULL) {
		for (unsigned int index = metadata_range;
		     index + 1U < managed_range_count; ++index) {
			managed_ranges[index] = managed_ranges[index + 1U];
		}
		--managed_range_count;
	}

	total_page_count = 0ULL;
	for (unsigned int index = 0U; index < managed_range_count; ++index) {
		total_page_count += managed_ranges[index].pages;
	}
	if (total_page_count == 0ULL) {
		panic("PMM has no managed pages after metadata reservation");
	}
	bitmap_size = bitmap_bytes_for(total_page_count);
	if (bitmap_size > metadata_page_count * NIMERA_PAGE_SIZE) {
		panic("PMM bitmap does not fit metadata pages");
	}

	for (u64 index = 0ULL; index < bitmap_size; ++index) {
		page_bitmap[index] = 0U;
	}
	for (u64 index = total_page_count; index < bitmap_size * 8ULL; ++index) {
		page_bitmap[index / 8ULL] |= (unsigned char)(1U << (index % 8ULL));
	}
	free_page_count = total_page_count;
	initialized = 1U;
}

int pmm_alloc_page(u64 *physical_address)
{
	if (initialized == 0U) {
		panic("PMM used before initialization");
	}
	for (u64 index = 0ULL; index < total_page_count; ++index) {
		unsigned char mask = (unsigned char)(1U << (index % 8ULL));

		if ((page_bitmap[index / 8ULL] & mask) == 0U) {
			page_bitmap[index / 8ULL] |= mask;
			*physical_address = bit_to_page(index);
			--free_page_count;
			return 0;
		}
	}
	return -1;
}

void pmm_free_page(u64 physical_address)
{
	u64 bit_index;
	unsigned char mask;

	if (initialized == 0U) {
		panic("PMM used before initialization");
	}
	if ((physical_address & (NIMERA_PAGE_SIZE - 1ULL)) != 0ULL) {
		panic("PMM free address is not page aligned");
	}
	bit_index = page_to_bit(physical_address);
	mask = (unsigned char)(1U << (bit_index % 8ULL));
	if ((page_bitmap[bit_index / 8ULL] & mask) == 0U) {
		panic("PMM double free");
	}
	page_bitmap[bit_index / 8ULL] &= (unsigned char)~mask;
	++free_page_count;
}

u64 pmm_total_pages(void)
{
	return total_page_count;
}

u64 pmm_free_pages(void)
{
	return free_page_count;
}

u64 pmm_used_pages(void)
{
	return total_page_count - free_page_count;
}

u64 pmm_metadata_pages(void)
{
	return metadata_page_count;
}

u64 pmm_bitmap_bytes(void)
{
	return bitmap_size;
}
