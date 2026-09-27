#include <nimera/user.h>

int main(void)
{
	volatile unsigned long long *kernel_memory =
		(volatile unsigned long long *)(unsigned long)0x40000000ULL;
	(void)nimera_terminal_cursor_visible(0);
	*kernel_memory = 0x4e494d455241ULL;
	return 1;
}
