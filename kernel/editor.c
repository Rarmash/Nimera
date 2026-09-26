#include <nimera/console.h>
#include <nimera/editor.h>
#include <nimera/format.h>
#include <nimera/heap.h>
#include <nimera/panic.h>
#include <nimera/terminal.h>
#include <nimera/vfs.h>

struct editor_buffer {
	char *data;
	u64 length;
	u64 capacity;
	u64 cursor;
	u64 preferred_column;
	unsigned int preferred_valid;
	unsigned int dirty;
};

static u64 editor_first_visible_line;

static unsigned int editor_string_length(const char *text)
{
	unsigned int length = 0U;

	while (text[length] != '\0') {
		++length;
	}
	return length;
}

static int editor_string_equals(const char *left, const char *right)
{
	unsigned int index = 0U;

	while (left[index] != '\0' && right[index] != '\0') {
		if (left[index] != right[index]) {
			return 0;
		}
		++index;
	}
	return left[index] == '\0' && right[index] == '\0';
}

static void editor_buffer_init(struct editor_buffer *buffer)
{
	buffer->data = (char *)0;
	buffer->length = 0ULL;
	buffer->capacity = 0ULL;
	buffer->cursor = 0ULL;
	buffer->preferred_column = 0ULL;
	buffer->preferred_valid = 0U;
	buffer->dirty = 0U;
}

static void editor_buffer_destroy(struct editor_buffer *buffer)
{
	if (buffer->data != (char *)0) {
		kfree(buffer->data);
	}
	editor_buffer_init(buffer);
}

static enum vfs_error editor_reserve(struct editor_buffer *buffer, u64 required)
{
	u64 capacity = buffer->capacity;
	char *data;

	if (required <= capacity) {
		return VFS_OK;
	}
	capacity = capacity == 0ULL ? 64ULL : capacity;
	while (capacity < required) {
		if (capacity > (~0ULL / 2ULL)) {
			capacity = required;
			break;
		}
		capacity *= 2ULL;
	}
	data = (char *)kmalloc(capacity);
	if (data == (char *)0) {
		return VFS_NO_MEMORY;
	}
	for (u64 index = 0ULL; index < buffer->length; ++index) {
		data[index] = buffer->data[index];
	}
	if (buffer->data != (char *)0) {
		kfree(buffer->data);
	}
	buffer->data = data;
	buffer->capacity = capacity;
	return VFS_OK;
}

static enum vfs_error editor_insert(struct editor_buffer *buffer, char value)
{
	enum vfs_error error = editor_reserve(buffer, buffer->length + 1ULL);

	if (error != VFS_OK) {
		return error;
	}
	for (u64 index = buffer->length; index > buffer->cursor; --index) {
		buffer->data[index] = buffer->data[index - 1ULL];
	}
	buffer->data[buffer->cursor++] = value;
	++buffer->length;
	buffer->preferred_valid = 0U;
	buffer->dirty = 1U;
	return VFS_OK;
}

static void editor_backspace(struct editor_buffer *buffer)
{
	if (buffer->cursor == 0ULL) {
		return;
	}
	--buffer->cursor;
	for (u64 index = buffer->cursor; index < buffer->length; ++index) {
		buffer->data[index] = buffer->data[index + 1ULL];
	}
	--buffer->length;
	buffer->preferred_valid = 0U;
	buffer->dirty = 1U;
}

static void editor_delete(struct editor_buffer *buffer)
{
	if (buffer->cursor == buffer->length) {
		return;
	}
	for (u64 index = buffer->cursor; index < buffer->length; ++index) {
		buffer->data[index] = buffer->data[index + 1ULL];
	}
	--buffer->length;
	buffer->preferred_valid = 0U;
	buffer->dirty = 1U;
}

static u64 editor_line_start(const struct editor_buffer *buffer, u64 offset)
{
	while (offset != 0ULL && buffer->data[offset - 1ULL] != '\n') {
		--offset;
	}
	return offset;
}

