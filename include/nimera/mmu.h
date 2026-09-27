#ifndef NIMERA_MMU_H
#define NIMERA_MMU_H

#include <nimera/memory.h>

struct mmu_address_space {
	u64 root_table;
	u64 table_pages;
	u64 l3_table_pages;
};

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
const struct mmu_address_space *mmu_kernel_address_space(void);
int mmu_address_space_create(struct mmu_address_space *space);
void mmu_address_space_destroy(struct mmu_address_space *space);
void mmu_activate_address_space(const struct mmu_address_space *space);
int mmu_map_user_page_in(struct mmu_address_space *space, u64 virtual_address,
			 u64 physical_address, unsigned int permissions);
int mmu_unmap_user_page_in(struct mmu_address_space *space, u64 virtual_address);
int mmu_clear_user_range_in(struct mmu_address_space *space, u64 start, u64 end);
int mmu_user_readable_range_in(const struct mmu_address_space *space,
			 u64 address, u64 length);
int mmu_user_writable_range_in(const struct mmu_address_space *space,
			 u64 address, u64 length);
u64 mmu_user_physical_address(const struct mmu_address_space *space, u64 address);
__attribute__((noreturn)) void mmu_write_text_test(void);
__attribute__((noreturn)) void mmu_execute_data_test(void);
__attribute__((noreturn)) void mmu_fault_test(void);

#endif
