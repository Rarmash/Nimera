#include "fsutil.h"

int main(int argc, char **argv)
{
	long long handle;
	long long result;
	unsigned long long length;
	if (argc != 3) { nimera_print("write: usage: write <path> <text>\n"); return 1; }
	length = nimera_text_length(argv[2]);
	handle = nimera_open(argv[1], nimera_text_length(argv[1]),
		NIMERA_OPEN_WRITE | NIMERA_OPEN_CREATE | NIMERA_OPEN_TRUNCATE);
	if (handle < 0) { nimera_print_error("write", handle); return 1; }
	result = nimera_write_all((unsigned long long)handle, argv[2], length);
	(void)nimera_close((unsigned long long)handle);
	if (result != (long long)length) { nimera_print_error("write", result); return 1; }
	return 0;
}
