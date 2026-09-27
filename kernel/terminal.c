#include <nimera/console.h>
#include <nimera/compositor.h>
#include <nimera/display.h>
#include <nimera/format.h>
#include <nimera/irq.h>
#include <nimera/terminal.h>
#include <nimera/terminal_fb.h>
#include <nimera/timer.h>
#include <nimera/window.h>

#define TERMINAL_ESCAPE_TIMEOUT_MS 1000ULL
#define TERMINAL_GEOMETRY_TIMEOUT_MS 300ULL
#define TERMINAL_PENDING_CAPACITY 64U
#define TERMINAL_GEOMETRY_CANDIDATE_CAPACITY 32U
#define TERMINAL_MIN_COLUMNS 20U
#define TERMINAL_MAX_COLUMNS 500U
#define TERMINAL_MIN_ROWS 10U
#define TERMINAL_MAX_ROWS 200U

static char pending_input[TERMINAL_PENDING_CAPACITY];
static unsigned int pending_read;
static unsigned int pending_write;
static unsigned int terminal_column_count = TERMINAL_DEFAULT_COLUMNS;
static unsigned int terminal_row_count = TERMINAL_DEFAULT_ROWS;
static unsigned int terminal_has_geometry;

static int terminal_pending_push(char character)
{
	unsigned int next = (pending_write + 1U) % TERMINAL_PENDING_CAPACITY;

	if (next == pending_read) {
		return 0;
	}
	pending_input[pending_write] = character;
	pending_write = next;
	return 1;
}

static int terminal_pending_pop(char *result)
{
	if (pending_read == pending_write) {
		return 0;
	}
	*result = pending_input[pending_read];
	pending_read = (pending_read + 1U) % TERMINAL_PENDING_CAPACITY;
	return 1;
}

enum terminal_geometry_parse_result {
	TERMINAL_GEOMETRY_INCOMPLETE,
	TERMINAL_GEOMETRY_INVALID,
	TERMINAL_GEOMETRY_VALID
};

static enum terminal_geometry_parse_result terminal_parse_geometry_response(
		const char *bytes, unsigned int length, unsigned int *rows,
		unsigned int *columns)
{
	unsigned int index = 0U;
	unsigned int value;
	unsigned int digit;

	if (length == 0U) {
		return TERMINAL_GEOMETRY_INCOMPLETE;
	}
	if (bytes[0] != 27) {
		return TERMINAL_GEOMETRY_INVALID;
	}
	if (length == 1U) {
		return TERMINAL_GEOMETRY_INCOMPLETE;
	}
	if (bytes[1] != '[') {
		return TERMINAL_GEOMETRY_INVALID;
	}
	if (length == 2U) {
		return TERMINAL_GEOMETRY_INCOMPLETE;
	}
	if (bytes[2] != '8') {
		return TERMINAL_GEOMETRY_INVALID;
	}
	if (length == 3U) {
		return TERMINAL_GEOMETRY_INCOMPLETE;
	}
	if (bytes[3] != ';') {
		return TERMINAL_GEOMETRY_INVALID;
	}
	index = 4U;
	if (index == length) {
		return TERMINAL_GEOMETRY_INCOMPLETE;
	}
	value = 0U;
	while (index < length && bytes[index] >= '0' && bytes[index] <= '9') {
		digit = (unsigned int)(bytes[index] - '0');
		if (value > (~0U - digit) / 10U) {
			return TERMINAL_GEOMETRY_INVALID;
		}
		value = value * 10U + digit;
		++index;
	}
	if (index == 4U) {
		return TERMINAL_GEOMETRY_INVALID;
	}
	if (index == length) {
		return TERMINAL_GEOMETRY_INCOMPLETE;
	}
	if (bytes[index++] != ';') {
		return TERMINAL_GEOMETRY_INVALID;
	}
	*rows = value;
	if (index == length) {
		return TERMINAL_GEOMETRY_INCOMPLETE;
	}
	value = 0U;
	{
		unsigned int columns_start = index;

		while (index < length && bytes[index] >= '0' && bytes[index] <= '9') {
			digit = (unsigned int)(bytes[index] - '0');
			if (value > (~0U - digit) / 10U) {
				return TERMINAL_GEOMETRY_INVALID;
			}
			value = value * 10U + digit;
			++index;
		}
		if (index == columns_start) {
			return TERMINAL_GEOMETRY_INVALID;
		}
	}
	if (index == length) {
		return TERMINAL_GEOMETRY_INCOMPLETE;
	}
	if (bytes[index++] != 't' || index != length) {
		return TERMINAL_GEOMETRY_INVALID;
	}
	*columns = value;
	if (*rows < TERMINAL_MIN_ROWS || *rows > TERMINAL_MAX_ROWS ||
		*columns < TERMINAL_MIN_COLUMNS || *columns > TERMINAL_MAX_COLUMNS) {
		return TERMINAL_GEOMETRY_INVALID;
	}
	return TERMINAL_GEOMETRY_VALID;
}

