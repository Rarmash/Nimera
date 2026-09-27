#include <nimera/editor.h>
#include <nimera/gui.h>

static u64 string_length(const char *text)
{ u64 length = 0ULL; while (text[length] != '\0') ++length; return length; }

static int load_file(struct user_editor *editor, const char *path)
{
	char buffer[512]; u64 length = 0ULL; long long handle;
	handle = nimera_open(path, string_length(path), NIMERA_OPEN_READ);
	if (handle == NIMERA_NERR_NOT_FOUND) return 0;
	if (handle < 0LL) return (int)handle;
	for (;;) {
		long long result = nimera_read((u64)handle, buffer, sizeof(buffer));
		if (result < 0LL) { (void)nimera_close((u64)handle); return (int)result; }
		if (result == 0LL) break;
		if (user_editor_reserve(editor, length + (u64)result) != 0) { (void)nimera_close((u64)handle); return -1; }
		for (u64 i = 0ULL; i < (u64)result; ++i) editor->data[length + i] = buffer[i];
		length += (u64)result;
	}
	(void)nimera_close((u64)handle);
	if (user_editor_set_text(editor, editor->data, length) != 0) return -1;
	return 0;
}

static int save_file(const struct user_editor *editor, const char *path)
{
	long long handle = nimera_open(path, string_length(path), NIMERA_OPEN_WRITE | NIMERA_OPEN_CREATE | NIMERA_OPEN_TRUNCATE);
	u64 written = 0ULL;
	if (handle < 0LL) return (int)handle;
	while (written < editor->length) {
		long long result = nimera_write_file((u64)handle, editor->data + written, editor->length - written);
		if (result <= 0LL) { (void)nimera_close((u64)handle); return -1; }
		written += (u64)result;
	}
	if (nimera_close((u64)handle) != 0LL) return -1;
	return 0;
}

static void encode_codepoint(u32 cp, char output[5])
{
	if (cp <= 0x7fU) { output[0] = (char)cp; output[1] = '\0'; }
	else if (cp <= 0x7ffU) { output[0] = (char)(0xc0U | (cp >> 6)); output[1] = (char)(0x80U | (cp & 0x3fU)); output[2] = '\0'; }
	else if (cp <= 0xffffU) { output[0] = (char)(0xe0U | (cp >> 12)); output[1] = (char)(0x80U | ((cp >> 6) & 0x3fU)); output[2] = (char)(0x80U | (cp & 0x3fU)); output[3] = '\0'; }
	else { output[0] = (char)(0xf0U | (cp >> 18)); output[1] = (char)(0x80U | ((cp >> 12) & 0x3fU)); output[2] = (char)(0x80U | ((cp >> 6) & 0x3fU)); output[3] = (char)(0x80U | (cp & 0x3fU)); output[4] = '\0'; }
}

static void draw_line(struct nimera_gui_window *window, const struct user_editor *editor,
	u64 line, u64 first_column, long long x, long long y, u64 columns)
{
	u64 offset = user_editor_line_start_number(editor, line), end = user_editor_line_end(editor, offset), index = 0ULL;
	while (offset < end && index < first_column) { offset = user_editor_next_boundary(editor, offset); ++index; }
	for (u64 column = 0ULL; offset < end && column < columns; ++column) {
		u32 cp; u64 length; char encoded[5];
		if (!user_editor_decode(editor, offset, &cp, &length)) break;
		encode_codepoint(cp, encoded); nimera_gui_draw_text(&window->canvas, x + (long long)column * 10LL, y, encoded, NIMERA_GUI_COLOR_TEXT); offset += length;
	}
}

