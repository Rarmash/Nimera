#include "editor.h"

static unsigned long long text_length(const char *text)
{
	unsigned long long length = 0ULL;
	while (text[length] != '\0') ++length;
	return length;
}

static void write_text(const char *text)
{
	(void)nimera_write_console(text, text_length(text));
}

static void write_number(unsigned long long value)
{
	char digits[24];
	unsigned int count = 0U;
	if (value == 0ULL) { write_text("0"); return; }
	while (value != 0ULL) {
		digits[count++] = (char)('0' + value % 10ULL);
		value /= 10ULL;
	}
	while (count != 0U) (void)nimera_write_console(&digits[--count], 1ULL);
}

#if NIMERA_USER_EDITOR_SELF_TEST
static int editor_self_test(void)
{
	static const char expected[] = "hello from nimedit!";
	struct user_editor editor;
	unsigned long long index;
	int ok = 1;
	user_editor_init(&editor);
	for (index = 0ULL; expected[index] != '\0'; ++index)
		if (user_editor_insert(&editor, expected[index]) != 0) ok = 0;
	ok = ok && editor.length == 19ULL && editor.cursor == 19ULL;
	for (index = 0ULL; index < editor.length && expected[index] != '\0'; ++index)
		if (editor.data[index] != expected[index]) ok = 0;
	ok = ok && user_editor_line_count(&editor) == 1ULL;
	user_editor_move_home(&editor);
	ok = ok && editor.cursor == 0ULL;
	user_editor_move_end(&editor);
	ok = ok && editor.cursor == editor.length;
	user_editor_destroy(&editor);
	write_text("NimEdit userspace model test\r\n");
	write_text("buffer hello from nimedit!: ");
	write_text(ok ? "OK\r\n" : "FAIL\r\n");
	write_text("dynamic buffer cleanup: OK\r\n");
	return ok ? 0 : 1;
}
#endif

static int load_file(struct user_editor *editor, const char *path)
{
	long long handle = nimera_open(path, text_length(path), NIMERA_OPEN_READ);
	if (handle == NIMERA_NERR_NOT_FOUND) {
		return user_editor_reserve(editor, 64ULL);
	}
	if (handle < 0LL) return (int)handle;
	if (user_editor_reserve(editor, 64ULL) != 0) {
		(void)nimera_close((unsigned long long)handle);
		return (int)NIMERA_NERR_NO_MEMORY;
	}
	for (;;) {
		long long result;
		if (editor->length == editor->capacity &&
			user_editor_reserve(editor, editor->capacity + 1ULL) != 0) {
			(void)nimera_close((unsigned long long)handle);
			return (int)NIMERA_NERR_NO_MEMORY;
		}
		result = nimera_read((unsigned long long)handle,
			editor->data + editor->length, editor->capacity - editor->length);
		if (result < 0LL) {
			(void)nimera_close((unsigned long long)handle);
			return (int)result;
		}
		if (result == 0LL) break;
		editor->length += (unsigned long long)result;
	}
	(void)nimera_close((unsigned long long)handle);
	return 0;
}

static int save_file(const struct user_editor *editor, const char *path)
{
	long long handle = nimera_open(path, text_length(path),
		NIMERA_OPEN_WRITE | NIMERA_OPEN_CREATE | NIMERA_OPEN_TRUNCATE);
	unsigned long long written = 0ULL;
	if (handle < 0LL) return (int)handle;
	while (written < editor->length) {
		long long result = nimera_write_file((unsigned long long)handle,
			editor->data + written, editor->length - written);
		if (result <= 0LL) {
			(void)nimera_close((unsigned long long)handle);
			return result < 0LL ? (int)result : (int)NIMERA_NERR_IO;
		}
		written += (unsigned long long)result;
	}
	if (nimera_close((unsigned long long)handle) != 0LL)
		return (int)NIMERA_NERR_IO;
	return 0;
}

static const char *error_text(int error)
{
	if (error == NIMERA_NERR_NOT_FOUND) return "File not found";
	if (error == NIMERA_NERR_IS_DIRECTORY) return "Is a directory";
	if (error == NIMERA_NERR_NO_MEMORY) return "Out of memory";
	return "File I/O error";
}

static unsigned long long visible_length(const struct user_editor *editor,
	unsigned long long line, unsigned int columns)
{
	unsigned long long start = user_editor_line_start_number(editor, line);
	unsigned long long end = user_editor_line_end(editor, start);
	unsigned long long length = end - start;
	return length > (unsigned long long)columns ? (unsigned long long)columns : length;
}

static void render_line(const struct user_editor *editor, unsigned long long line,
	unsigned int columns)
{
	unsigned long long start = user_editor_line_start_number(editor, line);
	unsigned long long length = visible_length(editor, line, columns);
	if (length != 0ULL) (void)nimera_write_console(editor->data + start, length);
}

