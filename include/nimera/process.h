#ifndef NIMERA_PROCESS_H
#define NIMERA_PROCESS_H

#include <nimera/mmu.h>
#include <nimera/types.h>

struct vfs_node;
struct pipe;

#define NIMERA_MAX_PROCESSES 16U
#define PROCESS_MAX_PAGES 256U
#define PROCESS_MAX_ALLOCATION_PAGES 256U
#define PROCESS_MAX_ALLOCATIONS 32U
#define PROCESS_MAX_HANDLES 16U

enum process_state {
	PROCESS_FREE,
	PROCESS_RUNNABLE,
	PROCESS_RUNNING,
	PROCESS_ZOMBIE
};

enum process_handle_type {
	PROCESS_HANDLE_NONE,
	PROCESS_HANDLE_VFS_FILE,
	PROCESS_HANDLE_VFS_DIRECTORY,
	PROCESS_HANDLE_CONSOLE_OUTPUT,
	PROCESS_HANDLE_PIPE_READ,
	PROCESS_HANDLE_PIPE_WRITE
};

struct process_page {
	u64 virtual_address;
	u64 physical_address;
	unsigned int permissions;
};

struct process_handle {
	unsigned int in_use;
	enum process_handle_type type;
	struct vfs_node *node;
	struct pipe *pipe;
	u64 offset;
	u64 flags;
};

struct process_stdio {
	struct pipe *stdin_pipe;
	struct pipe *stdout_pipe;
	struct pipe *stderr_pipe;
};

struct process_allocation {
	unsigned int in_use;
	u64 virtual_address;
	u64 page_count;
	u64 physical_pages[PROCESS_MAX_ALLOCATION_PAGES];
};

struct process {
	u64 pid;
	enum process_state state;
	long long exit_status;
	unsigned int thread_index;
	unsigned int terminal_owner;
	struct mmu_address_space address_space;
	struct process_page pages[PROCESS_MAX_PAGES];
	unsigned int page_count;
	struct process_handle handles[PROCESS_MAX_HANDLES];
	struct process_allocation allocations[PROCESS_MAX_ALLOCATIONS];
	struct vfs_node *cwd;
	char cwd_path[128];
};

struct process *process_current(void);
int process_current_pid(void);
struct process *process_find(u64 pid);
struct process *process_last_spawned(void);
int process_is_zombie(const struct process *process);
u64 process_user_physical(const struct process *process, u64 address);
void process_mark_exit(struct process *process, long long status);
void process_reap(struct process *process);
unsigned int process_count(void);

#endif
