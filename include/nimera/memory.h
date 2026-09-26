#ifndef NIMERA_MEMORY_H
#define NIMERA_MEMORY_H

#include <nimera/types.h>

struct memory_info {
	u64 physical_base;
	u64 physical_size;
};

struct memory_info memory_discover(void);

#endif
