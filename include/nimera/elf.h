#ifndef NIMERA_ELF_H
#define NIMERA_ELF_H

#include <nimera/types.h>

struct vfs_node;

#define ELF_MAX_ARGUMENTS 8U
#define ELF_MAX_ARGUMENT_BYTES 256U

struct elf_argument {
	const char *text;
	u64 length;
};

enum elf_result {
	ELF_OK = 0,
	ELF_NOT_FOUND = -1,
	ELF_INVALID = -2,
	ELF_UNSUPPORTED = -3,
	ELF_NO_MEMORY = -4,
	ELF_BUSY = -5,
	ELF_IO = -6
};

enum elf_result elf_load_user(struct vfs_node *cwd, const char *path,
			      const struct elf_argument *arguments,
			      unsigned int argument_count);
void elf_user_task_finished(void);
int elf_user_task_active(void);
int elf_install_test_payload(struct vfs_node *root);
const char *elf_error_string(enum elf_result result);
u64 elf_user_base(void);
u64 elf_user_limit(void);
long long elf_user_open(const char *path, u64 flags);
long long elf_user_read(unsigned int handle, char *buffer, u64 length);
long long elf_user_write(unsigned int handle, const char *buffer, u64 length);
long long elf_user_close(unsigned int handle);
void elf_user_close_all(void);
unsigned int elf_user_open_count(void);
long long elf_user_alloc(u64 bytes);
long long elf_user_free(u64 address);
u64 elf_user_allocation_count(void);

#endif