static int terminal_collect_geometry_response(void)
{
	char candidate[TERMINAL_GEOMETRY_CANDIDATE_CAPACITY];
	unsigned int length = 0U;
	unsigned int rows;
	unsigned int columns;
	u64 deadline = timer_uptime_ms() + TERMINAL_GEOMETRY_TIMEOUT_MS;

	for (;;) {
		char character;
		enum terminal_geometry_parse_result result;

		if (console_try_getc(&character) == 0) {
			if (timer_uptime_ms() >= deadline) {
				return 0;
			}
			arch_wait_for_event();
			continue;
		}
		if (length == TERMINAL_GEOMETRY_CANDIDATE_CAPACITY) {
			for (unsigned int index = 0U; index < length; ++index) {
				if (terminal_pending_push(candidate[index]) == 0) {
					return 0;
				}
			}
			length = 0U;
		}
		candidate[length++] = character;
		result = terminal_parse_geometry_response(candidate, length, &rows,
											 &columns);
		if (result == TERMINAL_GEOMETRY_VALID) {
			terminal_row_count = rows;
			terminal_column_count = columns;
			return 1;
		}
		if (result == TERMINAL_GEOMETRY_INVALID) {
			for (unsigned int index = 0U; index < length; ++index) {
				if (terminal_pending_push(candidate[index]) == 0) {
					return 0;
				}
			}
			length = 0U;
		}
	}
}

void terminal_init(void)
{
	console_reset_output();
	pending_read = 0U;
	pending_write = 0U;
	terminal_column_count = TERMINAL_DEFAULT_COLUMNS;
	terminal_row_count = TERMINAL_DEFAULT_ROWS;
	terminal_has_geometry = 0U;
	#if NIMERA_FRAMEBUFFER_TERMINAL
	if (display_available() != 0 && terminal_fb_init() == 0) {
		debug_console_write("terminal: framebuffer backend ");
		debug_format_u64_decimal(terminal_fb_columns());
		debug_console_putc('x');
		debug_format_u64_decimal(terminal_fb_rows());
		debug_console_write("\r\n");
		console_set_output(terminal_fb_putc, terminal_fb_write);
		terminal_column_count = terminal_fb_columns();
		terminal_row_count = terminal_fb_rows();
		terminal_has_geometry = 1U;
		return;
	}
	debug_console_write("terminal: framebuffer unavailable, using ANSI/UART\r\n");
	#endif
	#if NIMERA_TERMINAL_SIZE_NO_RESPONSE
	return;
	#endif
	console_write("\033[18t");
	if (terminal_collect_geometry_response() != 0) {
		terminal_has_geometry = 1U;
	}
}

void terminal_begin_update(void)
{
	if (terminal_framebuffer_active() != 0) compositor_begin_update();
}

void terminal_end_update(void)
{
	if (terminal_framebuffer_active() != 0) compositor_end_update();
}

void terminal_cancel_update(void)
{
	if (terminal_framebuffer_active() != 0) compositor_cancel_update();
}

