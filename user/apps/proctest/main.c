#include "fsutil.h"

static volatile unsigned long long private_data = 0x13579bdfULL;
static volatile unsigned long long private_bss;

int main(void)
{
	unsigned long long pid = (unsigned long long)nimera_getpid();
	volatile unsigned long long work = 0ULL;
	void *memory = nimera_alloc(4096ULL);
	if (memory == (void *)0) {
		nimera_print("proctest: alloc failed\n");
		return 1;
	}
	private_bss = pid;
	*(unsigned long long *)memory = pid ^ private_data;
	for (unsigned long long index = 0ULL; index < 3000000000ULL; ++index)
		work += index ^ private_bss;
	if (*(unsigned long long *)memory != (pid ^ private_data) || work == 0ULL) {
		nimera_print("proctest: private state failed\n");
		return 1;
	}
	nimera_print("proctest PID ");
	nimera_print_u64(pid);
	nimera_print(" OK\n");
	(void)nimera_free(memory);
	return 0;
}
