#ifndef NIMERA_MMU_H
#define NIMERA_MMU_H

#include <nimera/memory.h>

void mmu_init(const struct memory_map *map);
int mmu_map_device_range(u64 start, u64 size);
u64 mmu_enabled(void);
u64 mmu_initial_sctlr(void);
u64 mmu_current_sctlr(void);
u64 mmu_page_table_pages(void);
u64 mmu_l3_table_pages(void);
int mmu_validate_protections(const void *heap_pointer);
int mmu_user_readable_range(u64 address, u64 length);
int mmu_user_writable_range(u64 address, u64 length);
#define MMU_USER_READ  1U
#define MMU_USER_WRITE 2U
#define MMU_USER_EXEC  4U
int mmu_map_user_page(u64 virtual_address, u64 physical_address,
			 unsigned int permissions);
int mmu_unmap_user_page(u64 virtual_address);
__attribute__((noreturn)) void mmu_write_text_test(void);
__attribute__((noreturn)) void mmu_execute_data_test(void);
__attribute__((noreturn)) void mmu_fault_test(void);

#endif
