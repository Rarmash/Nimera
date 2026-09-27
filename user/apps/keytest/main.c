#include <nimera/user.h>

static unsigned long long text_length(const char *text)
{
	unsigned long long length = 0ULL;
	while (text[length] != '\0') ++length;
	return length;
}

static void write_text(const char *text)
{
	(void)nimera_write(text, text_length(text));
}

static void write_number(unsigned int value)
{
	char digits[10];
	unsigned int count = 0U;
	if (value == 0U) { (void)nimera_write("0", 1ULL); return; }
	while (value != 0U) { digits[count++] = (char)('0' + value % 10U); value /= 10U; }
	while (count != 0U) (void)nimera_write(&digits[--count], 1ULL);
}

static const char *key_name(const struct nimera_key_event *event)
{
	if ((event->modifiers & NIMERA_KEY_MOD_CTRL) != 0U && event->ch == 'q') return "CTRL-Q";
	switch (event->code) {
	case NIMERA_KEY_ENTER: return "ENTER";
	case NIMERA_KEY_BACKSPACE: return "BACKSPACE";
	case NIMERA_KEY_DELETE: return "DELETE";
	case NIMERA_KEY_UP: return "UP";
	case NIMERA_KEY_DOWN: return "DOWN";
	case NIMERA_KEY_LEFT: return "LEFT";
	case NIMERA_KEY_RIGHT: return "RIGHT";
	case NIMERA_KEY_HOME: return "HOME";
	case NIMERA_KEY_END: return "END";
	case NIMERA_KEY_ESCAPE: return "ESCAPE";
	case NIMERA_KEY_CHAR: return "CHAR";
	default: return "UNKNOWN";
	}
}

int main(void)
{
	struct nimera_terminal_size size;
	struct nimera_key_event event;
	if (nimera_terminal_size(&size) != 0) return 1;
	if (nimera_terminal_clear() != 0) return 1;
	if (nimera_terminal_cursor_visible(0) != 0) return 1;
	(void)nimera_terminal_move_cursor(0U, 0U);
	write_text("Nimera EL0 terminal test");
	(void)nimera_terminal_move_cursor(2U, 0U);
	write_text("Terminal: "); write_number(size.columns); write_text("x"); write_number(size.rows);
	(void)nimera_terminal_move_cursor(4U, 0U);
	write_text("Press keys...");
	(void)nimera_terminal_move_cursor(size.rows - 2U, 0U);
	write_text("Last key: ");
	(void)nimera_terminal_move_cursor(size.rows - 1U, 0U);
	write_text("Ctrl-Q to quit");
	for (;;) {
		if (nimera_read_key(&event) != 0) return 1;
		if ((event.modifiers & NIMERA_KEY_MOD_CTRL) != 0U && event.ch == 'q') break;
		(void)nimera_terminal_move_cursor(size.rows - 2U, 0U);
		(void)nimera_terminal_clear_line();
		write_text("Last key: "); write_text(key_name(&event));
	}
	return 0;
}