static u64 editor_line_end(const struct editor_buffer *buffer, u64 offset)
{
	while (offset < buffer->length && buffer->data[offset] != '\n') {
		++offset;
	}
	return offset;
}

static u64 editor_line_count(const struct editor_buffer *buffer)
{
	u64 count = 1ULL;

	for (u64 index = 0ULL; index < buffer->length; ++index) {
		if (buffer->data[index] == '\n') {
			++count;
		}
	}
	return count;
}

static u64 editor_line_number(const struct editor_buffer *buffer, u64 offset)
{
	u64 line = 0ULL;

	for (u64 index = 0ULL; index < offset; ++index) {
		if (buffer->data[index] == '\n') {
			++line;
		}
	}
	return line;
}

static u64 editor_line_start_number(const struct editor_buffer *buffer,
					    u64 line)
{
	u64 current = 0ULL;

	for (u64 index = 0ULL; index < buffer->length; ++index) {
		if (current == line) {
			return index;
		}
		if (buffer->data[index] == '\n') {
			++current;
		}
	}
	return buffer->length;
}

static void editor_set_cursor_column(struct editor_buffer *buffer, u64 column)
{
	u64 start = editor_line_start(buffer, buffer->cursor);
	u64 end = editor_line_end(buffer, start);

	buffer->cursor = start + (column > end - start ? end - start : column);
}

static void editor_move_vertical(struct editor_buffer *buffer, int direction)
{
	u64 current_line = editor_line_number(buffer, buffer->cursor);
	u64 current_start = editor_line_start(buffer, buffer->cursor);
	u64 current_column = buffer->cursor - current_start;
	u64 target_line;

	if (buffer->preferred_valid == 0U) {
		buffer->preferred_column = current_column;
		buffer->preferred_valid = 1U;
	}
	if (direction < 0) {
		if (current_line == 0ULL) {
			return;
		}
		target_line = current_line - 1ULL;
	} else {
		if (current_line + 1ULL >= editor_line_count(buffer)) {
			return;
		}
		target_line = current_line + 1ULL;
	}
	buffer->cursor = editor_line_start_number(buffer, target_line);
	editor_set_cursor_column(buffer, buffer->preferred_column);
}

static void editor_move_home(struct editor_buffer *buffer)
{
	buffer->cursor = editor_line_start(buffer, buffer->cursor);
	buffer->preferred_valid = 0U;
}

static void editor_move_end(struct editor_buffer *buffer)
{
	buffer->cursor = editor_line_end(buffer, buffer->cursor);
	buffer->preferred_valid = 0U;
}

static enum vfs_error editor_load(struct editor_buffer *buffer,
					  struct vfs_node *cwd, const char *path,
					  int *exists)
{
	struct vfs_node *node;
	char probe;
	u64 size;
	enum vfs_error error = vfs_resolve(cwd, path, &node);

	if (error == VFS_NOT_FOUND) {
		*exists = 0;
		return editor_reserve(buffer, 64ULL);
	}
	if (error != VFS_OK) {
		return error;
	}
	if (vfs_node_type(node) == VFS_NODE_DIRECTORY) {
		return VFS_IS_DIRECTORY;
	}
	*exists = 1;
	size = 0ULL;
	error = vfs_read(node, &probe, 0ULL, &size);
	if (error != VFS_OK && error != VFS_TOO_LARGE) {
		return error;
	}
	error = editor_reserve(buffer, size + 64ULL);
	if (error != VFS_OK) {
		return error;
	}
	if (size != 0ULL) {
		error = vfs_read(node, buffer->data, buffer->capacity, &buffer->length);
		if (error != VFS_OK) {
			return error;
		}
	}
	return VFS_OK;
}

static enum vfs_error editor_save(struct editor_buffer *buffer,
					  struct vfs_node *cwd, const char *path)
{
	enum vfs_error error = vfs_write(cwd, path, buffer->data, buffer->length,
						(struct vfs_node **)0);

