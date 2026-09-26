#include <nimera/mmu.h>
#include <nimera/panic.h>
#include <nimera/pmm.h>

typedef unsigned long long descriptor_t;

#define PAGE_TABLE_ENTRIES 512ULL
#define BLOCK_SIZE 0x200000ULL
#define UART_PHYSICAL_ADDRESS 0x09000000ULL

#define DESC_VALID 1ULL
#define DESC_TABLE 2ULL
#define DESC_AF (1ULL << 10)
#define DESC_SH_INNER (3ULL << 8)
#define DESC_ATTR_NORMAL (0ULL << 2)
#define DESC_ATTR_DEVICE (1ULL << 2)
#define DESC_AP_RW_EL1 (0ULL << 6)
#define DESC_AP_RO_EL1 (2ULL << 6)
#define DESC_UXN (1ULL << 54)
#define DESC_PXN (1ULL << 53)
#define DESC_PAGE_ATTRIBUTES (DESC_AF | DESC_SH_INNER | 0x1dcULL | \
				  DESC_PXN | DESC_UXN)

#define MAIR_NORMAL_WB 0xffULL
#define MAIR_DEVICE_NGNRNE 0x00ULL

/*
 * TCR_EL1: a 48-bit TTBR0 VA space, 4 KiB granules, inner-shareable normal
 * memory, and write-back/write-allocate cacheability descriptions. TTBR1 is
 * disabled because this milestone has only one address space.
 */
#define TCR_T0SZ (16ULL << 0)
#define TCR_IRGN0_WBWA (1ULL << 8)
#define TCR_ORGN0_WBWA (1ULL << 10)
#define TCR_SH0_INNER (3ULL << 12)
#define TCR_TG0_4K (0ULL << 14)
#define TCR_EPD1 (1ULL << 23)
#define TCR_T1SZ (16ULL << 16)
#define TCR_IRGN1_WBWA (1ULL << 24)
#define TCR_ORGN1_WBWA (1ULL << 26)
#define TCR_SH1_INNER (3ULL << 28)
#define TCR_TG1_4K (2ULL << 30)

static u64 table_page_count;
static u64 l3_table_page_count;
static u64 root_table_address;
static u64 initial_sctlr_value;
static unsigned int initialized;

extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char __bss_end[];
extern char __stack_bottom[];
extern char __stack_top[];

static u64 read_sctlr(void)
{
	u64 value;

	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(value));
	return value;
}

u64 mmu_enabled(void)
{
	return read_sctlr() & 1ULL;
}

u64 mmu_initial_sctlr(void)
{
	return initial_sctlr_value;
}

u64 mmu_current_sctlr(void)
{
	return read_sctlr();
}

static descriptor_t *allocate_table(void)
{
	u64 physical_address;
	descriptor_t *table;

	if (pmm_alloc_page(&physical_address) != 0) {
		panic("MMU ran out of page-table pages");
	}
	table = (descriptor_t *)(unsigned long)physical_address;
	for (u64 index = 0ULL; index < PAGE_TABLE_ENTRIES; ++index) {
		table[index] = 0ULL;
	}
	++table_page_count;
	return table;
}

static descriptor_t *child_table(descriptor_t *parent, u64 index)
{
	descriptor_t entry = parent[index];

	if ((entry & DESC_VALID) != 0ULL) {
		if ((entry & DESC_TABLE) != DESC_TABLE) {
			panic("MMU table conflicts with block mapping");
		}
		return (descriptor_t *)(unsigned long)(entry & ~0xfffULL);
	}

	descriptor_t *child = allocate_table();
	parent[index] = (u64)(unsigned long)child | DESC_VALID | DESC_TABLE;
	return child;
}

static descriptor_t *l2_table(descriptor_t *root, u64 virtual_address)
{
	u64 l0_index = (virtual_address >> 39) & 0x1ffULL;
	u64 l1_index = (virtual_address >> 30) & 0x1ffULL;
	descriptor_t *l1 = child_table(root, l0_index);

	return child_table(l1, l1_index);
}

static descriptor_t normal_rw_nx(u64 address)
{
	return address | DESC_VALID | DESC_AF | DESC_SH_INNER |
	       DESC_ATTR_NORMAL | DESC_AP_RW_EL1 | DESC_PXN | DESC_UXN;
}

