#include <nimera/editor.h>

static void copy_bytes(char *destination, const char *source, u64 length)
{ for (u64 i = 0ULL; i < length; ++i) destination[i] = source[i]; }

static int decode_bytes(const char *text, u64 length, u64 offset,
	u32 *codepoint, u64 *encoded_length)
{
	unsigned char first;
	u32 value;
	unsigned int need;
	if (offset >= length) return 0;
	first = (unsigned char)text[offset];
	if (first < 0x80U) { *codepoint = first; *encoded_length = 1ULL; return 1; }
	if ((first & 0xe0U) == 0xc0U) { value = first & 0x1fU; need = 1U; }
	else if ((first & 0xf0U) == 0xe0U) { value = first & 0x0fU; need = 2U; }
	else if ((first & 0xf8U) == 0xf0U) { value = first & 7U; need = 3U; }
	else return 0;
	if (offset + (u64)need >= length) return 0;
	for (unsigned int i = 0U; i < need; ++i) {
		unsigned char continuation = (unsigned char)text[offset + 1ULL + i];
		if ((continuation & 0xc0U) != 0x80U) return 0;
		value = (value << 6) | (u32)(continuation & 0x3fU);
	}
	if ((need == 1U && value < 0x80U) || (need == 2U && value < 0x800U) ||
		(need == 3U && value < 0x10000U) || value > 0x10ffffU ||
		(value >= 0xd800U && value <= 0xdfffU)) return 0;
	*codepoint = value; *encoded_length = (u64)need + 1ULL; return 1;
}

static u64 previous_boundary_bytes(const char *text, u64 offset)
{
	if (offset == 0ULL) return 0ULL;
	--offset;
	while (offset != 0ULL && ((unsigned char)text[offset] & 0xc0U) == 0x80U) --offset;
	return offset;
}

void user_editor_init(struct user_editor *editor)
{
	editor->data = (char *)0; editor->length = 0ULL; editor->capacity = 0ULL;
	editor->cursor = 0ULL; editor->preferred_column = 0ULL;
	editor->preferred_valid = 0U; editor->dirty = 0U;
}

void user_editor_destroy(struct user_editor *editor)
{ if (editor->data != (char *)0) (void)nimera_free(editor->data); user_editor_init(editor); }

int user_editor_reserve(struct user_editor *editor, u64 required)
{
	u64 capacity = editor->capacity == 0ULL ? 64ULL : editor->capacity;
	char *data;
	if (required <= editor->capacity) return 0;
	while (capacity < required) { if (capacity > (~0ULL / 2ULL)) { capacity = required; break; } capacity *= 2ULL; }
	data = (char *)nimera_alloc(capacity + 1ULL);
	if (data == (char *)0) return -1;
	copy_bytes(data, editor->data, editor->length);
	data[editor->length] = '\0';
	if (editor->data != (char *)0) (void)nimera_free(editor->data);
	editor->data = data; editor->capacity = capacity; return 0;
}

int user_editor_set_text(struct user_editor *editor, const char *text, u64 length)
{
	u64 offset = 0ULL; u32 codepoint; u64 encoded;
	while (offset < length) { if (!decode_bytes(text, length, offset, &codepoint, &encoded)) return -1; offset += encoded; }
	if (user_editor_reserve(editor, length) != 0) return -1;
	copy_bytes(editor->data, text, length); editor->data[length] = '\0'; editor->length = length;
	editor->cursor = 0ULL; editor->preferred_valid = 0U; editor->dirty = 0U; return 0;
}

static int encode(u32 value, char output[4], u64 *length)
{
	if (value <= 0x7fU) { output[0] = (char)value; *length = 1ULL; }
	else if (value <= 0x7ffU) { output[0] = (char)(0xc0U | (value >> 6)); output[1] = (char)(0x80U | (value & 0x3fU)); *length = 2ULL; }
	else if (value >= 0xd800U && value <= 0xdfffU) return -1;
	else if (value <= 0xffffU) { output[0] = (char)(0xe0U | (value >> 12)); output[1] = (char)(0x80U | ((value >> 6) & 0x3fU)); output[2] = (char)(0x80U | (value & 0x3fU)); *length = 3ULL; }
	else if (value <= 0x10ffffU) { output[0] = (char)(0xf0U | (value >> 18)); output[1] = (char)(0x80U | ((value >> 12) & 0x3fU)); output[2] = (char)(0x80U | ((value >> 6) & 0x3fU)); output[3] = (char)(0x80U | (value & 0x3fU)); *length = 4ULL; }
	else return -1;
	return 0;
}

int user_editor_insert_codepoint(struct user_editor *editor, u32 codepoint)
{
	char encoded[4]; u64 length;
	if (encode(codepoint, encoded, &length) != 0 || user_editor_reserve(editor, editor->length + length) != 0) return -1;
	for (u64 i = editor->length; i > editor->cursor; --i) editor->data[i + length - 1ULL] = editor->data[i - 1ULL];
	for (u64 i = 0ULL; i < length; ++i) editor->data[editor->cursor + i] = encoded[i];
	editor->cursor += length; editor->length += length; editor->data[editor->length] = '\0';
	editor->preferred_valid = 0U; editor->dirty = 1U; return 0;
}

