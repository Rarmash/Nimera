#include <nimera/elf.h>
#include <nimera/console.h>
#include <nimera/format.h>
#include <nimera/abi/syscall.h>
#include <nimera/mmu.h>
#include <nimera/pmm.h>
#include <nimera/pipe.h>
#include <nimera/process.h>
#include <nimera/scheduler.h>
#include <nimera/terminal.h>
#include <nimera/user.h>
#include <nimera/vfs.h>
#include <nimera/window.h>

#define ELF_USER_BASE 0x10000000ULL
#define ELF_USER_LIMIT 0x20000000ULL
#define ELF_DYNAMIC_BASE 0x18000000ULL
#define ELF_DYNAMIC_LIMIT 0x1f000000ULL
#define ELF_STACK_PAGES 4ULL
#define ELF_STACK_BASE (ELF_USER_LIMIT - ELF_STACK_PAGES * NIMERA_PAGE_SIZE)
#define ELF_MAX_PHNUM 16U
#define ELF_MAX_FILE_SIZE (64ULL * 1024ULL)
#define ELF_MAX_ALLOCATIONS PROCESS_MAX_ALLOCATIONS
#define ELF_MAX_ALLOCATION_PAGES PROCESS_MAX_ALLOCATION_PAGES
#define PT_LOAD 1U
#define PF_X 1U
#define PF_W 2U
#define PF_R 4U

static unsigned char image_storage[ELF_MAX_FILE_SIZE];
static char *image;
#define ELF_MAX_HANDLES PROCESS_MAX_HANDLES
#define ELF_MAX_PAGES PROCESS_MAX_PAGES
#define loaded_page process_page
#define user_handle process_handle
#define user_allocation process_allocation

static struct process processes[NIMERA_MAX_PROCESSES];
static struct process *loading_process;
static struct process *last_spawned_process;
static u64 next_pid = 1ULL;

static struct process *operation_process(void)
{
	struct process *process = scheduler_current_process();
	return process != (struct process *)0 ? process : loading_process;
}

#define pages (operation_process()->pages)
#define loaded_count (operation_process()->page_count)
#define user_cwd (operation_process()->cwd)
#define user_cwd_path (operation_process()->cwd_path)
#define handles (operation_process()->handles)
#define allocations (operation_process()->allocations)

struct process *process_current(void)
{
	return scheduler_current_process();
}

int process_current_pid(void)
{
	struct process *process = process_current();
	return process == (struct process *)0 ? -1 : (int)process->pid;
}

struct process *process_find(u64 pid)
{
	for (unsigned int index = 0U; index < NIMERA_MAX_PROCESSES; ++index)
		if (processes[index].state != PROCESS_FREE && processes[index].pid == pid)
			return &processes[index];
	return (struct process *)0;
}

struct process *process_last_spawned(void)
{
	return last_spawned_process;
}

int process_is_zombie(const struct process *process)
{
	return process != (const struct process *)0 && process->state == PROCESS_ZOMBIE;
}

u64 process_user_physical(const struct process *process, u64 address)
{
	return process == (const struct process *)0 ? 0ULL :
		mmu_user_physical_address(&process->address_space, address);
}

unsigned int process_count(void)
{
	unsigned int count = 0U;
	for (unsigned int index = 0U; index < NIMERA_MAX_PROCESSES; ++index)
		if (processes[index].state != PROCESS_FREE) ++count;
	return count;
}

static struct process *allocate_process(void)
{
	for (unsigned int index = 0U; index < NIMERA_MAX_PROCESSES; ++index) {
		if (processes[index].state != PROCESS_FREE) continue;
		for (u64 byte = 0ULL; byte < sizeof(processes[index]); ++byte)
			((unsigned char *)(void *)&processes[index])[byte] = 0U;
		processes[index].pid = next_pid++;
		if (next_pid == 0ULL) next_pid = 1ULL;
		processes[index].state = PROCESS_RUNNABLE;
		processes[index].exit_status = -1LL;
		return &processes[index];
	}
	return (struct process *)0;
}

static void clear_handle(unsigned int index)
{
	if (handles[index].in_use != 0U && handles[index].type == PROCESS_HANDLE_PIPE_READ)
		pipe_reader_close(handles[index].pipe);
	if (handles[index].in_use != 0U && handles[index].type == PROCESS_HANDLE_PIPE_WRITE)
		pipe_writer_close(handles[index].pipe);
	handles[index].in_use = 0U;
	handles[index].type = PROCESS_HANDLE_NONE;
	handles[index].node = (struct vfs_node *)0;
	handles[index].pipe = (struct pipe *)0;
	handles[index].offset = 0ULL;
	handles[index].flags = 0ULL;
}