	if (error == VFS_OK) {
		buffer->dirty = 0U;
	}
	return error;
}

static u64 editor_visible_line_length(const struct editor_buffer *buffer,
					      u64 line, unsigned int columns)
{
	u64 start = editor_line_start_number(buffer, line);
	u64 end = editor_line_end(buffer, start);
	u64 visible = end - start;

	if (visible > (u64)columns) {
		visible = (u64)columns;
	}
	return visible;
}

static char editor_visible_line_character(const struct editor_buffer *buffer,
						 u64 line, u64 column)
{
	u64 start = editor_line_start_number(buffer, line);

	return buffer->data[start + column];
}

static void editor_render_line(const struct editor_buffer *buffer, u64 line,
				       unsigned int columns)
{
	u64 visible = editor_visible_line_length(buffer, line, columns);

	for (u64 index = 0ULL; index < visible; ++index) {
		console_putc(editor_visible_line_character(buffer, line, index));
	}
}

static void editor_render(const struct editor_buffer *buffer, const char *path,
				  const char *status)
{
	unsigned int rows = terminal_rows();
	unsigned int columns = terminal_columns();
	unsigned int viewport = rows > 2U ? rows - 2U : 1U;
	u64 line = editor_line_number(buffer, buffer->cursor);
	u64 cursor_start = editor_line_start(buffer, buffer->cursor);
	u64 cursor_column = buffer->cursor - cursor_start;
	u64 first_visible_line = editor_first_visible_line;

	if (line < first_visible_line) {
		first_visible_line = line;
	}
	if (line >= first_visible_line + (u64)viewport) {
		first_visible_line = line - (u64)viewport + 1ULL;
	}
	editor_first_visible_line = first_visible_line;
	terminal_clear();
	terminal_move_cursor(0U, 0U);
	console_write("NimEdit 0.1 - ");
	console_write(path);
	if (buffer->dirty != 0U) {
		console_putc('*');
	}
	terminal_clear_line();
	for (unsigned int screen_line = 0U; screen_line < viewport;
		 ++screen_line) {
		u64 file_line = first_visible_line + (u64)screen_line;

		terminal_move_cursor(screen_line + 1U, 0U);
		terminal_clear_line();
		if (file_line < editor_line_count(buffer)) {
			editor_render_line(buffer, file_line, columns);
		}
	}
	terminal_move_cursor(rows - 1U, 0U);
	terminal_clear_line();
	if (status != (const char *)0) {
		console_write(status);
		if (editor_string_equals(status, "Saved")) {
			console_putc(' ');
			console_write(path);
		}
	} else {
		console_write("Ctrl-S Save | Ctrl-Q Quit");
	}
	console_write("   Ln ");
	format_u64_decimal(line + 1ULL);
	console_write(", Col ");
	format_u64_decimal(cursor_column + 1ULL);
	if (cursor_column >= (u64)columns) {
		cursor_column = (u64)columns - 1ULL;
	}
	terminal_move_cursor((unsigned int)(line - first_visible_line) + 1U,
				     (unsigned int)cursor_column);
	terminal_show_cursor();
}

static void editor_copy_text(struct editor_buffer *buffer, const char *text)
{
	u64 length = (u64)editor_string_length(text);

	if (editor_reserve(buffer, length + 1ULL) != VFS_OK) {
		panic("NimEdit test allocation failed");
	}
	for (u64 index = 0ULL; index < length; ++index) {
		buffer->data[index] = text[index];
	}
	buffer->length = length;
	buffer->cursor = length;
}

static void editor_test_require(int condition, const char *message)
{
	if (condition == 0) {
		panic(message);
	}
}

