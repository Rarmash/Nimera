#include "fsutil.h"

int main(int argc, char **argv)
{
	long long result;
	if (argc != 2) { nimera_print("rm: usage: rm <path>\n"); return 1; }
	result = nimera_unlink(argv[1], nimera_text_length(argv[1]));
	if (result != 0) { nimera_print_error("rm", result); return 1; }
	return 0;
}
