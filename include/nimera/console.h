#ifndef NIMERA_CONSOLE_H
#define NIMERA_CONSOLE_H

void console_putc(char c);
void console_write(const char *text);
char console_getc(void);
int console_try_getc(char *result);

typedef void (*console_putc_sink)(char character);
typedef void (*console_write_sink)(const char *text);

void console_set_output(console_putc_sink putc, console_write_sink write);
void console_reset_output(void);
void debug_console_putc(char character);
void debug_console_write(const char *text);

#endif