void editor_self_test(struct vfs_node *cwd)
{
	struct editor_buffer buffer;
	struct editor_buffer loaded;
	char content[64];
	u64 size;
	u64 heap_before = heap_allocated_bytes();
	static const char initial[] = "old";
	static const char replacement[] = "new text";

	console_write("NimEdit test\r\nHeap allocated before editor: ");
	format_u64_decimal(heap_before);
	console_write(" bytes\r\n");
	editor_buffer_init(&buffer);
	editor_test_require(editor_reserve(&buffer, 1ULL) == VFS_OK,
				    "NimEdit empty buffer test failed");
	for (unsigned int index = 0U; index < 300U; ++index) {
		editor_test_require(editor_insert(&buffer, (char)('a' + index % 26U)) ==
					VFS_OK, "NimEdit insert test failed");
	}
	editor_test_require(buffer.length == 300ULL && buffer.capacity >= 300ULL,
				    "NimEdit capacity test failed");
	console_write("Buffer insert/delete: OK\r\n");
	editor_buffer_destroy(&buffer);
	editor_buffer_init(&buffer);
	editor_copy_text(&buffer, "abcdef\nxy\n123456");
	buffer.cursor = 5ULL;
	editor_move_vertical(&buffer, 1);
	editor_test_require(buffer.cursor == 9ULL, "NimEdit preferred column failed");
	editor_move_vertical(&buffer, 1);
	editor_test_require(buffer.cursor == 15ULL, "NimEdit down navigation failed");
	editor_move_home(&buffer);
	editor_delete(&buffer);
	editor_backspace(&buffer);
	editor_test_require(buffer.length == 14ULL, "NimEdit edit operation failed");
	console_write("Line navigation: OK\r\nPreferred column: OK\r\nCapacity growth: OK\r\n");
	editor_buffer_destroy(&buffer);
	editor_buffer_init(&buffer);
	editor_copy_text(&buffer, "hello from nimedit!");
	editor_test_require(editor_visible_line_length(&buffer, 0ULL,
						      TERMINAL_DEFAULT_COLUMNS) == 19ULL,
					    "NimEdit renderer length failed");
	{
		static const char expected[] = "hello from nimedit!";

		for (u64 index = 0ULL; index < 19ULL; ++index) {
			editor_test_require(editor_visible_line_character(&buffer, 0ULL,
								 index) == expected[index],
						    "NimEdit renderer character failed");
		}
	}
	console_write("Renderer visible text: OK (hello from nimedit!)\r\n");
	editor_buffer_destroy(&buffer);
	if (vfs_write(cwd, "/tmp/nimedit-test.txt", initial,
			      sizeof(initial) - 1ULL, (struct vfs_node **)0) != VFS_OK) {
		panic("NimEdit test file creation failed");
	}
	editor_buffer_init(&loaded);
	{
		int exists;
		enum vfs_error error = editor_load(&loaded, cwd,
						   "/tmp/nimedit-test.txt", &exists);
		editor_test_require(error == VFS_OK && exists != 0 && loaded.length == 3ULL,
					    "NimEdit load test failed");
	}
	console_write("Load existing file: OK\r\n");
	editor_copy_text(&loaded, replacement);
	loaded.dirty = 1U;
	editor_test_require(editor_save(&loaded, cwd, "/tmp/nimedit-test.txt") ==
				    VFS_OK && loaded.dirty == 0U,
				    "NimEdit save or dirty test failed");
	editor_buffer_destroy(&loaded);
	editor_buffer_init(&loaded);
	{
		int exists;
		editor_test_require(editor_load(&loaded, cwd,
						"/tmp/nimedit-test.txt", &exists) == VFS_OK &&
					    exists != 0 && loaded.length == sizeof(replacement) - 1ULL,
					    "NimEdit reopen test failed");
	}
	for (u64 index = 0ULL; index < loaded.length; ++index) {
		content[index] = loaded.data[index];
	}
	size = loaded.length;
	editor_test_require(size == 8ULL && content[0] == 'n' && content[7] == 't',
				    "NimEdit read-back test failed");
	editor_buffer_destroy(&loaded);
	if (vfs_remove(cwd, "/tmp/nimedit-test.txt") != VFS_OK) {
		panic("NimEdit test cleanup failed");
	}
	console_write("Save/read-back: OK\r\nDirty tracking: OK\r\nHeap allocated after editor: ");
	format_u64_decimal(heap_allocated_bytes());
	console_write(" bytes\r\nNimEdit test complete.\r\n");
}

