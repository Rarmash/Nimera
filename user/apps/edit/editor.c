#include "editor.h"

static void copy_bytes(char *destination, const char *source,
	unsigned long long length)
{
	for (unsigned long long index = 0ULL; index < length; ++index)
		destination[index] = source[index];
}

void user_editor_init(struct user_editor *editor)
{
	editor->data = (char *)0;
	editor->length = 0ULL;
	editor->capacity = 0ULL;
	editor->cursor = 0ULL;
	editor->preferred_column = 0ULL;
	editor->preferred_valid = 0U;
	editor->dirty = 0U;
}

void user_editor_destroy(struct user_editor *editor)
{
	if (editor->data != (char *)0) (void)nimera_free(editor->data);
	user_editor_init(editor);
}

int user_editor_reserve(struct user_editor *editor, unsigned long long required)
{
	unsigned long long capacity = editor->capacity == 0ULL ? 64ULL : editor->capacity;
	char *data;
	if (required <= editor->capacity) return 0;
	while (capacity < required) {
		if (capacity > (~0ULL / 2ULL)) { capacity = required; break; }
		capacity *= 2ULL;
	}
	data = (char *)nimera_alloc(capacity);
	if (data == (char *)0) return -1;
	copy_bytes(data, editor->data, editor->length);
	if (editor->data != (char *)0) (void)nimera_free(editor->data);
	editor->data = data;
	editor->capacity = capacity;
	return 0;
}

int user_editor_insert(struct user_editor *editor, char value)
{
	if (user_editor_reserve(editor, editor->length + 1ULL) != 0) return -1;
	for (unsigned long long index = editor->length; index > editor->cursor; --index)
		editor->data[index] = editor->data[index - 1ULL];
	editor->data[editor->cursor++] = value;
	++editor->length;
	editor->preferred_valid = 0U;
	editor->dirty = 1U;
	return 0;
}

void user_editor_backspace(struct user_editor *editor)
{
	if (editor->cursor == 0ULL) return;
	--editor->cursor;
	for (unsigned long long index = editor->cursor; index < editor->length; ++index)
		editor->data[index] = editor->data[index + 1ULL];
	--editor->length;
	editor->preferred_valid = 0U;
	editor->dirty = 1U;
}

void user_editor_delete(struct user_editor *editor)
{
	if (editor->cursor == editor->length) return;
	for (unsigned long long index = editor->cursor; index < editor->length; ++index)
		editor->data[index] = editor->data[index + 1ULL];
	--editor->length;
	editor->preferred_valid = 0U;
	editor->dirty = 1U;
}

unsigned long long user_editor_line_start(const struct user_editor *editor,
	unsigned long long offset)
{
	while (offset != 0ULL && editor->data[offset - 1ULL] != '\n') --offset;
	return offset;
}

unsigned long long user_editor_line_end(const struct user_editor *editor,
	unsigned long long offset)
{
	while (offset < editor->length && editor->data[offset] != '\n') ++offset;
	return offset;
}

unsigned long long user_editor_line_count(const struct user_editor *editor)
{
	unsigned long long count = 1ULL;
	for (unsigned long long index = 0ULL; index < editor->length; ++index)
		if (editor->data[index] == '\n') ++count;
	return count;
}

unsigned long long user_editor_line_number(const struct user_editor *editor,
	unsigned long long offset)
{
	unsigned long long line = 0ULL;
	for (unsigned long long index = 0ULL; index < offset; ++index)
		if (editor->data[index] == '\n') ++line;
	return line;
}

unsigned long long user_editor_line_start_number(const struct user_editor *editor,
	unsigned long long line)
{
	unsigned long long current = 0ULL;
	for (unsigned long long index = 0ULL; index < editor->length; ++index) {
		if (current == line) return index;
		if (editor->data[index] == '\n') ++current;
	}
	return editor->length;
}

static void set_column(struct user_editor *editor, unsigned long long column)
{
	unsigned long long start = user_editor_line_start(editor, editor->cursor);
	unsigned long long end = user_editor_line_end(editor, start);
	editor->cursor = start + (column > end - start ? end - start : column);
}

void user_editor_move_vertical(struct user_editor *editor, int direction)
{
	unsigned long long line = user_editor_line_number(editor, editor->cursor);
	unsigned long long start = user_editor_line_start(editor, editor->cursor);
	unsigned long long column = editor->cursor - start;
	unsigned long long target;
	if (editor->preferred_valid == 0U) {
		editor->preferred_column = column;
		editor->preferred_valid = 1U;
	}
	if (direction < 0) {
		if (line == 0ULL) return;
		target = line - 1ULL;
	} else {
		if (line + 1ULL >= user_editor_line_count(editor)) return;
		target = line + 1ULL;
	}
	editor->cursor = user_editor_line_start_number(editor, target);
	set_column(editor, editor->preferred_column);
}

void user_editor_move_home(struct user_editor *editor)
{
	editor->cursor = user_editor_line_start(editor, editor->cursor);
	editor->preferred_valid = 0U;
}

void user_editor_move_end(struct user_editor *editor)
{
	editor->cursor = user_editor_line_end(editor, editor->cursor);
	editor->preferred_valid = 0U;
}
