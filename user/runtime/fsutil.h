#ifndef NIMERA_USER_FSUTIL_H
#define NIMERA_USER_FSUTIL_H

#include <nimera/user.h>

unsigned long long nimera_text_length(const char *text);
void nimera_print(const char *text);
void nimera_print_error(const char *program, long long error);
long long nimera_write_all(unsigned long long handle, const char *buffer,
			   unsigned long long length);

#endif