#undef handles
static void initialize_standard_handles(struct process *process,
	const struct process_stdio *stdio)
{
	for (unsigned int index = 0U; index < ELF_MAX_HANDLES; ++index)
		clear_handle(index);
	process->handles[NIMERA_STDIN].in_use = 1U;
	process->handles[NIMERA_STDIN].type = PROCESS_HANDLE_NONE;
	process->handles[NIMERA_STDOUT].in_use = 1U;
	process->handles[NIMERA_STDOUT].type = PROCESS_HANDLE_CONSOLE_OUTPUT;
	process->handles[NIMERA_STDERR].in_use = 1U;
	process->handles[NIMERA_STDERR].type = PROCESS_HANDLE_CONSOLE_OUTPUT;
	if (stdio == (const struct process_stdio *)0) return;
	if (stdio->stdin_pipe != (struct pipe *)0) {
		process->handles[NIMERA_STDIN].type = PROCESS_HANDLE_PIPE_READ;
		process->handles[NIMERA_STDIN].pipe = stdio->stdin_pipe;
		pipe_reader_open(stdio->stdin_pipe);
	}
	if (stdio->stdout_pipe != (struct pipe *)0) {
		process->handles[NIMERA_STDOUT].type = PROCESS_HANDLE_PIPE_WRITE;
		process->handles[NIMERA_STDOUT].pipe = stdio->stdout_pipe;
		pipe_writer_open(stdio->stdout_pipe);
	}
	if (stdio->stderr_pipe != (struct pipe *)0) {
		process->handles[NIMERA_STDERR].type = PROCESS_HANDLE_PIPE_WRITE;
		process->handles[NIMERA_STDERR].pipe = stdio->stderr_pipe;
		pipe_writer_open(stdio->stderr_pipe);
	}
	if (stdio->stdin_file != (struct vfs_node *)0) {
		process->handles[NIMERA_STDIN].type = PROCESS_HANDLE_VFS_FILE;
		process->handles[NIMERA_STDIN].node = stdio->stdin_file;
		process->handles[NIMERA_STDIN].flags = stdio->stdin_flags;
	}
	if (stdio->stdout_file != (struct vfs_node *)0) {
		u64 size = 0ULL;
		process->handles[NIMERA_STDOUT].type = PROCESS_HANDLE_VFS_FILE;
		process->handles[NIMERA_STDOUT].node = stdio->stdout_file;
		process->handles[NIMERA_STDOUT].flags = stdio->stdout_flags;
		if ((stdio->stdout_flags & NIMERA_OPEN_APPEND) != 0ULL &&
			vfs_get_size(stdio->stdout_file, &size) == VFS_OK)
			process->handles[NIMERA_STDOUT].offset = size;
	}
}
#define handles (operation_process()->handles)

static u16 read16(const unsigned char *p)
{
	return (u16)p[0] | ((u16)p[1] << 8);
}

