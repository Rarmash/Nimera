#ifndef NIMERA_EDITOR_H
#define NIMERA_EDITOR_H

#include <nimera/user.h>

/* UI-independent UTF-8 document model used by both NimEdit frontends. */
struct user_editor {
	char *data;
	u64 length;
	u64 capacity;
	u64 cursor;
	u64 preferred_column;
	unsigned int preferred_valid;
	unsigned int dirty;
};

void user_editor_init(struct user_editor *editor);
void user_editor_destroy(struct user_editor *editor);
int user_editor_reserve(struct user_editor *editor, u64 required);
int user_editor_set_text(struct user_editor *editor, const char *text, u64 length);
int user_editor_insert_codepoint(struct user_editor *editor, u32 codepoint);
int user_editor_insert(struct user_editor *editor, char value);
void user_editor_backspace(struct user_editor *editor);
void user_editor_delete(struct user_editor *editor);
u64 user_editor_previous_boundary(const struct user_editor *editor, u64 offset);
u64 user_editor_next_boundary(const struct user_editor *editor, u64 offset);
int user_editor_decode(const struct user_editor *editor, u64 offset,
	u32 *codepoint, u64 *encoded_length);
u64 user_editor_line_start(const struct user_editor *editor, u64 offset);
u64 user_editor_line_end(const struct user_editor *editor, u64 offset);
u64 user_editor_line_count(const struct user_editor *editor);
u64 user_editor_line_number(const struct user_editor *editor, u64 offset);
u64 user_editor_line_start_number(const struct user_editor *editor, u64 line);
u64 user_editor_line_column(const struct user_editor *editor, u64 offset);
u64 user_editor_line_column_count(const struct user_editor *editor, u64 line);
void user_editor_move_left(struct user_editor *editor);
void user_editor_move_right(struct user_editor *editor);
void user_editor_move_vertical(struct user_editor *editor, int direction);
void user_editor_move_home(struct user_editor *editor);
void user_editor_move_end(struct user_editor *editor);

#endif