int editor_run(struct vfs_node *cwd, const char *path)
{
	struct editor_buffer buffer;
	struct vfs_node *node;
	char local_path[VFS_PATH_MAX];
	unsigned int path_length;
	const char *status = (const char *)0;
	unsigned int force_quit = 0U;
	int exists;
	enum vfs_error error;

	if (path == (const char *)0) {
		console_write("Invalid path\r\n");
		return -1;
	}
	path_length = editor_string_length(path);
	if (path_length == 0U ||
		path_length >= VFS_PATH_MAX) {
		console_write("Invalid path\r\n");
		return -1;
	}
	for (unsigned int index = 0U; index <= path_length; ++index) {
		local_path[index] = path[index];
	}
	if (vfs_resolve(cwd, local_path, &node) == VFS_OK &&
		vfs_node_type(node) == VFS_NODE_DIRECTORY) {
		console_write("Is a directory\r\n");
		return -1;
	}
	editor_buffer_init(&buffer);
	editor_first_visible_line = 0ULL;
	error = editor_load(&buffer, cwd, local_path, &exists);
	if (error != VFS_OK) {
		console_write(vfs_error_string(error));
		console_write("\r\n");
		editor_buffer_destroy(&buffer);
		return -1;
	}
	(void)exists;
	for (;;) {
		struct key_event event;

		editor_render(&buffer, local_path, status);
		status = (const char *)0;
		event = terminal_read_key();
		if (event.code == KEY_CHAR && event.ctrl != 0U && event.ch == 's') {
			error = editor_save(&buffer, cwd, local_path);
			status = error == VFS_OK ? "Saved" : vfs_error_string(error);
			if (error != VFS_OK) {
				/* Keep dirty set so a failed save cannot look successful. */
				buffer.dirty = 1U;
			}
			force_quit = 0U;
			continue;
		}
		if (event.code == KEY_CHAR && event.ctrl != 0U && event.ch == 'q') {
			if (buffer.dirty == 0U || force_quit != 0U) {
				break;
			}
			force_quit = 1U;
			status = "Unsaved changes - press Ctrl-Q again to quit";
			continue;
		}
		force_quit = 0U;
		switch (event.code) {
		case KEY_CHAR:
			if (event.ctrl == 0U && event.ch >= 32 && event.ch <= 126) {
				error = editor_insert(&buffer, event.ch);
				if (error != VFS_OK) {
					status = vfs_error_string(error);
				}
			}
			break;
		case KEY_ENTER: error = editor_insert(&buffer, '\n');
			if (error != VFS_OK) status = vfs_error_string(error);
			break;
		case KEY_BACKSPACE: editor_backspace(&buffer); break;
		case KEY_DELETE: editor_delete(&buffer); break;
		case KEY_LEFT:
			if (buffer.cursor != 0ULL) --buffer.cursor;
			buffer.preferred_valid = 0U;
			break;
		case KEY_RIGHT:
			if (buffer.cursor < buffer.length) ++buffer.cursor;
			buffer.preferred_valid = 0U;
			break;
		case KEY_UP: editor_move_vertical(&buffer, -1); break;
		case KEY_DOWN: editor_move_vertical(&buffer, 1); break;
		case KEY_HOME: editor_move_home(&buffer); break;
		case KEY_END: editor_move_end(&buffer); break;
		default: break;
		}
	}
	editor_buffer_destroy(&buffer);
	terminal_clear();
	terminal_show_cursor();
	return 0;
}
