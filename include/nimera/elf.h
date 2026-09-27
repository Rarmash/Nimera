#ifndef NIMERA_ELF_H
#define NIMERA_ELF_H

#include <nimera/types.h>

struct vfs_node;

enum elf_result {
	ELF_OK = 0,
	ELF_NOT_FOUND = -1,
	ELF_INVALID = -2,
	ELF_UNSUPPORTED = -3,
	ELF_NO_MEMORY = -4,
	ELF_BUSY = -5,
	ELF_IO = -6
};

enum elf_result elf_load_user(struct vfs_node *cwd, const char *path);
void elf_user_task_finished(void);
int elf_user_task_active(void);
int elf_install_test_payload(struct vfs_node *root);
const char *elf_error_string(enum elf_result result);
u64 elf_user_base(void);
u64 elf_user_limit(void);

#endif
