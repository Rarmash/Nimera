#include <nimera/console.h>
#include <nimera/format.h>

void format_u64_hex(u64 value)
{
	static const char digits[] = "0123456789abcdef";
	char reversed[16];
	unsigned int count = 0U;

	console_write("0x");
	do {
		reversed[count++] = digits[value & 0xfULL];
		value >>= 4;
	} while (value != 0ULL);

	while (count != 0U) {
		console_putc(reversed[--count]);
	}
}

void format_u64_decimal(u64 value)
{
	char reversed[20];
	unsigned int count = 0U;

	do {
		reversed[count++] = (char)('0' + (value % 10ULL));
		value /= 10ULL;
	} while (value != 0ULL);

	while (count != 0U) {
		console_putc(reversed[--count]);
	}
}
