#include "fsutil.h"

int main(int argc, char **argv)
{
	long long result;
	if (argc != 2) { nimera_print("mkdir: usage: mkdir <path>\n"); return 1; }
	result = nimera_mkdir(argv[1], nimera_text_length(argv[1]));
	if (result != 0) { nimera_print_error("mkdir", result); return 1; }
	return 0;
}