int user_editor_insert(struct user_editor *editor, char value)
{ return user_editor_insert_codepoint(editor, (u32)(unsigned char)value); }

void user_editor_backspace(struct user_editor *editor)
{
	u64 start, removed;
	if (editor->cursor == 0ULL) return;
	start = previous_boundary_bytes(editor->data, editor->cursor);
	removed = editor->cursor - start;
	for (u64 i = start; i < editor->length; ++i) editor->data[i] = editor->data[i + removed];
	editor->cursor = start; editor->length -= removed; editor->data[editor->length] = '\0'; editor->preferred_valid = 0U; editor->dirty = 1U;
}

void user_editor_delete(struct user_editor *editor)
{
	u64 end;
	if (editor->cursor == editor->length) return;
	end = user_editor_next_boundary(editor, editor->cursor);
	for (u64 i = editor->cursor; i < editor->length; ++i) editor->data[i] = editor->data[i + end - editor->cursor];
	editor->length -= end - editor->cursor; editor->data[editor->length] = '\0'; editor->preferred_valid = 0U; editor->dirty = 1U;
}

u64 user_editor_previous_boundary(const struct user_editor *editor, u64 offset)
{ return previous_boundary_bytes(editor->data, offset); }
u64 user_editor_next_boundary(const struct user_editor *editor, u64 offset)
{ u32 cp; u64 length; return decode_bytes(editor->data, editor->length, offset, &cp, &length) ? offset + length : editor->length; }
int user_editor_decode(const struct user_editor *editor, u64 offset, u32 *cp, u64 *length)
{ return decode_bytes(editor->data, editor->length, offset, cp, length); }

u64 user_editor_line_start(const struct user_editor *editor, u64 offset)
{ while (offset != 0ULL && editor->data[offset - 1ULL] != '\n') offset = previous_boundary_bytes(editor->data, offset); return offset; }
u64 user_editor_line_end(const struct user_editor *editor, u64 offset)
{ while (offset < editor->length && editor->data[offset] != '\n') offset = user_editor_next_boundary(editor, offset); return offset; }
u64 user_editor_line_count(const struct user_editor *editor)
{ u64 count = 1ULL; for (u64 i = 0ULL; i < editor->length; ++i) if (editor->data[i] == '\n') ++count; return count; }
u64 user_editor_line_number(const struct user_editor *editor, u64 offset)
{ u64 line = 0ULL; for (u64 i = 0ULL; i < offset; ++i) if (editor->data[i] == '\n') ++line; return line; }
u64 user_editor_line_start_number(const struct user_editor *editor, u64 line)
{ u64 current = 0ULL; for (u64 i = 0ULL; i < editor->length; ++i) { if (current == line) return i; if (editor->data[i] == '\n') ++current; } return editor->length; }
u64 user_editor_line_column(const struct user_editor *editor, u64 offset)
{ u64 start = user_editor_line_start(editor, offset), column = 0ULL; while (start < offset) { start = user_editor_next_boundary(editor, start); ++column; } return column; }
u64 user_editor_line_column_count(const struct user_editor *editor, u64 line)
{ u64 offset = user_editor_line_start_number(editor, line), end = user_editor_line_end(editor, offset), count = 0ULL; while (offset < end) { offset = user_editor_next_boundary(editor, offset); ++count; } return count; }

void user_editor_move_left(struct user_editor *editor)
{ editor->cursor = user_editor_previous_boundary(editor, editor->cursor); editor->preferred_valid = 0U; }
void user_editor_move_right(struct user_editor *editor)
{ editor->cursor = user_editor_next_boundary(editor, editor->cursor); editor->preferred_valid = 0U; }
static void set_column(struct user_editor *editor, u64 column)
{ u64 start = user_editor_line_start(editor, editor->cursor), end = user_editor_line_end(editor, start), current = 0ULL; while (start < end && current < column) { start = user_editor_next_boundary(editor, start); ++current; } editor->cursor = start; }
void user_editor_move_vertical(struct user_editor *editor, int direction)
{ u64 line = user_editor_line_number(editor, editor->cursor), target; if (editor->preferred_valid == 0U) { editor->preferred_column = user_editor_line_column(editor, editor->cursor); editor->preferred_valid = 1U; } if (direction < 0) { if (line == 0ULL) return; target = line - 1ULL; } else { if (line + 1ULL >= user_editor_line_count(editor)) return; target = line + 1ULL; } editor->cursor = user_editor_line_start_number(editor, target); set_column(editor, editor->preferred_column); }
void user_editor_move_home(struct user_editor *editor) { editor->cursor = user_editor_line_start(editor, editor->cursor); editor->preferred_valid = 0U; }
void user_editor_move_end(struct user_editor *editor) { editor->cursor = user_editor_line_end(editor, editor->cursor); editor->preferred_valid = 0U; }
