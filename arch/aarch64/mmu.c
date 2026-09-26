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
#define DESC_UXN (1ULL << 54)
#define DESC_PXN (1ULL << 53)

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
static u64 root_table_address;
static u64 initial_sctlr_value;
static unsigned int initialized;

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

static void map_block(descriptor_t *root, u64 address)
{
	descriptor_t *table = l2_table(root, address);
	u64 index = (address >> 21) & 0x1ffULL;
	descriptor_t descriptor = address | DESC_VALID | DESC_AF |
				  DESC_SH_INNER | DESC_ATTR_NORMAL;

	if ((table[index] & DESC_VALID) != 0ULL && table[index] != descriptor) {
		panic("MMU block mapping conflict");
	}
	table[index] = descriptor;
}

static void map_page(descriptor_t *root, u64 address, u64 attributes)
{
	descriptor_t *l2 = l2_table(root, address);
	u64 l2_index = (address >> 21) & 0x1ffULL;
	descriptor_t *l3;
	u64 l3_index = (address >> 12) & 0x1ffULL;
	descriptor_t descriptor = address | DESC_VALID | DESC_TABLE | DESC_AF |
				  DESC_SH_INNER | attributes;

	if ((l2[l2_index] & DESC_VALID) != 0ULL &&
	    (l2[l2_index] & DESC_TABLE) != DESC_TABLE) {
		panic("MMU page mapping conflicts with block");
	}
	l3 = child_table(l2, l2_index);
	if ((l3[l3_index] & DESC_VALID) != 0ULL &&
	    l3[l3_index] != descriptor) {
		panic("MMU page mapping conflict");
	}
	l3[l3_index] = descriptor;
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
	root = allocate_table();
	root_table_address = (u64)(unsigned long)root;
	map_ram(root, map->physical);
	map_page(root, UART_PHYSICAL_ADDRESS,
		 DESC_ATTR_DEVICE | DESC_UXN | DESC_PXN);

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

__attribute__((noreturn))
void mmu_fault_test(void)
{
	volatile u64 *unmapped = (volatile u64 *)(unsigned long)0x1000000000ULL;
	volatile u64 value = *unmapped;

	(void)value;
	panic("MMU fault test unexpectedly returned");
}
