#ifndef NIMERA_HEAP_H
#define NIMERA_HEAP_H

#include <nimera/types.h>

void heap_init(void);
void *kmalloc(u64 size);
void kfree(void *pointer);

u64 heap_allocated_bytes(void);
u64 heap_reserved_bytes(void);
u64 heap_reusable_bytes(void);

#endif
