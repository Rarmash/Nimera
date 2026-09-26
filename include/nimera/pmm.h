#ifndef NIMERA_PMM_H
#define NIMERA_PMM_H

#include <nimera/memory.h>

#define NIMERA_PAGE_SIZE 4096ULL

void pmm_init(const struct memory_map *map);

int pmm_alloc_page(u64 *physical_address);
void pmm_free_page(u64 physical_address);

u64 pmm_total_pages(void);
u64 pmm_free_pages(void);
u64 pmm_used_pages(void);
u64 pmm_metadata_pages(void);
u64 pmm_bitmap_bytes(void);

#endif
