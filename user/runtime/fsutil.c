#include "fsutil.h"

unsigned long long nimera_text_length(const char *text)
{
	unsigned long long length = 0ULL;
	while (text[length] != '\0') ++length;
	return length;
}

void nimera_print(const char *text)
{
	(void)nimera_write(text, nimera_text_length(text));
}

void nimera_print_error(const char *program, long long error)
{
	nimera_print(program);
	nimera_print(": ");
	if (error == NIMERA_NERR_NOT_FOUND) nimera_print("No such file or directory");
	else if (error == NIMERA_NERR_NOT_DIRECTORY) nimera_print("Not a directory");
	else if (error == NIMERA_NERR_IS_DIRECTORY) nimera_print("Is a directory");
	else if (error == NIMERA_NERR_EXISTS) nimera_print("Already exists");
	else if (error == NIMERA_NERR_NOT_EMPTY) nimera_print("Directory is not empty");
	else if (error == NIMERA_NERR_BUSY) nimera_print("Volume is busy");
	else if (error == NIMERA_NERR_CROSS_DEVICE)
		nimera_print("Cannot move across filesystems");
	else nimera_print("operation failed");
	nimera_print("\n");
}

long long nimera_write_all(unsigned long long handle, const char *buffer,
			   unsigned long long length)
{
	unsigned long long total = 0ULL;
	while (total < length) {
		long long result = nimera_write_file(handle, buffer + total, length - total);
		if (result <= 0LL) return total != 0ULL ? (long long)total : result;
		total += (unsigned long long)result;
	}
	return (long long)total;
}
