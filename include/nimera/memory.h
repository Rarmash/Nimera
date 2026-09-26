#ifndef NIMERA_MEMORY_H
#define NIMERA_MEMORY_H

#include <nimera/types.h>

struct memory_range {
	u64 base;
	u64 size;
};

#define NIMERA_MEMORY_MAX_RESERVED_RANGES 16U
#define NIMERA_MEMORY_MAX_USABLE_RANGES 17U

struct memory_map {
	struct memory_range physical;
	struct memory_range kernel;
	struct memory_range dtb;
	struct memory_range reserved[NIMERA_MEMORY_MAX_RESERVED_RANGES];
	unsigned int reserved_count;
	u64 reserved_size;
	struct memory_range usable[NIMERA_MEMORY_MAX_USABLE_RANGES];
	unsigned int usable_count;
	u64 usable_size;
};

struct memory_map memory_discover(void);
void memory_print_map(const struct memory_map *map);

#endif
