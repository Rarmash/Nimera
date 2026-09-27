#ifndef NIMERA_USER_H
#define NIMERA_USER_H

#include <nimera/abi/syscall.h>
#include <nimera/abi/terminal.h>

long long nimera_write(const char *text, unsigned long long length);
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

#endif
