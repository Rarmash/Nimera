#include "fsutil.h"

int main(int argc, char **argv)
{
	long long handle;
	if (argc != 2) { nimera_print("touch: usage: touch <path>\n"); return 1; }
	handle = nimera_open(argv[1], nimera_text_length(argv[1]),
		NIMERA_OPEN_WRITE | NIMERA_OPEN_CREATE);
	if (handle < 0) { nimera_print_error("touch", handle); return 1; }
	return nimera_close((unsigned long long)handle) == 0 ? 0 : 1;
}
