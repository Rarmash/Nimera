#ifndef NIMERA_USER_EDITOR_H
#define NIMERA_USER_EDITOR_H

#include <nimera/user.h>

struct user_editor {
	char *data;
	unsigned long long length;
	unsigned long long capacity;
	unsigned long long cursor;
	unsigned long long preferred_column;
	unsigned int preferred_valid;
	unsigned int dirty;
};

void user_editor_init(struct user_editor *editor);
void user_editor_destroy(struct user_editor *editor);
int user_editor_reserve(struct user_editor *editor, unsigned long long required);
int user_editor_insert(struct user_editor *editor, char value);
void user_editor_backspace(struct user_editor *editor);
void user_editor_delete(struct user_editor *editor);
unsigned long long user_editor_line_start(const struct user_editor *editor,
	unsigned long long offset);
unsigned long long user_editor_line_end(const struct user_editor *editor,
	unsigned long long offset);
unsigned long long user_editor_line_count(const struct user_editor *editor);
unsigned long long user_editor_line_number(const struct user_editor *editor,
	unsigned long long offset);
unsigned long long user_editor_line_start_number(const struct user_editor *editor,
	unsigned long long line);
void user_editor_move_vertical(struct user_editor *editor, int direction);
void user_editor_move_home(struct user_editor *editor);
void user_editor_move_end(struct user_editor *editor);

#endif
