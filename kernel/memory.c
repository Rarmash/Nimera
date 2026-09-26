#include <nimera/memory.h>

extern struct memory_info platform_memory_discover(void);

struct memory_info memory_discover(void)
{
	return platform_memory_discover();
}