static descriptor_t normal_ro_x(u64 address)
{
	return address | DESC_VALID | DESC_AF | DESC_SH_INNER |
	       DESC_ATTR_NORMAL | DESC_AP_RO_EL1 | DESC_UXN;
}

static descriptor_t normal_ro_nx(u64 address)
{
	return address | DESC_VALID | DESC_AF | DESC_SH_INNER |
	       DESC_ATTR_NORMAL | DESC_AP_RO_EL1 | DESC_PXN | DESC_UXN;
}

static descriptor_t device_rw_nx(u64 address)
{
	return address | DESC_VALID | DESC_AF | DESC_SH_INNER |
	       DESC_ATTR_DEVICE | DESC_AP_RW_EL1 | DESC_PXN | DESC_UXN;
}

static u64 descriptor_attributes(descriptor_t descriptor)
{
	return descriptor & DESC_PAGE_ATTRIBUTES;
}

static void map_block(descriptor_t *root, u64 address)
{
	descriptor_t *table = l2_table(root, address);
	u64 index = (address >> 21) & 0x1ffULL;
	descriptor_t descriptor = normal_rw_nx(address);

	if ((table[index] & DESC_VALID) != 0ULL && table[index] != descriptor) {
		panic("MMU block mapping conflict");
	}
	table[index] = descriptor;
}

static descriptor_t *split_block(descriptor_t *l2, u64 index)
{
	descriptor_t block = l2[index];
	descriptor_t *l3 = allocate_table();
	u64 block_address = block & ~(BLOCK_SIZE - 1ULL);
	u64 attributes = block & DESC_PAGE_ATTRIBUTES;

	for (u64 page = 0ULL; page < PAGE_TABLE_ENTRIES; ++page) {
		l3[page] = (block_address + page * NIMERA_PAGE_SIZE) |
			   DESC_VALID | DESC_TABLE | attributes;
	}
	l2[index] = (u64)(unsigned long)l3 | DESC_VALID | DESC_TABLE;
	return l3;
}

static void map_page(descriptor_t *root, u64 address, u64 attributes)
{
	descriptor_t *l2 = l2_table(root, address);
	u64 l2_index = (address >> 21) & 0x1ffULL;
	descriptor_t *l3;
	u64 l3_index = (address >> 12) & 0x1ffULL;
	descriptor_t descriptor = address | DESC_VALID | DESC_TABLE | attributes;
	unsigned int new_l3 = (l2[l2_index] & DESC_VALID) == 0ULL;

	if ((l2[l2_index] & DESC_VALID) != 0ULL &&
	    (l2[l2_index] & DESC_TABLE) != DESC_TABLE) {
		l3 = split_block(l2, l2_index);
		++l3_table_page_count;
	} else {
		l3 = child_table(l2, l2_index);
		if (new_l3 != 0U) {
			++l3_table_page_count;
		}
	}
	l3[l3_index] = descriptor;
}

static u64 page_align_down(u64 address)
{
	return address & ~(NIMERA_PAGE_SIZE - 1ULL);
}

static u64 page_align_up(u64 address)
{
	if (address > ~0ULL - (NIMERA_PAGE_SIZE - 1ULL)) {
		panic("MMU permission range overflows address space");
	}
	return (address + NIMERA_PAGE_SIZE - 1ULL) &
	       ~(NIMERA_PAGE_SIZE - 1ULL);
}

static void map_permission_range(descriptor_t *root, u64 start, u64 end,
					 u64 attributes)
{
	start = page_align_down(start);
	end = page_align_up(end);
	if (end < start) {
		panic("MMU permission range is inverted");
	}
	for (u64 address = start; address < end; address += NIMERA_PAGE_SIZE) {
		map_page(root, address, attributes);
	}
}

static u64 symbol_address(const char *symbol)
{
	return (u64)(unsigned long)symbol;
}

