#include <nimera/elf.h>
#include <nimera/abi/syscall.h>
#include <nimera/mmu.h>
#include <nimera/pmm.h>
#include <nimera/scheduler.h>
#include <nimera/terminal.h>
#include <nimera/user.h>
#include <nimera/vfs.h>

#define ELF_USER_BASE 0x10000000ULL
#define ELF_USER_LIMIT 0x20000000ULL
#define ELF_DYNAMIC_BASE 0x18000000ULL
#define ELF_DYNAMIC_LIMIT 0x1f000000ULL
#define ELF_STACK_PAGES 4ULL
#define ELF_STACK_BASE (ELF_USER_LIMIT - ELF_STACK_PAGES * NIMERA_PAGE_SIZE)
#define ELF_MAX_PHNUM 16U
#define ELF_MAX_PAGES 256U
#define ELF_MAX_FILE_SIZE (64ULL * 1024ULL)
#define ELF_MAX_ALLOCATIONS 32U
#define ELF_MAX_ALLOCATION_PAGES 256U
#define PT_LOAD 1U
#define PF_X 1U
#define PF_W 2U
#define PF_R 4U

struct loaded_page {
	u64 virtual_address;
	u64 physical_address;
	unsigned int permissions;
};

static struct loaded_page pages[ELF_MAX_PAGES];
static unsigned int page_count;
static unsigned int active;
static unsigned char image_storage[ELF_MAX_FILE_SIZE];
static char *image;
static struct vfs_node *user_cwd;

#define ELF_MAX_HANDLES 16U
struct user_handle {
	unsigned int in_use;
	struct vfs_node *node;
	u64 offset;
	u64 flags;
};
static struct user_handle handles[ELF_MAX_HANDLES];

struct user_allocation {
	unsigned int in_use;
	u64 virtual_address;
	u64 page_count;
	u64 physical_pages[ELF_MAX_ALLOCATION_PAGES];
};
static struct user_allocation allocations[ELF_MAX_ALLOCATIONS];

static void clear_handle(unsigned int index)
{
	handles[index].in_use = 0U;
	handles[index].node = (struct vfs_node *)0;
	handles[index].offset = 0ULL;
	handles[index].flags = 0ULL;
}

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
	for (unsigned int index = 0U; index < page_count; ++index) {
		(void)mmu_unmap_user_page(pages[index].virtual_address);
		pmm_free_page(pages[index].physical_address);
	}
	page_count = 0U;
}

static void clear_dynamic_allocations(void)
{
	for (unsigned int index = 0U; index < ELF_MAX_ALLOCATIONS; ++index) {
		if (allocations[index].in_use == 0U) continue;
		for (u64 page = 0ULL; page < allocations[index].page_count; ++page) {
			(void)mmu_unmap_user_page(allocations[index].virtual_address +
				page * NIMERA_PAGE_SIZE);
			pmm_free_page(allocations[index].physical_pages[page]);
		}
		allocations[index].in_use = 0U;
	}
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
	for (unsigned int index = 0U; index < page_count; ++index)
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
	if (page_count == ELF_MAX_PAGES) return -1;
	page = &pages[page_count++];
	page->virtual_address = virtual_address;
	page->permissions = permissions;
	if (pmm_alloc_page(&page->physical_address) != 0) {
		--page_count;
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
	struct vfs_node *file;
	u64 size = 0ULL;
	u64 entry;
	u64 phoff;
	u16 phentsize;
	u16 phnum;
	unsigned int load_segments = 0U;
	int entry_executable = 0;
	enum vfs_error vfs_result;

	if (active != 0U) return ELF_BUSY;
	if (argument_count != 0U && arguments == (const struct elf_argument *)0)
		return ELF_INVALID;
	vfs_result = vfs_resolve(cwd, path, &file);
	if (vfs_result != VFS_OK) return ELF_NOT_FOUND;
	if (vfs_node_type(file) != VFS_NODE_FILE) {
		vfs_node_release(file); return ELF_INVALID;
	}
	if (vfs_get_size(file, &size) != VFS_OK || size < 64ULL ||
		size > ELF_MAX_FILE_SIZE) {
		vfs_node_release(file); return ELF_INVALID;
	}
	image = (char *)(void *)image_storage;
	if (vfs_read(file, image, size, &size) != VFS_OK) {
		vfs_node_release(file); image = (char *)0; return ELF_IO;
	}
	vfs_node_release(file);
	{
		const unsigned char *h = (const unsigned char *)(const void *)image;
		if (h[0] != 0x7fU || h[1] != 'E' || h[2] != 'L' || h[3] != 'F' ||
		    h[4] != 2U || h[5] != 1U || h[6] != 1U || read16(h + 16) != 2U ||
		    read16(h + 18) != 183U || read32(h + 20) != 1U ||
		    read16(h + 52) != 64U) {
			image = (char *)0; return ELF_UNSUPPORTED;
		}
		if (read16(h + 54) != 56U || read16(h + 56) == 0U) {
			image = (char *)0; return ELF_UNSUPPORTED;
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
				image = (char *)0; return ELF_INVALID;
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
					clear_loaded_pages(); image = (char *)0; return ELF_INVALID;
				}
				if (copy_end > copy_start)
					for (u64 byte = copy_start; byte < copy_end; ++byte)
						((unsigned char *)(unsigned long)page->physical_address)
							[byte - page_address] = (unsigned char)image[
								offset + byte - virtual_address];
			}
		}
		if (load_segments == 0U || entry_executable == 0) {
			clear_loaded_pages(); image = (char *)0; return ELF_INVALID;
		}
	}
	if (entry < ELF_USER_BASE || entry >= ELF_USER_LIMIT) {
		clear_loaded_pages(); image = (char *)0; return ELF_INVALID;
	}
	for (u64 index = 0ULL; index < ELF_STACK_PAGES; ++index) {
		struct loaded_page *page;
		if (add_page(ELF_STACK_BASE + index * NIMERA_PAGE_SIZE,
				MMU_USER_READ | MMU_USER_WRITE, &page) != 0) {
			clear_loaded_pages(); image = (char *)0; return ELF_NO_MEMORY;
		}
	}
	{
		u64 argc;
		u64 argv;
		for (unsigned int index = 0U; index < page_count; ++index)
			if (mmu_map_user_page(pages[index].virtual_address,
				pages[index].physical_address, pages[index].permissions) != 0) {
				clear_loaded_pages(); image = (char *)0; return ELF_INVALID;
			}
		if (build_user_stack(arguments, argument_count, &argc, &argv) != 0) {
			clear_loaded_pages(); image = (char *)0; return ELF_INVALID;
		}
		for (unsigned int index = 0U; index < ELF_MAX_HANDLES; ++index)
			clear_handle(index);
		user_cwd = cwd;
		active = 1U;
		scheduler_enable_user_task_argv(entry, ELF_USER_LIMIT, argc, argv);
		return ELF_OK;
	}
}

