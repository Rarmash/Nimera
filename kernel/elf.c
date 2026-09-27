#include <nimera/elf.h>
#include <nimera/mmu.h>
#include <nimera/pmm.h>
#include <nimera/scheduler.h>
#include <nimera/user.h>
#include <nimera/vfs.h>

#define ELF_USER_BASE 0x10000000ULL
#define ELF_USER_LIMIT 0x20000000ULL
#define ELF_STACK_PAGES 4ULL
#define ELF_STACK_BASE (ELF_USER_LIMIT - ELF_STACK_PAGES * NIMERA_PAGE_SIZE)
#define ELF_MAX_PHNUM 16U
#define ELF_MAX_PAGES 256U
#define ELF_MAX_FILE_SIZE (64ULL * 1024ULL)
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

enum elf_result elf_load_user(struct vfs_node *cwd, const char *path)
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
	vfs_result = vfs_resolve(cwd, path, &file);
	if (vfs_result != VFS_OK) return ELF_NOT_FOUND;
	if (vfs_node_type(file) != VFS_NODE_FILE) return ELF_INVALID;
	if (vfs_get_size(file, &size) != VFS_OK || size < 64ULL ||
	    size > ELF_MAX_FILE_SIZE)
		return ELF_INVALID;
	image = (char *)(void *)image_storage;
	if (vfs_read(file, image, size, &size) != VFS_OK) {
		image = (char *)0; return ELF_IO;
	}
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
	for (unsigned int index = 0U; index < page_count; ++index)
		if (mmu_map_user_page(pages[index].virtual_address,
				pages[index].physical_address, pages[index].permissions) != 0) {
			clear_loaded_pages(); image = (char *)0; return ELF_INVALID;
		}
	active = 1U;
	scheduler_enable_user_task(entry, ELF_USER_LIMIT, 0ULL);
	return ELF_OK;
}

void elf_user_task_finished(void)
{
	if (active == 0U) return;
	clear_loaded_pages();
	image = (char *)0;
	active = 0U;
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

int elf_install_test_payload(struct vfs_node *root)
{
#if NIMERA_ELF_INSTALL_TEST
	extern const unsigned char _binary_build_user_app_hello_elf_start[];
	extern const unsigned char _binary_build_user_app_hello_elf_end[];
	struct vfs_node *node;
	enum vfs_error result;
	u64 size = (u64)(_binary_build_user_app_hello_elf_end -
		_binary_build_user_app_hello_elf_start);

	if (vfs_resolve(root, "/apps/hello", &node) == VFS_OK) {
		return vfs_node_type(node) == VFS_NODE_FILE ? 0 : -1;
	}
	result = vfs_create_file(root, "/apps/hello",
		(const char *)_binary_build_user_app_hello_elf_start, size, &node);
	return result == VFS_OK ? 0 : -1;
#else
	(void)root;
	return 0;
#endif
}