static void validate_section_ranges(const struct memory_map *map)
{
	u64 ram_start = map->physical.base;
	u64 ram_end = ram_start + map->physical.size;
	u64 starts[] = {symbol_address(__text_start),
			 symbol_address(__rodata_start), symbol_address(__data_start),
			 symbol_address(__bss_start), symbol_address(__stack_bottom)};
	u64 ends[] = {symbol_address(__text_end), symbol_address(__rodata_end),
			       symbol_address(__data_end), symbol_address(__bss_end),
			       symbol_address(__stack_top)};

	if (map->physical.size > ~0ULL - ram_start) {
		panic("MMU physical RAM range overflows address space");
	}
	for (unsigned int index = 0U; index < 5U; ++index) {
		if (ends[index] < starts[index] || starts[index] < ram_start ||
		    ends[index] > ram_end) {
			panic("MMU kernel section is outside physical RAM");
		}
	}
	for (unsigned int index = 0U; index < 5U; ++index) {
		if ((starts[index] & (NIMERA_PAGE_SIZE - 1ULL)) != 0ULL ||
		    (ends[index] & (NIMERA_PAGE_SIZE - 1ULL)) != 0ULL) {
			panic("MMU kernel section is not page aligned");
		}
	}
	for (unsigned int index = 0U; index < 4U; ++index) {
		if (ends[index] > starts[index + 1U]) {
			panic("MMU kernel permission ranges overlap");
		}
	}
}

static descriptor_t lookup_descriptor(u64 virtual_address)
{
	descriptor_t *root = (descriptor_t *)(unsigned long)root_table_address;
	descriptor_t l0 = root[(virtual_address >> 39) & 0x1ffULL];
	descriptor_t *l1;
	descriptor_t l1_entry;
	descriptor_t *l2;
	descriptor_t l2_entry;
	descriptor_t *l3;

	if ((l0 & (DESC_VALID | DESC_TABLE)) !=
	    (DESC_VALID | DESC_TABLE)) {
		return 0ULL;
	}
	l1 = (descriptor_t *)(unsigned long)(l0 & ~0xfffULL);
	l1_entry = l1[(virtual_address >> 30) & 0x1ffULL];
	if ((l1_entry & (DESC_VALID | DESC_TABLE)) !=
	    (DESC_VALID | DESC_TABLE)) {
		return 0ULL;
	}
	l2 = (descriptor_t *)(unsigned long)(l1_entry & ~0xfffULL);
	l2_entry = l2[(virtual_address >> 21) & 0x1ffULL];
	if ((l2_entry & DESC_VALID) == 0ULL) {
		return 0ULL;
	}
	if ((l2_entry & DESC_TABLE) == 0ULL) {
		return l2_entry;
	}
	l3 = (descriptor_t *)(unsigned long)(l2_entry & ~0xfffULL);
	return l3[(virtual_address >> 12) & 0x1ffULL];
}

static int descriptor_matches(u64 address, descriptor_t attributes)
{
	descriptor_t expected = page_align_down(address) | DESC_VALID |
					DESC_TABLE | attributes;

	return lookup_descriptor(address) == expected ? 0 : -1;
}

int mmu_validate_protections(const void *heap_pointer)
{
	if (descriptor_matches(symbol_address(__text_start),
				       descriptor_attributes(normal_ro_x(0ULL))) != 0 ||
	    descriptor_matches(symbol_address(__rodata_start),
				       descriptor_attributes(normal_ro_nx(0ULL))) != 0 ||
	    descriptor_matches(symbol_address(__data_start),
				       descriptor_attributes(normal_rw_nx(0ULL))) != 0 ||
	    descriptor_matches(symbol_address(__bss_start),
				       descriptor_attributes(normal_rw_nx(0ULL))) != 0 ||
	    descriptor_matches(symbol_address(__stack_bottom),
				       descriptor_attributes(normal_rw_nx(0ULL))) != 0 ||
	    descriptor_matches((u64)(unsigned long)heap_pointer,
				       descriptor_attributes(normal_rw_nx(0ULL))) != 0 ||
	    descriptor_matches(root_table_address,
				       descriptor_attributes(normal_rw_nx(0ULL))) != 0 ||
	    descriptor_matches(UART_PHYSICAL_ADDRESS,
				       descriptor_attributes(device_rw_nx(0ULL))) != 0) {
		return -1;
	}
	return 0;
}

static volatile unsigned int execute_data_instruction
	__attribute__((section(".data"))) = 0xd65f03c0U;

__attribute__((noreturn))
void mmu_write_text_test(void)
{
	volatile unsigned int *text =
		(volatile unsigned int *)(unsigned long)&mmu_write_text_test;

	*text = 0U;
	panic("MMU write-to-text test unexpectedly returned");
}

