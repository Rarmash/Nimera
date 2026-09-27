#include "../../runtime/fsutil.h"

static unsigned long long length(const char *text)
{
	unsigned long long n = 0ULL;
	while (text[n] != '\0') ++n;
	return n;
}

int main(int argc, char **argv)
{
	char buffer[128];
	long long handle;
	if (argc > 2) {
		const char message[] = "cat: usage: cat [path]\n";
		(void)nimera_write(NIMERA_STDERR, message, sizeof(message) - 1ULL); return 1;
	}
	if (argc == 1) {
		for (;;) {
			long long count = nimera_read(NIMERA_STDIN, buffer, sizeof(buffer));
			if (count == 0LL) return 0;
			if (count < 0LL) {
				const char message[] = "cat: stdin is not readable\n";
				(void)nimera_write(NIMERA_STDERR, message, sizeof(message) - 1ULL);
				return 1;
			}
			if (nimera_write_all(NIMERA_STDOUT, buffer, (unsigned long long)count) != count)
				return 1;
		}
	}
	handle = nimera_open(argv[1], length(argv[1]), NIMERA_OPEN_READ);
	if (handle < 0) {
		const char message[] = "cat: open failed\n";
		(void)nimera_write(NIMERA_STDERR, message, sizeof(message) - 1ULL); return 1;
	}
	for (;;) {
		long long count = nimera_read((unsigned long long)handle, buffer, sizeof(buffer));
		if (count <= 0) break;
		(void)nimera_write_all(NIMERA_STDOUT, buffer, (unsigned long long)count);
	}
	(void)nimera_close((unsigned long long)handle);
	return 0;
}
