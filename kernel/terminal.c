#include <nimera/console.h>
#include <nimera/format.h>
#include <nimera/irq.h>
#include <nimera/terminal.h>
#include <nimera/timer.h>

#define TERMINAL_ESCAPE_TIMEOUT_MS 1000ULL

static unsigned int pending_valid;
static char pending_character;

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
	if (pending_valid != 0U) {
		*result = pending_character;
		pending_valid = 0U;
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
		pending_character = character;
		pending_valid = 1U;
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

	terminal_next_byte(&character);
	if (character == 27) {
		return terminal_escape();
	}
	return terminal_character(character);
}

void terminal_clear(void)
{
	console_write("\033[2J\033[H");
}

void terminal_move_cursor(unsigned int row, unsigned int column)
{
	console_write("\033[");
	format_u64_decimal((u64)row + 1ULL);
	console_putc(';');
	format_u64_decimal((u64)column + 1ULL);
	console_putc('H');
}

void terminal_clear_line(void)
{
	console_write("\033[2K");
}

void terminal_hide_cursor(void)
{
	console_write("\033[?25l");
}

void terminal_show_cursor(void)
{
	console_write("\033[?25h");
}

unsigned int terminal_rows(void)
{
	return TERMINAL_DEFAULT_ROWS;
}

unsigned int terminal_columns(void)
{
	return TERMINAL_DEFAULT_COLUMNS;
}
