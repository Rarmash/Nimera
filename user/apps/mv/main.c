#include "fsutil.h"

int main(int argc, char **argv)
{
	long long result;
	if (argc != 3) { nimera_print("mv: usage: mv <source> <destination>\n"); return 1; }
	result = nimera_rename(argv[1], nimera_text_length(argv[1]),
		argv[2], nimera_text_length(argv[2]));
	if (result != 0) { nimera_print_error("mv", result); return 1; }
	return 0;
}