int terminal_framebuffer_active(void)
{
	#if NIMERA_FRAMEBUFFER_TERMINAL
	return terminal_fb_self_test();
	#else
	return 0;
	#endif
}

unsigned int terminal_geometry_detected(void)
{
	return terminal_has_geometry;
}

int terminal_geometry_self_test(void)
{
	static const char valid[] = "\033[8;30;120t";
	static const char malformed[] = "\033[8;;120t";
	static const char missing_number[] = "\033[8;30;t";
	static const char huge[] = "\033[8;999999999999999999999;120t";
	static const char bad_rows[] = "\033[8;9;120t";
	static const char bad_columns[] = "\033[8;30;501t";
	static const char surrounding[] = "a\033[8;30;120tb";
	unsigned int rows;
	unsigned int columns;

	if (terminal_parse_geometry_response(valid, sizeof(valid) - 1U, &rows,
									 &columns) != TERMINAL_GEOMETRY_VALID ||
		rows != 30U || columns != 120U ||
		terminal_parse_geometry_response(malformed, sizeof(malformed) - 1U,
									&rows, &columns) != TERMINAL_GEOMETRY_INVALID ||
		terminal_parse_geometry_response(missing_number,
									 sizeof(missing_number) - 1U, &rows,
									 &columns) != TERMINAL_GEOMETRY_INVALID ||
		terminal_parse_geometry_response(huge, sizeof(huge) - 1U, &rows,
									 &columns) != TERMINAL_GEOMETRY_INVALID ||
		terminal_parse_geometry_response(bad_rows, sizeof(bad_rows) - 1U, &rows,
									 &columns) != TERMINAL_GEOMETRY_INVALID ||
		terminal_parse_geometry_response(bad_columns,
									 sizeof(bad_columns) - 1U, &rows,
									 &columns) != TERMINAL_GEOMETRY_INVALID) {
		return 0;
	}
	if (surrounding[0] != 'a' || surrounding[sizeof(surrounding) - 2U] != 'b' ||
		terminal_parse_geometry_response(&surrounding[1], 11U, &rows,
									 &columns) != TERMINAL_GEOMETRY_VALID) {
		return 0;
	}
	return 1;
}

static struct key_event terminal_event(enum key_code code, char ch,
					       unsigned int ctrl)
{
	struct key_event event;

	event.code = code;
	event.ch = ch;
	event.ctrl = ctrl;
	return event;
}

static struct key_event terminal_character(char character)
{
	if (character == '\r' || character == '\n') {
		return terminal_event(KEY_ENTER, 0, 0U);
	}
	if (character == '\b' || character == 127) {
		return terminal_event(KEY_BACKSPACE, 0, 0U);
	}
	if (character == '\t') {
		return terminal_event(KEY_TAB, 0, 0U);
	}
	if (character >= 32 && character <= 126) {
		if (character == 's' || character == 'q') {
			/* Control letters are encoded as ASCII 0x13/0x11. */
			return terminal_event(KEY_CHAR, character, 0U);
		}
		return terminal_event(KEY_CHAR, character, 0U);
	}
	if (character == 19) {
		return terminal_event(KEY_CHAR, 's', 1U);
	}
	if (character == 17) {
		return terminal_event(KEY_CHAR, 'q', 1U);
	}
	return terminal_event(KEY_ESCAPE, 0, 0U);
}

static int terminal_wait_byte(char *result, u64 deadline)
{
	for (;;) {
		if (console_try_getc(result) != 0) {
			return 1;
		}
		if (timer_uptime_ms() >= deadline) {
			return 0;
		}
		arch_wait_for_event();
	}
}

static int terminal_next_byte(char *result)
{
	if (terminal_pending_pop(result) != 0) {
		return 1;
	}
	*result = console_getc();
	return 1;
}

static struct key_event terminal_escape(void)
{
	char character;
	u64 deadline = timer_uptime_ms() + TERMINAL_ESCAPE_TIMEOUT_MS;

