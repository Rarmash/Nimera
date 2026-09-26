#include <nimera/heap.h>
#include <nimera/panic.h>
#include <nimera/pmm.h>

#define NULL ((void *)0)
#define HEAP_BLOCK_MAGIC 0x4e494d4552414842ULL
#define HEAP_BLOCK_FREE 0ULL
#define HEAP_BLOCK_ALLOCATED 1ULL
#define HEAP_ALIGNMENT 16ULL

struct heap_block {
	u64 magic;
	u64 size;
	u64 requested;
	u64 state;
	struct heap_block *next;
	u64 reserved;
};

_Static_assert(sizeof(struct heap_block) % HEAP_ALIGNMENT == 0,
	       "heap metadata must preserve 16-byte alignment");

static struct heap_block *heap_head;
static u64 reserved_bytes;
static u64 allocated_bytes;
static unsigned int initialized;

static u64 align_size(u64 size)
{
	if (size > ~0ULL - (HEAP_ALIGNMENT - 1ULL)) {
		return 0ULL;
	}
	return (size + HEAP_ALIGNMENT - 1ULL) & ~(HEAP_ALIGNMENT - 1ULL);
}

static void check_block(const struct heap_block *block)
{
	if (block->magic != HEAP_BLOCK_MAGIC ||
	    (block->state != HEAP_BLOCK_FREE &&
	     block->state != HEAP_BLOCK_ALLOCATED)) {
		panic("heap block metadata is corrupt");
	}
}

static void coalesce_free_blocks(void)
{
	struct heap_block *block = heap_head;

	while (block != NULL && block->next != NULL) {
		struct heap_block *next = block->next;
		unsigned long block_end = (unsigned long)(block + 1) + block->size;

		check_block(block);
		check_block(next);
		if (block->state == HEAP_BLOCK_FREE &&
		    next->state == HEAP_BLOCK_FREE &&
		    block_end == (unsigned long)next) {
			block->size += sizeof(struct heap_block) + next->size;
			block->next = next->next;
			continue;
		}
		block = next;
	}
}

static int add_page(void)
{
	u64 physical_address;
	struct heap_block *page;
	struct heap_block **link;

	if (pmm_alloc_page(&physical_address) != 0) {
		return -1;
	}
	page = (struct heap_block *)(unsigned long)physical_address;
	page->magic = HEAP_BLOCK_MAGIC;
	page->size = NIMERA_PAGE_SIZE - sizeof(struct heap_block);
	page->requested = 0ULL;
	page->state = HEAP_BLOCK_FREE;
	page->next = NULL;
	page->reserved = 0ULL;
	reserved_bytes += NIMERA_PAGE_SIZE;

	link = &heap_head;
	while (*link != NULL && (unsigned long)*link < (unsigned long)page) {
		link = &(*link)->next;
	}
	page->next = *link;
	*link = page;
	coalesce_free_blocks();
	return 0;
}

void heap_init(void)
{
	if (initialized != 0U) {
		panic("heap initialized twice");
	}
	heap_head = NULL;
	reserved_bytes = 0ULL;
	allocated_bytes = 0ULL;
	initialized = 1U;
}

void *kmalloc(u64 size)
{
	u64 aligned_size;
	struct heap_block *block;

	if (initialized == 0U) {
		panic("heap used before heap_init");
	}
	if (size == 0ULL) {
		return NULL;
	}
	aligned_size = align_size(size);
	if (aligned_size == 0ULL ||
	    aligned_size > NIMERA_PAGE_SIZE - sizeof(struct heap_block)) {
		return NULL;
	}

	for (;;) {
		for (block = heap_head; block != NULL; block = block->next) {
			check_block(block);
			if (block->state == HEAP_BLOCK_FREE &&
			    block->size >= aligned_size) {
				if (block->size >= aligned_size +
				    sizeof(struct heap_block) + HEAP_ALIGNMENT) {
					struct heap_block *split =
						(struct heap_block *)((unsigned long)(block + 1) +
								     aligned_size);

					split->magic = HEAP_BLOCK_MAGIC;
					split->size = block->size - aligned_size -
						sizeof(struct heap_block);
					split->requested = 0ULL;
					split->state = HEAP_BLOCK_FREE;
					split->next = block->next;
					split->reserved = 0ULL;
					block->next = split;
					block->size = aligned_size;
				}
				block->requested = size;
				block->state = HEAP_BLOCK_ALLOCATED;
				allocated_bytes += size;
				return (void *)(block + 1);
			}
		}
		if (add_page() != 0) {
			return NULL;
		}
	}
}

void kfree(void *pointer)
{
	struct heap_block *block;

	if (pointer == NULL) {
		return;
	}
	if (initialized == 0U) {
		panic("heap used before heap_init");
	}
	for (block = heap_head; block != NULL; block = block->next) {
		check_block(block);
		if ((void *)(block + 1) == pointer) {
			if (block->state != HEAP_BLOCK_ALLOCATED) {
				panic("heap double free");
			}
			block->state = HEAP_BLOCK_FREE;
			allocated_bytes -= block->requested;
			block->requested = 0ULL;
			coalesce_free_blocks();
			return;
		}
	}
	panic("heap free pointer is invalid");
}

u64 heap_allocated_bytes(void)
{
	return allocated_bytes;
}

u64 heap_reserved_bytes(void)
{
	return reserved_bytes;
}

u64 heap_reusable_bytes(void)
{
	u64 total = 0ULL;

	for (struct heap_block *block = heap_head; block != NULL;
	     block = block->next) {
		check_block(block);
		if (block->state == HEAP_BLOCK_FREE) {
			total += block->size;
		}
	}
	return total;
}
