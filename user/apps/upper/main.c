#include "../../runtime/fsutil.h"

int main(void)
{
	char buffer[256];
	for (;;) {
		long long count = nimera_read(NIMERA_STDIN, buffer, sizeof(buffer));
		if (count == 0LL) return 0;
		if (count < 0LL) {
			nimera_print_error("upper", count);
			return 1;
		}
		for (long long index = 0LL; index < count; ++index)
			if (buffer[index] >= 'a' && buffer[index] <= 'z')
				buffer[index] = (char)(buffer[index] - 'a' + 'A');
		if (nimera_write_all(NIMERA_STDOUT, buffer, (unsigned long long)count) != count)
			return 1;
	}
}
