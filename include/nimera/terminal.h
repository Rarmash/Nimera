#ifndef NIMERA_TERMINAL_H
#define NIMERA_TERMINAL_H

#include <nimera/types.h>

#define TERMINAL_DEFAULT_ROWS 25U
#define TERMINAL_DEFAULT_COLUMNS 80U

enum key_code {
	KEY_CHAR,
	KEY_ENTER,
	KEY_BACKSPACE,
	KEY_DELETE,
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_HOME,
	KEY_END,
	KEY_ESCAPE
};

struct key_event {
	enum key_code code;
	char ch;
	unsigned int ctrl;
};

struct key_event terminal_read_key(void);

void terminal_init(void);
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
