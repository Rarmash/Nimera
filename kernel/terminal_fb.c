#include <nimera/display.h>
#include <nimera/graphics.h>
#include <nimera/heap.h>
#include <nimera/terminal_fb.h>

#define FB_TERMINAL_FOREGROUND 0x00ffffffU
#define FB_TERMINAL_BACKGROUND 0x00101828U
#define FB_TERMINAL_CURSOR 0x00e0b040U

struct terminal_fb_cell {
	char character;
};

static struct terminal_fb_cell *cells;
static unsigned int rows;
static unsigned int columns;
static unsigned int cursor_row;
static unsigned int cursor_column;
static unsigned int cursor_visible;
static unsigned int active;

static struct terminal_fb_cell *fb_cell(unsigned int row, unsigned int column)
{
	return &cells[(u64)row * columns + column];
}

static void fb_flush_cell(unsigned int row, unsigned int column)
{
	graphics_draw_char((u64)column * graphics_cell_width(),
			   (u64)row * graphics_cell_height(),
			   fb_cell(row, column)->character,
			   FB_TERMINAL_FOREGROUND, FB_TERMINAL_BACKGROUND);
	display_flush((u64)column * graphics_cell_width(),
		      (u64)row * graphics_cell_height(),
		      graphics_cell_width(), graphics_cell_height());
}

static void fb_flush_cursor_cell(unsigned int row, unsigned int column,
				 unsigned int cursor)
{
	if (cursor == 0U) {
		fb_flush_cell(row, column);
		return;
	}
	graphics_draw_char((u64)column * graphics_cell_width(),
			   (u64)row * graphics_cell_height(),
			   fb_cell(row, column)->character,
			   FB_TERMINAL_BACKGROUND, FB_TERMINAL_CURSOR);
	display_flush((u64)column * graphics_cell_width(),
		      (u64)row * graphics_cell_height(),
		      graphics_cell_width(), graphics_cell_height());
}

static void fb_hide_cursor_at(unsigned int row, unsigned int column)
{
	if (cursor_visible != 0U && row < rows && column < columns)
		fb_flush_cursor_cell(row, column, 0U);
}

static void fb_show_cursor_at(unsigned int row, unsigned int column)
{
	if (cursor_visible != 0U && row < rows && column < columns)
		fb_flush_cursor_cell(row, column, 1U);
}

static void fb_scroll(void)
{
	for (unsigned int row = 1U; row < rows; ++row)
		for (unsigned int column = 0U; column < columns; ++column)
			*fb_cell(row - 1U, column) = *fb_cell(row, column);
	for (unsigned int column = 0U; column < columns; ++column)
		fb_cell(rows - 1U, column)->character = ' ';
	graphics_clear(FB_TERMINAL_BACKGROUND);
	for (unsigned int row = 0U; row < rows; ++row)
		for (unsigned int column = 0U; column < columns; ++column)
			graphics_draw_char((u64)column * graphics_cell_width(),
					   (u64)row * graphics_cell_height(),
					   fb_cell(row, column)->character,
					   FB_TERMINAL_FOREGROUND,
					   FB_TERMINAL_BACKGROUND);
	display_flush(0ULL, 0ULL, display_width(), display_height());
}

static void fb_newline(void)
{
	cursor_column = 0U;
	++cursor_row;
	if (cursor_row < rows) return;
	fb_scroll();
	cursor_row = rows - 1U;
}

int terminal_fb_init(void)
{
	u64 count;
	if (!display_available()) return -1;
	columns = (unsigned int)(display_width() / graphics_cell_width());
	rows = (unsigned int)(display_height() / graphics_cell_height());
	if (columns == 0U || rows == 0U) return -1;
	count = (u64)rows * columns;
	cells = (struct terminal_fb_cell *)kmalloc(count * sizeof(*cells));
	if (cells == (struct terminal_fb_cell *)0) return -1;
	for (u64 index = 0ULL; index < count; ++index) cells[index].character = ' ';
	cursor_row = 0U;
	cursor_column = 0U;
	cursor_visible = 1U;
	active = 1U;
	graphics_clear(FB_TERMINAL_BACKGROUND);
	display_flush(0ULL, 0ULL, display_width(), display_height());
	fb_show_cursor_at(cursor_row, cursor_column);
	return 0;
}

void terminal_fb_putc(char character)
{
	if (active == 0U) return;
	fb_hide_cursor_at(cursor_row, cursor_column);
	if (character == '\r') {
		cursor_column = 0U;
	} else if (character == '\n') {
		fb_newline();
	} else if (character == '\b') {
		if (cursor_column != 0U) {
			--cursor_column;
			fb_cell(cursor_row, cursor_column)->character = ' ';
			fb_flush_cell(cursor_row, cursor_column);
		}
	} else if (character >= 32 && character <= 126) {
		if (cursor_column == columns) fb_newline();
		fb_cell(cursor_row, cursor_column)->character = character;
		fb_flush_cell(cursor_row, cursor_column);
		++cursor_column;
		if (cursor_column == columns) fb_newline();
	}
	fb_show_cursor_at(cursor_row, cursor_column < columns ? cursor_column : 0U);
}

void terminal_fb_write(const char *text)
{
	while (*text != '\0') terminal_fb_putc(*text++);
}

void terminal_fb_clear(void)
{
	if (active == 0U) return;
	for (unsigned int row = 0U; row < rows; ++row)
		for (unsigned int column = 0U; column < columns; ++column)
			fb_cell(row, column)->character = ' ';
	cursor_row = 0U;
	cursor_column = 0U;
	graphics_clear(FB_TERMINAL_BACKGROUND);
	display_flush(0ULL, 0ULL, display_width(), display_height());
	fb_show_cursor_at(cursor_row, cursor_column);
}

void terminal_fb_move_cursor(unsigned int row, unsigned int column)
{
	if (active == 0U || row >= rows || column >= columns) return;
	fb_hide_cursor_at(cursor_row, cursor_column);
	cursor_row = row;
	cursor_column = column;
	fb_show_cursor_at(cursor_row, cursor_column);
}

void terminal_fb_clear_line(void)
{
	if (active == 0U) return;
	fb_hide_cursor_at(cursor_row, cursor_column);
	for (unsigned int column = 0U; column < columns; ++column)
		fb_cell(cursor_row, column)->character = ' ';
	for (unsigned int column = 0U; column < columns; ++column)
		fb_flush_cell(cursor_row, column);
	fb_show_cursor_at(cursor_row, cursor_column);
}

void terminal_fb_hide_cursor(void)
{
	fb_hide_cursor_at(cursor_row, cursor_column);
	cursor_visible = 0U;
}

void terminal_fb_show_cursor(void)
{
	cursor_visible = 1U;
	fb_show_cursor_at(cursor_row, cursor_column);
}

unsigned int terminal_fb_rows(void) { return rows; }
unsigned int terminal_fb_columns(void) { return columns; }

int terminal_fb_self_test(void)
{
	return active != 0U && rows != 0U && columns != 0U ? 1 : 0;
}
