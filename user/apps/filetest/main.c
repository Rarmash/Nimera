#include <nimera/user.h>

static const char path[] = "/tmp/user-syscall-test.txt";
static const char first[] = "range ";
static const char second[] = "I/O works\n";
static const char ok[] = "file syscalls: OK\n";
static const char fail[] = "file syscalls: FAIL\n";

static unsigned long long length(const char *text)
{
	unsigned long long n = 0ULL;
	while (text[n] != '\0') ++n;
	return n;
}

int main(int argc, char **argv)
{
	char buffer[32];
	long long handle;
	long long count;
	(void)argc; (void)argv;
	handle = nimera_open(path, length(path), NIMERA_OPEN_WRITE | NIMERA_OPEN_CREATE |
		NIMERA_OPEN_TRUNCATE);
	if (handle < 0 || nimera_write_file((unsigned long long)handle, first,
		length(first)) != (long long)length(first) ||
		nimera_write_file((unsigned long long)handle, second, length(second)) !=
		(long long)length(second) || nimera_close((unsigned long long)handle) != 0) {
		(void)nimera_write(fail, sizeof(fail) - 1ULL); return 1;
	}
	handle = nimera_open(path, length(path), NIMERA_OPEN_READ);
	if (handle < 0) { (void)nimera_write(fail, sizeof(fail) - 1ULL); return 1; }
	count = nimera_read((unsigned long long)handle, buffer, sizeof(buffer));
	if (count != (long long)(sizeof(first) + sizeof(second) - 2ULL) ||
		nimera_close((unsigned long long)handle) != 0) {
		(void)nimera_write(fail, sizeof(fail) - 1ULL); return 1;
	}
	(void)nimera_write(ok, sizeof(ok) - 1ULL);
	return 0;
}
