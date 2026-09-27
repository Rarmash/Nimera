#include "fsutil.h"

int main(void)
{
	nimera_print("PID: ");
	nimera_print_u64((unsigned long long)nimera_getpid());
	nimera_print("\n");
	return 0;
}