void elf_user_task_finished(void)
{
	if (active == 0U) return;
	terminal_show_cursor();
	elf_user_close_all();
	clear_dynamic_allocations();
	clear_loaded_pages();
	image = (char *)0;
	user_cwd = (struct vfs_node *)0;
	active = 0U;
}

long long elf_user_alloc(u64 bytes)
{
	u64 page_count;
	unsigned int slot = ELF_MAX_ALLOCATIONS;
	u64 address;

	if (active == 0U || bytes == 0ULL || bytes >
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
				(void)mmu_unmap_user_page(address + rollback * NIMERA_PAGE_SIZE);
				pmm_free_page(allocations[slot].physical_pages[rollback]);
			}
			allocations[slot].page_count = 0ULL;
			return NIMERA_NERR_NO_MEMORY;
		}
		if (mmu_map_user_page(address + page * NIMERA_PAGE_SIZE, physical,
			MMU_USER_READ | MMU_USER_WRITE) != 0) {
			pmm_free_page(physical);
			for (u64 rollback = 0ULL; rollback < page; ++rollback) {
				(void)mmu_unmap_user_page(address + rollback * NIMERA_PAGE_SIZE);
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
			(void)mmu_unmap_user_page(address + page * NIMERA_PAGE_SIZE);
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
	return active != 0U;
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
	if (error == VFS_TOO_LARGE) return NIMERA_NERR_TOO_LARGE;
	return NIMERA_NERR_IO;
}

long long elf_user_open(const char *path, u64 flags)
{
	struct vfs_node *node;
	u64 size = 0ULL;
	unsigned int slot;
	unsigned int access = (unsigned int)(flags & (NIMERA_OPEN_READ | NIMERA_OPEN_WRITE));
	enum vfs_error error;
	if (active == 0U || user_cwd == (struct vfs_node *)0 || path == (const char *)0 ||
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
	handles[slot].node = node;
	handles[slot].offset = (flags & NIMERA_OPEN_APPEND) != 0ULL ? size : 0ULL;
	handles[slot].flags = flags;
	return (long long)slot;
}

long long elf_user_read(unsigned int handle, char *buffer, u64 length)
{
	u64 completed = 0ULL;
	enum vfs_error error;
	if (handle >= ELF_MAX_HANDLES || handles[handle].in_use == 0U ||
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
	if (handle >= ELF_MAX_HANDLES || handles[handle].in_use == 0U ||
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

long long elf_user_close(unsigned int handle)
{
	if (handle >= ELF_MAX_HANDLES || handles[handle].in_use == 0U)
		return NIMERA_NERR_BAD_HANDLE;
	vfs_node_release(handles[handle].node);
	clear_handle(handle);
	return 0LL;
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
			 _binary_build_user_app_edit_elf_end}
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
		if (result != VFS_OK) return -1;
	}
	return 0;
#else
	(void)root;
	return 0;
#endif
}
