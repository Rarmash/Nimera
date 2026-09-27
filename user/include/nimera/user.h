#ifndef NIMERA_USER_H
#define NIMERA_USER_H

#include <nimera/abi/syscall.h>
#include <nimera/abi/terminal.h>

long long nimera_write(unsigned long long handle, const void *buffer,
	unsigned long long length);
long long nimera_write_console(const char *text, unsigned long long length);
long long nimera_exit(long long status);
long long nimera_open(const char *path, unsigned long long length,
	unsigned long long flags);
long long nimera_read(unsigned long long handle, void *buffer,
	unsigned long long length);
long long nimera_write_file(unsigned long long handle, const void *buffer,
	unsigned long long length);
long long nimera_close(unsigned long long handle);
int nimera_read_key(struct nimera_key_event *event);
int nimera_terminal_size(struct nimera_terminal_size *size);
int nimera_terminal_clear(void);
int nimera_terminal_move_cursor(unsigned int row, unsigned int column);
int nimera_terminal_clear_line(void);
int nimera_terminal_cursor_visible(int visible);
int nimera_terminal_begin_update(void);
int nimera_terminal_end_update(void);
void *nimera_alloc(unsigned long long bytes);
int nimera_free(void *address);
long long nimera_open_directory(const char *path, unsigned long long length);
long long nimera_read_directory(unsigned long long handle,
	struct nimera_dir_entry *entry);
int nimera_mkdir(const char *path, unsigned long long length);
int nimera_unlink(const char *path, unsigned long long length);
int nimera_rmdir(const char *path, unsigned long long length);
int nimera_rename(const char *source, unsigned long long source_length,
	const char *destination, unsigned long long destination_length);
long long nimera_getcwd(char *buffer, unsigned long long capacity);
long long nimera_getpid(void);

#endif
