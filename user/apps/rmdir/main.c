#include "fsutil.h"

int main(int argc, char **argv)
{
	long long result;
	if (argc != 2) { nimera_print("rmdir: usage: rmdir <path>\n"); return 1; }
	result = nimera_rmdir(argv[1], nimera_text_length(argv[1]));
	if (result != 0) { nimera_print_error("rmdir", result); return 1; }
	return 0;
}