static void draw(struct nimera_gui_window *window, const struct user_editor *editor,
	const char *path, const char *status, u64 *first_line, u64 *first_column)
{
	u64 top = 14ULL, bottom = window->canvas.height > 38ULL ? window->canvas.height - 34ULL : 4ULL;
	u64 rows = bottom > top ? (bottom - top) / nimera_gui_font_line_height() : 1ULL;
	u64 columns = window->canvas.width > 24ULL ? (window->canvas.width - 24ULL) / 10ULL : 1ULL;
	u64 line = user_editor_line_number(editor, editor->cursor), column = user_editor_line_column(editor, editor->cursor);
	if (line < *first_line) *first_line = line;
	if (line >= *first_line + rows) *first_line = line - rows + 1ULL;
	if (column < *first_column) *first_column = column;
	if (column >= *first_column + columns) *first_column = column - columns + 1ULL;
	nimera_gui_clear(&window->canvas, NIMERA_GUI_COLOR_BACKGROUND);
	nimera_gui_draw_text(&window->canvas, 12, 8, "NimEdit 0.3 - ", NIMERA_GUI_COLOR_ACCENT);
	nimera_gui_draw_text(&window->canvas, 142, 8, path, NIMERA_GUI_COLOR_TEXT);
	if (editor->dirty != 0U) nimera_gui_draw_text(&window->canvas, 12, 26, "* modified", NIMERA_GUI_COLOR_ACCENT);
	for (u64 row = 0ULL; row < rows; ++row) if (*first_line + row < user_editor_line_count(editor))
		draw_line(window, editor, *first_line + row, *first_column, 12, (long long)(top + row * nimera_gui_font_line_height()), columns);
	if (line >= *first_line && line < *first_line + rows && column >= *first_column && column < *first_column + columns)
		nimera_gui_fill_rect(&window->canvas, (struct nimera_gui_rect){12LL + (long long)(column - *first_column) * 10LL,
		(long long)(top + (line - *first_line) * nimera_gui_font_line_height()), 2ULL, 16ULL}, NIMERA_GUI_COLOR_ACCENT);
	nimera_gui_fill_rect(&window->canvas, (struct nimera_gui_rect){0, (long long)bottom, window->canvas.width, 34ULL}, NIMERA_GUI_COLOR_PANEL);
	nimera_gui_draw_text(&window->canvas, 12, (long long)bottom + 7LL, status != (const char *)0 ? status : "Ctrl-S Save | Ctrl-Q Quit", NIMERA_GUI_COLOR_TEXT);
	nimera_gui_draw_text(&window->canvas, 520, (long long)bottom + 7LL, "Ln ", NIMERA_GUI_COLOR_ACCENT);
}

int main(int argc, char **argv)
{
	struct nimera_gui_window window; struct nimera_window_event event; struct user_editor editor;
	u64 first_line = 0ULL, first_column = 0ULL; const char *status = (const char *)0; int result;
	if (argc != 2) return 2;
	user_editor_init(&editor); result = load_file(&editor, argv[1]);
	if (result != 0) { user_editor_destroy(&editor); return 1; }
	if (nimera_gui_window_create(&window, 800U, 560U, "NimEdit", 7ULL, NIMERA_WINDOW_CLOSABLE | NIMERA_WINDOW_RESIZABLE) != 0) { user_editor_destroy(&editor); return 1; }
	draw(&window, &editor, argv[1], status, &first_line, &first_column); (void)nimera_gui_window_present(&window, (const struct nimera_gui_damage *)0);
	for (;;) {
		if (nimera_gui_window_next_event(&window, &event) != 0) break;
		if (event.type == NIMERA_WINDOW_EVENT_CLOSE_REQUEST) {
			if (editor.dirty != 0U) { status = "Unsaved changes - press Ctrl-S before closing"; draw(&window, &editor, argv[1], status, &first_line, &first_column); (void)nimera_gui_window_present(&window, (const struct nimera_gui_damage *)0); continue; }
			break;
		}
		if (event.type == NIMERA_WINDOW_EVENT_RESIZED) { draw(&window, &editor, argv[1], status, &first_line, &first_column); (void)nimera_gui_window_present(&window, (const struct nimera_gui_damage *)0); continue; }
		if (event.type != NIMERA_WINDOW_KEY) continue;
		if ((event.modifiers & NIMERA_KEY_MOD_CTRL) != 0U && event.ch == 's') { result = save_file(&editor, argv[1]); status = result == 0 ? "Saved" : "Save failed"; if (result == 0) editor.dirty = 0U; }
		else if ((event.modifiers & NIMERA_KEY_MOD_CTRL) != 0U && event.ch == 'q') { if (editor.dirty == 0U) break; status = "Unsaved changes - press Ctrl-S before closing"; }
		else { status = (const char *)0; switch (event.key_code) {
			case NIMERA_KEY_LEFT: user_editor_move_left(&editor); break; case NIMERA_KEY_RIGHT: user_editor_move_right(&editor); break;
			case NIMERA_KEY_UP: user_editor_move_vertical(&editor, -1); break; case NIMERA_KEY_DOWN: user_editor_move_vertical(&editor, 1); break;
			case NIMERA_KEY_HOME: user_editor_move_home(&editor); break; case NIMERA_KEY_END: user_editor_move_end(&editor); break;
			case NIMERA_KEY_BACKSPACE: user_editor_backspace(&editor); break; case NIMERA_KEY_DELETE: user_editor_delete(&editor); break;
			case NIMERA_KEY_ENTER: (void)user_editor_insert_codepoint(&editor, '\n'); break;
			case NIMERA_KEY_CHAR: if (event.modifiers == 0U && event.ch >= 32U) (void)user_editor_insert_codepoint(&editor, event.ch); break;
			default: break;
		} }
		draw(&window, &editor, argv[1], status, &first_line, &first_column); (void)nimera_gui_window_present(&window, (const struct nimera_gui_damage *)0);
	}
	nimera_gui_window_destroy(&window); user_editor_destroy(&editor); return 0;
}