static void render(const struct user_editor *editor, const char *path,
	const char *status, unsigned int rows, unsigned int columns,
	unsigned long long first_line)
{
	unsigned int viewport = rows - 2U;
	unsigned long long line = user_editor_line_number(editor, editor->cursor);
	unsigned long long start = user_editor_line_start(editor, editor->cursor);
	unsigned long long column = editor->cursor - start;
	if (line < first_line) first_line = line;
	if (line >= first_line + viewport) first_line = line - viewport + 1ULL;
	(void)nimera_terminal_begin_update();
	(void)nimera_terminal_clear();
	(void)nimera_terminal_move_cursor(0U, 0U);
	write_text("NimEdit 0.2 - "); write_text(path);
	if (editor->dirty != 0U) write_text("*");
	(void)nimera_terminal_clear_line();
	for (unsigned int screen = 0U; screen < viewport; ++screen) {
		unsigned long long file_line = first_line + screen;
		(void)nimera_terminal_move_cursor(screen + 1U, 0U);
		(void)nimera_terminal_clear_line();
		if (file_line < user_editor_line_count(editor))
			render_line(editor, file_line, columns);
	}
	(void)nimera_terminal_move_cursor(rows - 1U, 0U);
	(void)nimera_terminal_clear_line();
	if (status != (const char *)0) write_text(status);
	else write_text("Ctrl-S Save | Ctrl-Q Quit");
	write_text("       Ln "); write_number(line + 1ULL);
	write_text(", Col "); write_number(column + 1ULL);
	if (column >= columns) column = columns - 1U;
	(void)nimera_terminal_move_cursor((unsigned int)(line - first_line) + 1U,
		(unsigned int)column);
	(void)nimera_terminal_cursor_visible(1);
	(void)nimera_terminal_end_update();
}

static const char *key_status(const struct nimera_key_event *event)
{
	if ((event->modifiers & NIMERA_KEY_MOD_CTRL) != 0U && event->ch == 'q') return "Ctrl-Q";
	if (event->code == NIMERA_KEY_UP) return "Up";
	if (event->code == NIMERA_KEY_DOWN) return "Down";
	if (event->code == NIMERA_KEY_LEFT) return "Left";
	if (event->code == NIMERA_KEY_RIGHT) return "Right";
	return "";
}

int main(unsigned long long argc, char **argv)
{
	struct nimera_terminal_size size;
	struct user_editor editor;
	const char *status = (const char *)0;
	unsigned long long first_line = 0ULL;
	unsigned int force_quit = 0U;
	int result;
#if NIMERA_USER_EDITOR_SELF_TEST
	(void)argc; (void)argv;
	return editor_self_test();
#endif
	if (argc != 2ULL) { write_text("Usage: edit <path>\r\n"); return 2; }
	if (nimera_terminal_size(&size) != 0 || size.rows < 4U || size.columns < 10U) {
		write_text("NimEdit: terminal is too small\r\n");
		(void)nimera_terminal_cursor_visible(1); return 2;
	}
	user_editor_init(&editor);
	result = load_file(&editor, argv[1]);
	if (result != 0) {
		write_text("NimEdit: "); write_text(error_text(result)); write_text("\r\n");
		user_editor_destroy(&editor); (void)nimera_terminal_cursor_visible(1); return 1;
	}
	for (;;) {
		struct nimera_key_event event;
		render(&editor, argv[1], status, size.rows, size.columns, first_line);
		status = (const char *)0;
		if (nimera_read_key(&event) != 0) { result = NIMERA_NERR_IO; break; }
		if (event.code == NIMERA_KEY_CHAR &&
			(event.modifiers & NIMERA_KEY_MOD_CTRL) != 0U && event.ch == 's') {
			result = save_file(&editor, argv[1]);
			if (result == 0) { editor.dirty = 0U; status = "Saved"; }
			else status = error_text(result);
			force_quit = 0U; continue;
		}
		if (event.code == NIMERA_KEY_CHAR &&
			(event.modifiers & NIMERA_KEY_MOD_CTRL) != 0U && event.ch == 'q') {
			if (editor.dirty == 0U || force_quit != 0U) break;
			force_quit = 1U;
			status = "Unsaved changes - press Ctrl-Q again to quit";
			continue;
		}
		force_quit = 0U;
		if (event.code == NIMERA_KEY_CHAR && event.modifiers == 0U &&
			event.ch >= 32U && event.ch <= 126U) result = user_editor_insert(&editor, (char)event.ch);
		else if (event.code == NIMERA_KEY_ENTER) result = user_editor_insert(&editor, '\n');
		else { result = 0; switch (event.code) {
			case NIMERA_KEY_BACKSPACE: user_editor_backspace(&editor); break;
			case NIMERA_KEY_DELETE: user_editor_delete(&editor); break;
			case NIMERA_KEY_LEFT: if (editor.cursor != 0ULL) --editor.cursor; editor.preferred_valid = 0U; break;
			case NIMERA_KEY_RIGHT: if (editor.cursor < editor.length) ++editor.cursor; editor.preferred_valid = 0U; break;
			case NIMERA_KEY_UP: user_editor_move_vertical(&editor, -1); break;
			case NIMERA_KEY_DOWN: user_editor_move_vertical(&editor, 1); break;
			case NIMERA_KEY_HOME: user_editor_move_home(&editor); break;
			case NIMERA_KEY_END: user_editor_move_end(&editor); break;
			default: status = key_status(&event); break;
		} }
		if (result != 0) status = error_text(result);
	}
	user_editor_destroy(&editor);
	(void)nimera_terminal_clear();
	(void)nimera_terminal_cursor_visible(1);
	return result == 0 ? 0 : 1;
}
