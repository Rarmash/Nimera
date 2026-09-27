#ifndef NIMERA_TERMINAL_H
#define NIMERA_TERMINAL_H

#include <nimera/input.h>

#define TERMINAL_DEFAULT_ROWS 25U
#define TERMINAL_DEFAULT_COLUMNS 80U

struct key_event terminal_read_key(void);

void terminal_init(void);
int terminal_framebuffer_active(void);
unsigned int terminal_geometry_detected(void);
int terminal_geometry_self_test(void);

void terminal_clear(void);
void terminal_move_cursor(unsigned int row, unsigned int column);
void terminal_clear_line(void);
void terminal_hide_cursor(void);
void terminal_show_cursor(void);
unsigned int terminal_rows(void);
unsigned int terminal_columns(void);

#endif
