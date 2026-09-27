#include "fsutil.h"

int main(void)
{
	char path[NIMERA_PATH_MAX];
	long long result = nimera_getcwd(path, sizeof(path));
	if (result < 0LL) { nimera_print_error("pwd", result); return 1; }
	nimera_print(path);
	nimera_print("\n");
	return 0;
}
