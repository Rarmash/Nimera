#ifndef NIMERA_TERMINAL_FB_H
#define NIMERA_TERMINAL_FB_H

#include <nimera/types.h>
#include <nimera/input.h>

int terminal_fb_init(void);
void terminal_fb_putc(char character);
void terminal_fb_write(const char *text);
void terminal_fb_clear(void);
void terminal_fb_move_cursor(unsigned int row, unsigned int column);
void terminal_fb_clear_line(void);
void terminal_fb_hide_cursor(void);
void terminal_fb_show_cursor(void);
unsigned int terminal_fb_rows(void);
unsigned int terminal_fb_columns(void);
int terminal_fb_self_test(void);
void terminal_fb_handle_pointer_event(const struct pointer_event *event);

#endif
