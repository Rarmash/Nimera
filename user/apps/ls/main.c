#include "fsutil.h"

int main(int argc, char **argv)
{
	struct nimera_dir_entry entry;
	const char *path = argc == 1 ? "." : argc == 2 ? argv[1] : (const char *)0;
	long long handle;
	if (path == (const char *)0) { nimera_print("ls: usage: ls [path]\n"); return 1; }
	handle = nimera_open_directory(path, nimera_text_length(path));
	if (handle < 0) { nimera_print_error("ls", handle); return 1; }
	for (;;) {
		long long result = nimera_read_directory((unsigned long long)handle, &entry);
		if (result == 0) break;
		if (result < 0) { nimera_print_error("ls", result); (void)nimera_close((unsigned long long)handle); return 1; }
		nimera_print(entry.name); nimera_print("\n");
	}
	(void)nimera_close((unsigned long long)handle);
	return 0;
}
