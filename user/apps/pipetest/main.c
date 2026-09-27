#include "../../runtime/fsutil.h"

static int same(const char *left, const char *right)
{
	unsigned int index = 0U;
	while (left[index] != '\0' || right[index] != '\0') {
		if (left[index] != right[index]) return 0;
		++index;
	}
	return 1;
}

static unsigned long long number(const char *text)
{
	unsigned long long value = 0ULL;
	for (unsigned int index = 0U; text[index] >= '0' && text[index] <= '9'; ++index)
		value = value * 10ULL + (unsigned long long)(text[index] - '0');
	return value;
}

static char pattern(unsigned long long offset)
{
	return (char)('a' + (offset % 26ULL));
}

static int fault_now(void)
{
	volatile unsigned long long *unmapped =
		(volatile unsigned long long *)(unsigned long)0x40000000ULL;
	*unmapped = 0ULL;
	return 1;
}

int main(int argc, char **argv)
{
	char buffer[256];
	unsigned long long total = 0ULL;
	unsigned long long expected;
	if (argc < 2) return 1;
	if (same(argv[1], "producer") || same(argv[1], "broken-producer")) {
		for (unsigned long long offset = 0ULL; offset < 16384ULL;) {
			unsigned long long count = 16384ULL - offset;
			if (count > sizeof(buffer)) count = sizeof(buffer);
			for (unsigned long long index = 0ULL; index < count; ++index)
				buffer[index] = pattern(offset + index);
			long long result = nimera_write(NIMERA_STDOUT, buffer, count);
			if (result == NIMERA_NERR_BROKEN_PIPE)
				return same(argv[1], "broken-producer") ? 0 : 1;
			if (result <= 0LL) return 1;
			offset += (unsigned long long)result;
		}
		return 0;
	}
	if (same(argv[1], "fault-producer")) {
		for (unsigned int index = 0U; index < 64U; ++index) buffer[index] = pattern(index);
		if (nimera_write_all(NIMERA_STDOUT, buffer, 64ULL) != 64LL) return 1;
		return fault_now();
	}
	if (same(argv[1], "fault-consumer")) return fault_now();
	if (!same(argv[1], "checker") || argc < 3) return 1;
	expected = number(argv[2]);
	for (;;) {
		long long result = nimera_read(NIMERA_STDIN, buffer, sizeof(buffer));
		if (result == 0LL) break;
		if (result < 0LL) return 1;
		for (long long index = 0LL; index < result; ++index)
			if (total >= expected || buffer[index] != pattern(total + (unsigned long long)index))
				return 1;
		total += (unsigned long long)result;
	}
	if (total != expected) return 1;
	nimera_print("pipe checker: exact bytes/order OK\n");
	return 0;
}