static u32 read32(const unsigned char *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static u64 read64(const unsigned char *p)
{
	return (u64)read32(p) | ((u64)read32(p + 4) << 32);
}

static u64 align_down(u64 value)
{
	return value & ~(NIMERA_PAGE_SIZE - 1ULL);
}

static u64 align_up(u64 value)
{
	return (value + NIMERA_PAGE_SIZE - 1ULL) & ~(NIMERA_PAGE_SIZE - 1ULL);
}

static int range_inside(u64 start, u64 length, u64 limit)
{
	return start <= limit && length <= limit - start;
}

static void clear_loaded_pages(void)
{
	for (unsigned int index = 0U; index < loaded_count; ++index) {
		(void)mmu_unmap_user_page_in(&operation_process()->address_space,
			pages[index].virtual_address);
		pmm_free_page(pages[index].physical_address);
	}
	loaded_count = 0U;
}

static void clear_dynamic_allocations(void)
{
	for (unsigned int index = 0U; index < ELF_MAX_ALLOCATIONS; ++index) {
		if (allocations[index].in_use == 0U) continue;
		for (u64 page = 0ULL; page < allocations[index].page_count; ++page) {
			(void)mmu_unmap_user_page_in(&operation_process()->address_space,
				allocations[index].virtual_address +
				page * NIMERA_PAGE_SIZE);
			pmm_free_page(allocations[index].physical_pages[page]);
		}
		allocations[index].in_use = 0U;
	}
}

static void discard_process(struct process *process)
{
	if (process == (struct process *)0) return;
	loading_process = process;
	clear_dynamic_allocations();
	clear_loaded_pages();
	mmu_address_space_destroy(&process->address_space);
	loading_process = (struct process *)0;
	for (u64 byte = 0ULL; byte < sizeof(*process); ++byte)
		((unsigned char *)(void *)process)[byte] = 0U;
}

static int allocation_overlaps(u64 address, u64 length)
{
	for (unsigned int index = 0U; index < ELF_MAX_ALLOCATIONS; ++index) {
		u64 start;
		if (allocations[index].in_use == 0U) continue;
		start = allocations[index].virtual_address;
		if (address < start + allocations[index].page_count * NIMERA_PAGE_SIZE &&
			start < address + length) return 1;
	}
	return 0;
}

static struct loaded_page *find_page(u64 virtual_address)
{
	for (unsigned int index = 0U; index < loaded_count; ++index)
		if (pages[index].virtual_address == virtual_address) return &pages[index];
	return (struct loaded_page *)0;
}

static int add_page(u64 virtual_address, unsigned int permissions,
			struct loaded_page **result)
{
	struct loaded_page *page = find_page(virtual_address);

	if (page != (struct loaded_page *)0) {
		unsigned int merged = page->permissions | permissions;
		if ((merged & MMU_USER_WRITE) != 0U &&
		    (merged & MMU_USER_EXEC) != 0U) return -1;
		page->permissions = merged;
		*result = page;
		return 0;
	}
	if (loaded_count == ELF_MAX_PAGES) return -1;
	page = &pages[loaded_count++];
	page->virtual_address = virtual_address;
	page->permissions = permissions;
	if (pmm_alloc_page(&page->physical_address) != 0) {
		--loaded_count;
		return -1;
	}
	for (u64 offset = 0ULL; offset < NIMERA_PAGE_SIZE; ++offset)
		((unsigned char *)(unsigned long)page->physical_address)[offset] = 0U;
	*result = page;
	return 0;
}

static unsigned int segment_permissions(u32 flags)
{
	if ((flags & PF_W) != 0U) return MMU_USER_READ | MMU_USER_WRITE;
	return (flags & PF_X) != 0U ? MMU_USER_READ | MMU_USER_EXEC : MMU_USER_READ;
}

/* Build the user stack through its backing pages while the kernel is in EL1.
 * The pointers written into the stack remain user virtual addresses. */
static unsigned char *stack_physical_address(u64 address)
{
	struct loaded_page *page = find_page(align_down(address));
	if (page == (struct loaded_page *)0) return (unsigned char *)0;
	return (unsigned char *)(unsigned long)(page->physical_address +
		(address & (NIMERA_PAGE_SIZE - 1ULL)));
}

static int stack_put_byte(u64 address, unsigned char value)
{
	volatile unsigned char *destination = stack_physical_address(address);
	if (destination == (volatile unsigned char *)0) return -1;
	*destination = value;
	return 0;
}

static int stack_put_u64(u64 address, u64 value)
{
	for (unsigned int index = 0U; index < 8U; ++index)
		if (stack_put_byte(address + index, (unsigned char)(value >> (index * 8U))) != 0)
			return -1;
	return 0;
}

static int build_user_stack(const struct elf_argument *arguments,
		unsigned int count, u64 *argc, u64 *argv)
{
	u64 values[ELF_MAX_ARGUMENTS + 1U];
	u64 argv_address = ELF_STACK_BASE + 0x20ULL;
	u64 string_address = ELF_STACK_BASE + 0x100ULL;
	u64 total = 0ULL;
	if (count > ELF_MAX_ARGUMENTS) return -1;
	for (unsigned int index = 0U; index < count; ++index) {
		if (arguments[index].text == (const char *)0 ||
			arguments[index].length > ELF_MAX_ARGUMENT_BYTES - total)
			return -1;
		total += arguments[index].length;
	}
	for (unsigned int index = 0U; index < count; ++index) {
		values[index] = string_address;
		for (u64 byte = 0ULL; byte < arguments[index].length; ++byte)
			if (stack_put_byte(string_address + byte,
				(unsigned char)arguments[index].text[byte]) != 0) return -1;
		if (stack_put_byte(string_address + arguments[index].length, 0U) != 0) return -1;
		string_address += arguments[index].length + 1ULL;
	}
	values[count] = 0ULL;
	for (unsigned int index = 0U; index <= count; ++index)
		if (stack_put_u64(argv_address + (u64)index * 8ULL, values[index]) != 0) return -1;
	*argc = count;
	*argv = argv_address;
	return 0;
}

enum elf_result elf_load_user(struct vfs_node *cwd, const char *path,
		const struct elf_argument *arguments, unsigned int argument_count)
{
	return elf_load_user_with_stdio(cwd, path, arguments, argument_count,
		(const struct process_stdio *)0);
}

enum elf_result elf_load_user_with_stdio(struct vfs_node *cwd, const char *path,
		const struct elf_argument *arguments, unsigned int argument_count,
		const struct process_stdio *stdio)
{
	struct vfs_node *file;
	u64 size = 0ULL;
	u64 entry;
	u64 phoff;
	u16 phentsize;
	u16 phnum;
	unsigned int load_segments = 0U;
	int entry_executable = 0;
	enum vfs_error vfs_result;
	struct process *process;

	if (argument_count != 0U && arguments == (const struct elf_argument *)0)
		return ELF_INVALID;
	process = allocate_process();
	if (process == (struct process *)0) return ELF_BUSY;
	if (mmu_address_space_create(&process->address_space) != 0) {
		discard_process(process); return ELF_NO_MEMORY;
	}
	if (mmu_clear_user_range_in(&process->address_space, ELF_USER_BASE,
		ELF_USER_LIMIT) != 0) {
		discard_process(process); return ELF_NO_MEMORY;
	}
	loading_process = process;
	vfs_result = vfs_resolve(cwd, path, &file);
	if (vfs_result != VFS_OK) { discard_process(process); return ELF_NOT_FOUND; }
	if (vfs_node_type(file) != VFS_NODE_FILE) {
		vfs_node_release(file); discard_process(process); return ELF_INVALID;
	}
	if (vfs_get_size(file, &size) != VFS_OK || size < 64ULL ||
		size > ELF_MAX_FILE_SIZE) {
		vfs_node_release(file); discard_process(process); return ELF_INVALID;
	}
	image = (char *)(void *)image_storage;
	if (vfs_read(file, image, size, &size) != VFS_OK) {
		vfs_node_release(file); image = (char *)0; discard_process(process); return ELF_IO;
	}
	vfs_node_release(file);
	{
		const unsigned char *h = (const unsigned char *)(const void *)image;
		if (h[0] != 0x7fU || h[1] != 'E' || h[2] != 'L' || h[3] != 'F' ||
		    h[4] != 2U || h[5] != 1U || h[6] != 1U || read16(h + 16) != 2U ||
		    read16(h + 18) != 183U || read32(h + 20) != 1U ||
		    read16(h + 52) != 64U) {
				image = (char *)0; discard_process(process); return ELF_UNSUPPORTED;
		}
		if (read16(h + 54) != 56U || read16(h + 56) == 0U) {
			image = (char *)0; discard_process(process); return ELF_UNSUPPORTED;
		}
		entry = read64(h + 24);
		phoff = read64(h + 32);
		phentsize = read16(h + 54);
		phnum = read16(h + 56);
		if (phentsize != 56U || phnum == 0U || phnum > ELF_MAX_PHNUM ||
		    phoff > size || (u64)phnum * phentsize > size - phoff) {
			image = (char *)0; return ELF_INVALID;
		}
		for (unsigned int index = 0U; index < phnum; ++index) {
			const unsigned char *p = h + phoff + (u64)index * phentsize;
			u32 type = read32(p);
			u32 flags = read32(p + 4);
			u64 offset = read64(p + 8);
			u64 virtual_address = read64(p + 16);
			u64 filesz = read64(p + 32);
			u64 memsz = read64(p + 40);
			u64 start;
			u64 end;
			if (type != PT_LOAD) continue;
			++load_segments;
			if (((flags & PF_W) != 0U && (flags & PF_X) != 0U) ||
			    filesz > memsz || !range_inside(offset, filesz, size) ||
			    virtual_address > ~0ULL - memsz) {
					image = (char *)0; return ELF_INVALID;
			}
			start = align_down(virtual_address);
			end = align_up(virtual_address + memsz);
			if (start < ELF_USER_BASE ||
				end > ELF_STACK_BASE - NIMERA_PAGE_SIZE || end <= start ||
				(start < ELF_DYNAMIC_LIMIT && end > ELF_DYNAMIC_BASE) ||
			    (offset & (NIMERA_PAGE_SIZE - 1ULL)) !=
			    (virtual_address & (NIMERA_PAGE_SIZE - 1ULL))) {
				image = (char *)0; discard_process(process); return ELF_INVALID;
			}
			if (entry >= virtual_address && entry < virtual_address + memsz &&
			    (flags & PF_X) != 0U) entry_executable = 1;
			for (u64 page_address = start; page_address < end;
			     page_address += NIMERA_PAGE_SIZE) {
				struct loaded_page *page;
				u64 copy_start = page_address > virtual_address ? page_address : virtual_address;
				u64 copy_end = page_address + NIMERA_PAGE_SIZE;
				u64 file_end = virtual_address + filesz;
				if (copy_end > file_end) copy_end = file_end;
				if (add_page(page_address, segment_permissions(flags), &page) != 0) {
					clear_loaded_pages(); image = (char *)0; discard_process(process); return ELF_INVALID;
				}
				if (copy_end > copy_start)
					for (u64 byte = copy_start; byte < copy_end; ++byte)
						((unsigned char *)(unsigned long)page->physical_address)
							[byte - page_address] = (unsigned char)image[
								offset + byte - virtual_address];
			}
		}
		if (load_segments == 0U || entry_executable == 0) {
			clear_loaded_pages(); image = (char *)0; discard_process(process); return ELF_INVALID;
		}
	}
	if (entry < ELF_USER_BASE || entry >= ELF_USER_LIMIT) {
		clear_loaded_pages(); image = (char *)0; discard_process(process); return ELF_INVALID;
	}
	for (u64 index = 0ULL; index < ELF_STACK_PAGES; ++index) {
		struct loaded_page *page;
		if (add_page(ELF_STACK_BASE + index * NIMERA_PAGE_SIZE,
				MMU_USER_READ | MMU_USER_WRITE, &page) != 0) {
			clear_loaded_pages(); image = (char *)0; discard_process(process); return ELF_NO_MEMORY;
		}
	}
	{
		u64 argc;
		u64 argv;
		for (unsigned int index = 0U; index < loaded_count; ++index)
			if (mmu_map_user_page_in(&process->address_space, pages[index].virtual_address,
				pages[index].physical_address, pages[index].permissions) != 0) {
				clear_loaded_pages(); image = (char *)0; discard_process(process); return ELF_INVALID;
			}
		if (build_user_stack(arguments, argument_count, &argc, &argv) != 0) {
			clear_loaded_pages(); image = (char *)0; discard_process(process); return ELF_INVALID;
		}
		initialize_standard_handles(process, stdio);
		user_cwd = cwd;
		if (vfs_format_path(cwd, user_cwd_path, sizeof(user_cwd_path)) != VFS_OK) {
			clear_loaded_pages(); image = (char *)0; user_cwd = (struct vfs_node *)0;
			discard_process(process); return ELF_INVALID;
		}
		process->state = PROCESS_RUNNABLE;
		scheduler_enable_user_task_for_process(process, entry, ELF_USER_LIMIT, argc, argv);
		last_spawned_process = process;
		loading_process = (struct process *)0;
		return ELF_OK;
	}
}

void elf_user_task_finished(void)
{
	for (unsigned int index = 0U; index < NIMERA_MAX_PROCESSES; ++index)
		if (processes[index].state == PROCESS_ZOMBIE) {
			if (processes[index].terminal_owner != 0U) terminal_show_cursor();
			process_reap(&processes[index]);
			return;
		}
}

void process_mark_exit(struct process *process, long long status)
{
	if (process == (struct process *)0) return;
	window_manager_destroy_process_windows(process);
	if (process->terminal_owner != 0U) terminal_cancel_update();
	process->exit_status = status;
	/* Endpoint references must disappear at exit so readers can observe EOF
	 * before the shell eventually reaps the zombie process. */
	loading_process = process;
	elf_user_close_all();
	loading_process = (struct process *)0;
	process->state = PROCESS_ZOMBIE;
}

void process_reap(struct process *process)
{
	if (process == (struct process *)0 || process->state != PROCESS_ZOMBIE) return;
	loading_process = process;
	scheduler_release_process(process);
	elf_user_close_all();
	clear_dynamic_allocations();
	clear_loaded_pages();
	mmu_address_space_destroy(&process->address_space);
	loading_process = (struct process *)0;
	for (u64 byte = 0ULL; byte < sizeof(*process); ++byte)
		((unsigned char *)(void *)process)[byte] = 0U;
}

long long elf_user_alloc(u64 bytes)
{
	u64 page_count;
	unsigned int slot = ELF_MAX_ALLOCATIONS;
	u64 address;

	if (operation_process() == (struct process *)0 || bytes == 0ULL || bytes >
		ELF_MAX_ALLOCATION_PAGES * NIMERA_PAGE_SIZE ||
		bytes > ~0ULL - (NIMERA_PAGE_SIZE - 1ULL)) return NIMERA_NERR_INVALID;
	page_count = (bytes + NIMERA_PAGE_SIZE - 1ULL) / NIMERA_PAGE_SIZE;
	for (unsigned int index = 0U; index < ELF_MAX_ALLOCATIONS; ++index)
		if (allocations[index].in_use == 0U) { slot = index; break; }
	if (slot == ELF_MAX_ALLOCATIONS) return NIMERA_NERR_NO_MEMORY;
	for (address = ELF_DYNAMIC_BASE;
		address + page_count * NIMERA_PAGE_SIZE <= ELF_DYNAMIC_LIMIT;
		address += NIMERA_PAGE_SIZE) {
		if (!allocation_overlaps(address, page_count * NIMERA_PAGE_SIZE)) break;
	}
	if (address + page_count * NIMERA_PAGE_SIZE > ELF_DYNAMIC_LIMIT)
		return NIMERA_NERR_NO_MEMORY;
	allocations[slot].virtual_address = address;
	allocations[slot].page_count = page_count;
	for (u64 page = 0ULL; page < page_count; ++page) {
		u64 physical;
		if (pmm_alloc_page(&physical) != 0) {
			for (u64 rollback = 0ULL; rollback < page; ++rollback) {
				(void)mmu_unmap_user_page_in(&operation_process()->address_space, address + rollback * NIMERA_PAGE_SIZE);
				pmm_free_page(allocations[slot].physical_pages[rollback]);
			}
			allocations[slot].page_count = 0ULL;
			return NIMERA_NERR_NO_MEMORY;
		}
		if (mmu_map_user_page_in(&operation_process()->address_space, address + page * NIMERA_PAGE_SIZE, physical,
			MMU_USER_READ | MMU_USER_WRITE) != 0) {
			pmm_free_page(physical);
			for (u64 rollback = 0ULL; rollback < page; ++rollback) {
				(void)mmu_unmap_user_page_in(&operation_process()->address_space, address + rollback * NIMERA_PAGE_SIZE);
				pmm_free_page(allocations[slot].physical_pages[rollback]);
			}
			allocations[slot].page_count = 0ULL;
			return NIMERA_NERR_NO_MEMORY;
		}
		for (u64 byte = 0ULL; byte < NIMERA_PAGE_SIZE; ++byte)
			((unsigned char *)(unsigned long)physical)[byte] = 0U;
		allocations[slot].physical_pages[page] = physical;
	}
	allocations[slot].in_use = 1U;
	return (long long)address;
}

long long elf_user_free(u64 address)
{
	for (unsigned int index = 0U; index < ELF_MAX_ALLOCATIONS; ++index) {
		if (allocations[index].in_use == 0U ||
			allocations[index].virtual_address != address) continue;
		for (u64 page = 0ULL; page < allocations[index].page_count; ++page) {
			(void)mmu_unmap_user_page_in(&operation_process()->address_space, address + page * NIMERA_PAGE_SIZE);
			pmm_free_page(allocations[index].physical_pages[page]);
		}
		allocations[index].in_use = 0U;
		allocations[index].page_count = 0ULL;
		return 0LL;
	}
	return NIMERA_NERR_INVALID;
}

u64 elf_user_allocation_count(void)
{
	u64 count = 0ULL;
	for (unsigned int index = 0U; index < ELF_MAX_ALLOCATIONS; ++index)
		count += allocations[index].in_use != 0U ? 1ULL : 0ULL;
	return count;
}

int elf_user_task_active(void)
{
	for (unsigned int index = 0U; index < NIMERA_MAX_PROCESSES; ++index)
		if (processes[index].state == PROCESS_RUNNABLE ||
			processes[index].state == PROCESS_RUNNING) return 1;
	return 0;
}

const char *elf_error_string(enum elf_result result)
{
	if (result == ELF_BUSY) return "user task already active";
	if (result == ELF_NO_MEMORY) return "not enough memory for ELF";
	if (result == ELF_NOT_FOUND) return "file not found";
	if (result == ELF_IO) return "unable to read executable";
	return "invalid ELF executable";
}

u64 elf_user_base(void) { return ELF_USER_BASE; }
u64 elf_user_limit(void) { return ELF_USER_LIMIT; }

static long long user_vfs_error(enum vfs_error error)
{
	if (error == VFS_NOT_FOUND) return NIMERA_NERR_NOT_FOUND;
	if (error == VFS_NO_MEMORY) return NIMERA_NERR_NO_MEMORY;
	if (error == VFS_IS_DIRECTORY) return NIMERA_NERR_IS_DIRECTORY;
	if (error == VFS_NOT_DIRECTORY) return NIMERA_NERR_NOT_DIRECTORY;
	if (error == VFS_NOT_EMPTY) return NIMERA_NERR_NOT_EMPTY;
	if (error == VFS_ALREADY_EXISTS) return NIMERA_NERR_EXISTS;
	if (error == VFS_BUSY) return NIMERA_NERR_BUSY;
	if (error == VFS_CROSS_DEVICE) return NIMERA_NERR_CROSS_DEVICE;
	if (error == VFS_TOO_LARGE) return NIMERA_NERR_TOO_LARGE;
	return NIMERA_NERR_IO;
}

static long long user_vfs_result(enum vfs_error error)
{
	return error == VFS_OK ? 0LL : user_vfs_error(error);
}

long long elf_user_open(const char *path, u64 flags)
{
	struct vfs_node *node;
	u64 size = 0ULL;
	unsigned int slot;
	unsigned int access = (unsigned int)(flags & (NIMERA_OPEN_READ | NIMERA_OPEN_WRITE));
	enum vfs_error error;
	if (operation_process() == (struct process *)0 || user_cwd == (struct vfs_node *)0 || path == (const char *)0 ||
		(access != NIMERA_OPEN_READ && access != NIMERA_OPEN_WRITE) ||
		(flags & ~(NIMERA_OPEN_READ | NIMERA_OPEN_WRITE | NIMERA_OPEN_CREATE |
		NIMERA_OPEN_TRUNCATE | NIMERA_OPEN_APPEND)) != 0ULL ||
		((flags & (NIMERA_OPEN_TRUNCATE | NIMERA_OPEN_APPEND)) != 0ULL &&
		 (flags & NIMERA_OPEN_WRITE) == 0ULL)) return NIMERA_NERR_INVALID;
	error = vfs_resolve(user_cwd, path, &node);
	if (error != VFS_OK && (flags & NIMERA_OPEN_CREATE) != 0ULL &&
		(flags & NIMERA_OPEN_WRITE) != 0ULL)
		error = vfs_create_file(user_cwd, path, (const char *)0, 0ULL, &node);
	if (error != VFS_OK) return user_vfs_error(error);
	if (vfs_node_type(node) != VFS_NODE_FILE) {
		vfs_node_release(node); return NIMERA_NERR_IS_DIRECTORY;
	}
	if ((flags & NIMERA_OPEN_TRUNCATE) != 0ULL) {
		error = vfs_write_at(node, 0ULL, (const char *)0, 0ULL);
		if (error != VFS_OK) { vfs_node_release(node); return user_vfs_error(error); }
	}
	if (vfs_get_size(node, &size) != VFS_OK) { vfs_node_release(node); return NIMERA_NERR_IO; }
	for (slot = 0U; slot < ELF_MAX_HANDLES && handles[slot].in_use != 0U; ++slot) {}
	if (slot == ELF_MAX_HANDLES) { vfs_node_release(node); return NIMERA_NERR_NO_HANDLES; }
	handles[slot].in_use = 1U;
	handles[slot].type = PROCESS_HANDLE_VFS_FILE;
	handles[slot].node = node;
	handles[slot].pipe = (struct pipe *)0;
	handles[slot].offset = (flags & NIMERA_OPEN_APPEND) != 0ULL ? size : 0ULL;
	handles[slot].flags = flags;
	return (long long)slot;
}

long long elf_user_open_directory(const char *path)
{
	struct vfs_node *node;
	unsigned int slot;
	enum vfs_error error;
	if (operation_process() == (struct process *)0 || user_cwd == (struct vfs_node *)0 || path == (const char *)0)
		return NIMERA_NERR_INVALID;
	error = vfs_resolve(user_cwd, path, &node);
	if (error != VFS_OK) return user_vfs_error(error);
	if (vfs_node_type(node) != VFS_NODE_DIRECTORY) {
		vfs_node_release(node); return NIMERA_NERR_NOT_DIRECTORY;
	}
	for (slot = 0U; slot < ELF_MAX_HANDLES && handles[slot].in_use != 0U; ++slot) {}
	if (slot == ELF_MAX_HANDLES) { vfs_node_release(node); return NIMERA_NERR_NO_HANDLES; }
	handles[slot].in_use = 1U;
	handles[slot].type = PROCESS_HANDLE_VFS_DIRECTORY;
	handles[slot].node = node;
	handles[slot].pipe = (struct pipe *)0;
	handles[slot].offset = 0ULL;
	handles[slot].flags = 0ULL;
	return (long long)slot;
}

long long elf_user_read(unsigned int handle, char *buffer, u64 length)
{
	u64 completed = 0ULL;
	enum vfs_error error;
	if (handle < ELF_MAX_HANDLES && handles[handle].in_use != 0U &&
		handles[handle].type == PROCESS_HANDLE_PIPE_READ)
		return pipe_read_try(handles[handle].pipe, buffer, length);
	if (handle >= ELF_MAX_HANDLES || handles[handle].in_use == 0U ||
		handles[handle].type != PROCESS_HANDLE_VFS_FILE ||
		(handles[handle].flags & NIMERA_OPEN_READ) == 0ULL ||
		buffer == (char *)0) return NIMERA_NERR_BAD_HANDLE;
	error = vfs_read_at(handles[handle].node, handles[handle].offset,
		buffer, length, &completed);
	if (error != VFS_OK) return user_vfs_error(error);
	handles[handle].offset += completed;
	return (long long)completed;
}

long long elf_user_write(unsigned int handle, const char *buffer, u64 length)
{
	u64 offset;
	enum vfs_error error;
	if (handle < ELF_MAX_HANDLES && handles[handle].in_use != 0U) {
		if (handles[handle].type == PROCESS_HANDLE_PIPE_WRITE)
			return pipe_write_try(handles[handle].pipe, buffer, length);
		if (handles[handle].type == PROCESS_HANDLE_CONSOLE_OUTPUT) {
			if (length != 0ULL && buffer == (const char *)0) return NIMERA_NERR_INVALID;
			for (u64 index = 0ULL; index < length; ++index) console_putc(buffer[index]);
			return (long long)length;
		}
	}
	if (handle >= ELF_MAX_HANDLES || handles[handle].in_use == 0U ||
		handles[handle].type != PROCESS_HANDLE_VFS_FILE ||
		(handles[handle].flags & NIMERA_OPEN_WRITE) == 0ULL ||
		(length != 0ULL && buffer == (const char *)0)) return NIMERA_NERR_BAD_HANDLE;
	offset = handles[handle].offset;
	if ((handles[handle].flags & NIMERA_OPEN_APPEND) != 0ULL &&
		vfs_get_size(handles[handle].node, &offset) != VFS_OK) return NIMERA_NERR_IO;
	error = vfs_write_at(handles[handle].node, offset, buffer, length);
	if (error != VFS_OK) return user_vfs_error(error);
	handles[handle].offset = offset + length;
	return (long long)length;
}

long long elf_user_read_directory(unsigned int handle,
	struct nimera_dir_entry *entry)
{
	struct vfs_node *node;
	struct vfs_node *child;
	enum vfs_error error;
	if (handle >= ELF_MAX_HANDLES || handles[handle].in_use == 0U ||
		handles[handle].type != PROCESS_HANDLE_VFS_DIRECTORY || entry == (struct nimera_dir_entry *)0)
		return NIMERA_NERR_BAD_HANDLE;
	node = handles[handle].node;
	error = vfs_readdir(node, (unsigned int)handles[handle].offset, &child);
	if (error == VFS_NOT_FOUND) return 0LL;
	if (error != VFS_OK) return user_vfs_error(error);
	entry->type = vfs_node_type(child) == VFS_NODE_DIRECTORY ?
		NIMERA_DIR_DIRECTORY : NIMERA_DIR_REGULAR;
	entry->name_length = 0U;
	while (entry->name_length < NIMERA_DIR_NAME_MAX &&
		vfs_node_name(child)[entry->name_length] != '\0') {
		entry->name[entry->name_length] = vfs_node_name(child)[entry->name_length];
		++entry->name_length;
	}
	entry->name[entry->name_length] = '\0';
	++handles[handle].offset;
	vfs_node_release(child);
	return 1LL;
}

long long elf_user_mkdir(const char *path)
{
	return user_vfs_result(vfs_mkdir(user_cwd, path, (struct vfs_node **)0));
}

long long elf_user_unlink(const char *path)
{
	return user_vfs_result(vfs_remove(user_cwd, path));
}

long long elf_user_rmdir(const char *path)
{
	return user_vfs_result(vfs_rmdir(user_cwd, path));
}

long long elf_user_rename(const char *source, const char *destination)
{
	return user_vfs_result(vfs_rename(user_cwd, source, destination));
}

long long elf_user_getcwd(char *buffer, u64 capacity)
{
	u64 length = 0ULL;
	if (operation_process() == (struct process *)0 || buffer == (char *)0) return NIMERA_NERR_INVALID;
	while (user_cwd_path[length] != '\0') ++length;
	if (capacity <= length) return NIMERA_NERR_TOO_LARGE;
	for (u64 index = 0ULL; index <= length; ++index) buffer[index] = user_cwd_path[index];
	return (long long)length;
}

long long elf_user_close(unsigned int handle)
{
	if (handle >= ELF_MAX_HANDLES || handles[handle].in_use == 0U)
		return NIMERA_NERR_BAD_HANDLE;
	if (handles[handle].type == PROCESS_HANDLE_VFS_FILE ||
		handles[handle].type == PROCESS_HANDLE_VFS_DIRECTORY)
		vfs_node_release(handles[handle].node);
	clear_handle(handle);
	return 0LL;
}

enum process_handle_type elf_user_handle_type(unsigned int handle)
{
	if (handle >= ELF_MAX_HANDLES || handles[handle].in_use == 0U)
		return PROCESS_HANDLE_NONE;
	return handles[handle].type;
}

struct pipe *elf_user_handle_pipe(unsigned int handle)
{
	if (handle >= ELF_MAX_HANDLES || handles[handle].in_use == 0U)
		return (struct pipe *)0;
	return handles[handle].pipe;
}

void elf_user_close_all(void)
{
	for (unsigned int index = 0U; index < ELF_MAX_HANDLES; ++index)
		if (handles[index].in_use != 0U) (void)elf_user_close(index);
}

unsigned int elf_user_open_count(void)
{
	unsigned int count = 0U;
	for (unsigned int index = 0U; index < ELF_MAX_HANDLES; ++index) count += handles[index].in_use;
	return count;
}

int elf_install_test_payload(struct vfs_node *root)
{
#if NIMERA_ELF_INSTALL_TEST
	extern const unsigned char _binary_build_user_app_hello_elf_start[];
	extern const unsigned char _binary_build_user_app_hello_elf_end[];
	extern const unsigned char _binary_build_user_app_cat_elf_start[];
	extern const unsigned char _binary_build_user_app_cat_elf_end[];
	extern const unsigned char _binary_build_user_app_filetest_elf_start[];
	extern const unsigned char _binary_build_user_app_filetest_elf_end[];
	extern const unsigned char _binary_build_user_app_edit_elf_start[];
	extern const unsigned char _binary_build_user_app_edit_elf_end[];
	extern const unsigned char _binary_build_user_app_ls_elf_start[];
	extern const unsigned char _binary_build_user_app_ls_elf_end[];
	extern const unsigned char _binary_build_user_app_mkdir_elf_start[];
	extern const unsigned char _binary_build_user_app_mkdir_elf_end[];
	extern const unsigned char _binary_build_user_app_touch_elf_start[];
	extern const unsigned char _binary_build_user_app_touch_elf_end[];
	extern const unsigned char _binary_build_user_app_rm_elf_start[];
	extern const unsigned char _binary_build_user_app_rm_elf_end[];
	extern const unsigned char _binary_build_user_app_rmdir_elf_start[];
	extern const unsigned char _binary_build_user_app_rmdir_elf_end[];
	extern const unsigned char _binary_build_user_app_mv_elf_start[];
	extern const unsigned char _binary_build_user_app_mv_elf_end[];
	extern const unsigned char _binary_build_user_app_pwd_elf_start[];
	extern const unsigned char _binary_build_user_app_pwd_elf_end[];
	extern const unsigned char _binary_build_user_app_write_elf_start[];
	extern const unsigned char _binary_build_user_app_write_elf_end[];
	extern const unsigned char _binary_build_user_app_append_elf_start[];
	extern const unsigned char _binary_build_user_app_append_elf_end[];
	extern const unsigned char _binary_build_user_app_pidtest_elf_start[];
	extern const unsigned char _binary_build_user_app_pidtest_elf_end[];
	extern const unsigned char _binary_build_user_app_proctest_elf_start[];
	extern const unsigned char _binary_build_user_app_proctest_elf_end[];
	extern const unsigned char _binary_build_user_app_procfault_elf_start[];
	extern const unsigned char _binary_build_user_app_procfault_elf_end[];
	extern const unsigned char _binary_build_user_app_upper_elf_start[];
	extern const unsigned char _binary_build_user_app_upper_elf_end[];
	extern const unsigned char _binary_build_user_app_pipetest_elf_start[];
	extern const unsigned char _binary_build_user_app_pipetest_elf_end[];
	extern const unsigned char _binary_build_user_app_jobtest_elf_start[];
	extern const unsigned char _binary_build_user_app_jobtest_elf_end[];
	#if NIMERA_USER_GUI_TEST
	extern const unsigned char _binary_build_user_app_guihello_elf_start[];
	extern const unsigned char _binary_build_user_app_guihello_elf_end[];
	extern const unsigned char _binary_build_user_app_guidemo_elf_start[];
	extern const unsigned char _binary_build_user_app_guidemo_elf_end[];
	#endif
	#if NIMERA_NIMEDIT_GUI_TEST
	extern const unsigned char _binary_build_user_app_nimedit_elf_start[];
	extern const unsigned char _binary_build_user_app_nimedit_elf_end[];
	#endif
	#if NIMERA_USER_GUI_TEST || NIMERA_GUI_RUNTIME_TEST || NIMERA_GUI_TEXTFIELD_TEST
	extern const unsigned char _binary_build_user_app_guitest_elf_start[];
	extern const unsigned char _binary_build_user_app_guitest_elf_end[];
	#endif
	#if NIMERA_TERMINAL_APP_TEST || NIMERA_TERMINAL_FAULT_TEST || NIMERA_TERMINAL_CHECK_TEST
	extern const unsigned char _binary_build_user_app_keytest_elf_start[];
	extern const unsigned char _binary_build_user_app_keytest_elf_end[];
	extern const unsigned char _binary_build_user_app_faulttest_elf_start[];
	extern const unsigned char _binary_build_user_app_faulttest_elf_end[];
	extern const unsigned char _binary_build_user_app_termcheck_elf_start[];
	extern const unsigned char _binary_build_user_app_termcheck_elf_end[];
	#endif
	struct { const char *path; const unsigned char *start; const unsigned char *end; }
		payloads[] = {
			{"/apps/hello", _binary_build_user_app_hello_elf_start,
			 _binary_build_user_app_hello_elf_end},
			{"/apps/cat", _binary_build_user_app_cat_elf_start,
			 _binary_build_user_app_cat_elf_end},
			{"/apps/filetest", _binary_build_user_app_filetest_elf_start,
			 _binary_build_user_app_filetest_elf_end},
			{"/apps/edit", _binary_build_user_app_edit_elf_start,
			 _binary_build_user_app_edit_elf_end},
			{"/apps/ls", _binary_build_user_app_ls_elf_start,
			 _binary_build_user_app_ls_elf_end},
			{"/apps/mkdir", _binary_build_user_app_mkdir_elf_start,
			 _binary_build_user_app_mkdir_elf_end},
			{"/apps/touch", _binary_build_user_app_touch_elf_start,
			 _binary_build_user_app_touch_elf_end},
			{"/apps/rm", _binary_build_user_app_rm_elf_start,
			 _binary_build_user_app_rm_elf_end},
			{"/apps/rmdir", _binary_build_user_app_rmdir_elf_start,
			 _binary_build_user_app_rmdir_elf_end},
			{"/apps/mv", _binary_build_user_app_mv_elf_start,
			 _binary_build_user_app_mv_elf_end},
			{"/apps/pwd", _binary_build_user_app_pwd_elf_start,
			 _binary_build_user_app_pwd_elf_end},
			{"/apps/write", _binary_build_user_app_write_elf_start,
			 _binary_build_user_app_write_elf_end},
			{"/apps/append", _binary_build_user_app_append_elf_start,
			 _binary_build_user_app_append_elf_end},
			{"/apps/pidtest", _binary_build_user_app_pidtest_elf_start,
			 _binary_build_user_app_pidtest_elf_end},
			{"/apps/proctest", _binary_build_user_app_proctest_elf_start,
			 _binary_build_user_app_proctest_elf_end},
			{"/apps/procfault", _binary_build_user_app_procfault_elf_start,
			 _binary_build_user_app_procfault_elf_end},
			{"/apps/upper", _binary_build_user_app_upper_elf_start,
			 _binary_build_user_app_upper_elf_end},
			{"/apps/pipetest", _binary_build_user_app_pipetest_elf_start,
			 _binary_build_user_app_pipetest_elf_end},
			{"/apps/jobtest", _binary_build_user_app_jobtest_elf_start,
			 _binary_build_user_app_jobtest_elf_end}
			#if NIMERA_USER_GUI_TEST
			,{"/apps/guihello", _binary_build_user_app_guihello_elf_start,
			 _binary_build_user_app_guihello_elf_end},
			{"/apps/guidemo", _binary_build_user_app_guidemo_elf_start,
			 _binary_build_user_app_guidemo_elf_end}
			#endif
			#if NIMERA_NIMEDIT_GUI_TEST
			,{"/apps/nimedit", _binary_build_user_app_nimedit_elf_start,
			 _binary_build_user_app_nimedit_elf_end}
			#endif
			#if NIMERA_USER_GUI_TEST || NIMERA_GUI_RUNTIME_TEST || NIMERA_GUI_TEXTFIELD_TEST
			,{"/apps/guitest", _binary_build_user_app_guitest_elf_start,
			 _binary_build_user_app_guitest_elf_end}
			#endif
			#if NIMERA_TERMINAL_APP_TEST || NIMERA_TERMINAL_FAULT_TEST || NIMERA_TERMINAL_CHECK_TEST
			,{"/apps/keytest", _binary_build_user_app_keytest_elf_start,
			 _binary_build_user_app_keytest_elf_end},
			{"/apps/faulttest", _binary_build_user_app_faulttest_elf_start,
			 _binary_build_user_app_faulttest_elf_end},
			{"/apps/termcheck", _binary_build_user_app_termcheck_elf_start,
			 _binary_build_user_app_termcheck_elf_end}
			#endif
		};
	for (unsigned int index = 0U; index < sizeof(payloads) / sizeof(payloads[0]); ++index) {
		struct vfs_node *node;
		u64 size = (u64)(payloads[index].end - payloads[index].start);
		enum vfs_error result = vfs_resolve(root, payloads[index].path, &node);
		if (result == VFS_OK) {
			if (vfs_node_type(node) != VFS_NODE_FILE) {
				vfs_node_release(node); return -1;
			}
			vfs_node_release(node);
			continue;
		}
		result = vfs_create_file(root, payloads[index].path,
			(const char *)payloads[index].start, size, &node);
		if (result != VFS_OK) {
			console_write("payload failed: "); console_write(payloads[index].path);
			console_write(" error="); format_u64_decimal((u64)result); console_write("\r\n");
			return -1;
		}
	}
	return 0;
#else
	(void)root;
	return 0;
#endif
}