__attribute__((noreturn))
void mmu_execute_data_test(void)
{
	void (*entry)(void) =
		(void (*)(void))(unsigned long)&execute_data_instruction;

	entry();
	panic("MMU execute-from-data test unexpectedly returned");
}

static void map_ram(descriptor_t *root, struct memory_range range)
{
	u64 start = range.base;
	u64 end;

	if (range.size > ~0ULL - range.base) {
		panic("MMU RAM range overflows address space");
	}
	end = range.base + range.size;
	if (start > ~0ULL - (NIMERA_PAGE_SIZE - 1ULL)) {
		panic("MMU RAM range cannot be page aligned");
	}
	start &= ~(NIMERA_PAGE_SIZE - 1ULL);
	if (end > ~0ULL - (NIMERA_PAGE_SIZE - 1ULL)) {
		panic("MMU RAM range cannot be page aligned");
	}
	end = (end + NIMERA_PAGE_SIZE - 1ULL) &
	      ~(NIMERA_PAGE_SIZE - 1ULL);
	while (start < end && (start & (BLOCK_SIZE - 1ULL)) != 0ULL) {
		map_page(root, start, DESC_ATTR_NORMAL);
		start += NIMERA_PAGE_SIZE;
	}
	while (end - start >= BLOCK_SIZE) {
		map_block(root, start);
		start += BLOCK_SIZE;
	}
	while (start < end) {
		map_page(root, start, DESC_ATTR_NORMAL);
		start += NIMERA_PAGE_SIZE;
	}
}

void mmu_init(const struct memory_map *map)
{
	descriptor_t *root;
	u64 initial_sctlr;
	u64 tcr = TCR_T0SZ | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA |
		  TCR_SH0_INNER | TCR_TG0_4K | TCR_EPD1 | TCR_T1SZ | TCR_IRGN1_WBWA |
		  TCR_ORGN1_WBWA | TCR_SH1_INNER | TCR_TG1_4K;
	u64 mair = MAIR_NORMAL_WB | (MAIR_DEVICE_NGNRNE << 8);

	if (initialized != 0U || mmu_enabled() != 0ULL) {
		panic("MMU initialized twice or already enabled");
	}
	initial_sctlr = read_sctlr();
	initial_sctlr_value = initial_sctlr;
	table_page_count = 0ULL;
	l3_table_page_count = 0ULL;
	root = allocate_table();
	root_table_address = (u64)(unsigned long)root;
	map_ram(root, map->physical);
	validate_section_ranges(map);
	map_permission_range(root, symbol_address(__text_start),
				     symbol_address(__text_end),
				     descriptor_attributes(normal_ro_x(0ULL)));
	map_permission_range(root, symbol_address(__rodata_start),
				     symbol_address(__rodata_end),
				     descriptor_attributes(normal_ro_nx(0ULL)));
	map_permission_range(root, symbol_address(__data_start),
				     symbol_address(__data_end),
				     descriptor_attributes(normal_rw_nx(0ULL)));
	map_permission_range(root, symbol_address(__bss_start),
				     symbol_address(__stack_top),
				     descriptor_attributes(normal_rw_nx(0ULL)));
	map_page(root, UART_PHYSICAL_ADDRESS,
		 descriptor_attributes(device_rw_nx(UART_PHYSICAL_ADDRESS)));

	__asm__ volatile("msr mair_el1, %0" :: "r"(mair) : "memory");
	__asm__ volatile("msr tcr_el1, %0" :: "r"(tcr) : "memory");
	__asm__ volatile("msr ttbr0_el1, %0" :: "r"(root_table_address)
						 : "memory");
	__asm__ volatile("dsb sy\n\ttlbi vmalle1\n\tdsb sy\n\tisb" ::: "memory");
	__asm__ volatile("msr sctlr_el1, %0" :: "r"(initial_sctlr | 1ULL)
					 : "memory");
	__asm__ volatile("isb" ::: "memory");
	initialized = 1U;
}

u64 mmu_page_table_pages(void)
{
	return table_page_count;
}

u64 mmu_l3_table_pages(void)
{
	return l3_table_page_count;
}

__attribute__((noreturn))
void mmu_fault_test(void)
{
	volatile u64 *unmapped = (volatile u64 *)(unsigned long)0x1000000000ULL;
	volatile u64 value = *unmapped;

	(void)value;
	panic("MMU fault test unexpectedly returned");
}
