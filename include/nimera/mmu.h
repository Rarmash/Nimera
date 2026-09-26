#ifndef NIMERA_MMU_H
#define NIMERA_MMU_H

#include <nimera/memory.h>

void mmu_init(const struct memory_map *map);
u64 mmu_enabled(void);
u64 mmu_initial_sctlr(void);
u64 mmu_current_sctlr(void);
u64 mmu_page_table_pages(void);
__attribute__((noreturn)) void mmu_fault_test(void);

#endif