	if (terminal_wait_byte(&character, deadline) == 0) {
		return terminal_event(KEY_ESCAPE, 0, 0U);
	}
	if (character != '[' && character != 'O') {
		(void)terminal_pending_push(character);
		return terminal_event(KEY_ESCAPE, 0, 0U);
	}
	if (terminal_wait_byte(&character, deadline) == 0) {
		return terminal_event(KEY_ESCAPE, 0, 0U);
	}
	switch (character) {
	case 'A': return terminal_event(KEY_UP, 0, 0U);
	case 'B': return terminal_event(KEY_DOWN, 0, 0U);
	case 'C': return terminal_event(KEY_RIGHT, 0, 0U);
	case 'D': return terminal_event(KEY_LEFT, 0, 0U);
	case 'H': return terminal_event(KEY_HOME, 0, 0U);
	case 'F': return terminal_event(KEY_END, 0, 0U);
	case '3':
		if (terminal_wait_byte(&character, deadline) != 0 && character == '~') {
			return terminal_event(KEY_DELETE, 0, 0U);
		}
		return terminal_event(KEY_ESCAPE, 0, 0U);
	default:
		return terminal_event(KEY_ESCAPE, 0, 0U);
	}
}

struct key_event terminal_read_key(void)
{
	char character;
	struct key_event event;
	struct pointer_event pointer;

	if (terminal_framebuffer_active() != 0 && input_hardware_available() != 0) {
		for (;;) {
			while (input_try_get_pointer_event(&pointer) != 0)
				window_manager_handle_pointer_event(&pointer);
			if (input_try_get_event(&event) != 0) {
#if NIMERA_WINDOW_TEST
				if (window_manager_terminal_focused() != 0) return event;
#else
				return event;
#endif
			}
			input_wait_for_activity();
		}
	}
	if (input_try_get_event(&event) != 0) return event;

	terminal_next_byte(&character);
	if (character == 27) {
		return terminal_escape();
	}
	return terminal_character(character);
}

void terminal_clear(void)
{
	if (terminal_framebuffer_active() != 0) {
		terminal_begin_update(); terminal_fb_clear(); terminal_end_update();
	}
	else debug_console_write("\033[2J\033[H");
}

void terminal_move_cursor(unsigned int row, unsigned int column)
{
	if (terminal_framebuffer_active() != 0) {
		terminal_begin_update(); terminal_fb_move_cursor(row, column); terminal_end_update();
		return;
	}
	debug_console_write("\033[");
	{
		static const char digits[] = "0123456789";
		char reversed[20];
		unsigned int count = 0U;
		u64 value = (u64)row + 1ULL;
		do { reversed[count++] = digits[value % 10ULL]; value /= 10ULL; } while (value != 0ULL);
		while (count != 0U) debug_console_putc(reversed[--count]);
	}
	debug_console_putc(';');
	{
		static const char digits[] = "0123456789";
		char reversed[20];
		unsigned int count = 0U;
		u64 value = (u64)column + 1ULL;
		do { reversed[count++] = digits[value % 10ULL]; value /= 10ULL; } while (value != 0ULL);
		while (count != 0U) debug_console_putc(reversed[--count]);
	}
	debug_console_putc('H');
}

void terminal_clear_line(void)
{
	if (terminal_framebuffer_active() != 0) {
		terminal_begin_update(); terminal_fb_clear_line(); terminal_end_update();
	}
	else debug_console_write("\033[2K");
}

void terminal_hide_cursor(void)
{
	if (terminal_framebuffer_active() != 0) {
		terminal_begin_update(); terminal_fb_hide_cursor(); terminal_end_update();
	}
	else debug_console_write("\033[?25l");
}

void terminal_show_cursor(void)
{
	if (terminal_framebuffer_active() != 0) {
		terminal_begin_update(); terminal_fb_show_cursor(); terminal_end_update();
	}
	else debug_console_write("\033[?25h");
}

unsigned int terminal_rows(void)
{
	return terminal_row_count;
}

unsigned int terminal_columns(void)
{
	return terminal_column_count;
}
