#include <nimera/display.h>
#include <nimera/graphics.h>
#include <nimera/heap.h>
#include <nimera/terminal_fb.h>
#include <nimera/utf8.h>

#define FB_TERMINAL_FOREGROUND 0x00ffffffU
#define FB_TERMINAL_BACKGROUND 0x00101828U
#define FB_TERMINAL_CURSOR 0x00e0b040U
#define FB_CELL_CHUNK_CAPACITY 512U
#define FB_CELL_CHUNK_COUNT 16U

struct terminal_fb_cell {
	u32 codepoint;
};

static struct terminal_fb_cell *cell_chunks[FB_CELL_CHUNK_COUNT];
static unsigned int rows;
static unsigned int columns;
static unsigned int cursor_row;
static unsigned int cursor_column;
static unsigned int cursor_visible;
static unsigned int active;
static unsigned int pointer_x;
static unsigned int pointer_y;
static unsigned int pointer_visible;
static struct utf8_decoder decoder;

static struct terminal_fb_cell *fb_cell(unsigned int row, unsigned int column)
{
	u64 index = (u64)row * columns + column;
	return &cell_chunks[index / FB_CELL_CHUNK_CAPACITY]
		[index % FB_CELL_CHUNK_CAPACITY];
}

static void fb_flush_cell(unsigned int row, unsigned int column)
{
	graphics_draw_codepoint((u64)column * graphics_cell_width(),
			   (u64)row * graphics_cell_height(),
			   fb_cell(row, column)->codepoint,
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
	graphics_draw_codepoint((u64)column * graphics_cell_width(),
			   (u64)row * graphics_cell_height(),
			   fb_cell(row, column)->codepoint,
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

static void fb_redraw_cell(unsigned int row, unsigned int column)
{
	if (cursor_visible != 0U && row == cursor_row && column == cursor_column)
		fb_flush_cursor_cell(row, column, 1U);
	else
		fb_flush_cell(row, column);
}

static void fb_draw_pointer(void)
{
	/* A small cross is an overlay; terminal cells remain the backing image. */
	graphics_fill_rect(pointer_x, pointer_y, 2ULL, 12ULL, FB_TERMINAL_CURSOR);
	graphics_fill_rect(pointer_x, pointer_y, 12ULL, 2ULL, FB_TERMINAL_CURSOR);
	display_flush(pointer_x, pointer_y, 12ULL, 12ULL);
}

static void fb_redraw_pointer_area(unsigned int x, unsigned int y)
{
	unsigned int first_row = y / graphics_cell_height();
	unsigned int first_column = x / graphics_cell_width();
	unsigned int last_row = (y + 11U) / graphics_cell_height();
	unsigned int last_column = (x + 11U) / graphics_cell_width();

	if (last_row >= rows) last_row = rows - 1U;
	if (last_column >= columns) last_column = columns - 1U;
	for (unsigned int row = first_row; row <= last_row; ++row)
		for (unsigned int column = first_column; column <= last_column; ++column)
			fb_redraw_cell(row, column);
}

static void fb_scroll(void)
{
	for (unsigned int row = 1U; row < rows; ++row)
		for (unsigned int column = 0U; column < columns; ++column)
			*fb_cell(row - 1U, column) = *fb_cell(row, column);
	for (unsigned int column = 0U; column < columns; ++column)
		fb_cell(rows - 1U, column)->codepoint = ' ';
	graphics_clear(FB_TERMINAL_BACKGROUND);
	for (unsigned int row = 0U; row < rows; ++row)
		for (unsigned int column = 0U; column < columns; ++column)
			graphics_draw_codepoint((u64)column * graphics_cell_width(),
					   (u64)row * graphics_cell_height(),
					   fb_cell(row, column)->codepoint,
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
	if ((count + FB_CELL_CHUNK_CAPACITY - 1ULL) /
		FB_CELL_CHUNK_CAPACITY > FB_CELL_CHUNK_COUNT) return -1;
	for (u64 chunk = 0ULL; chunk * FB_CELL_CHUNK_CAPACITY < count; ++chunk) {
		u64 remaining = count - chunk * FB_CELL_CHUNK_CAPACITY;
		u64 entries = remaining < FB_CELL_CHUNK_CAPACITY ? remaining :
			FB_CELL_CHUNK_CAPACITY;
		cell_chunks[chunk] = (struct terminal_fb_cell *)kmalloc(
			entries * sizeof(struct terminal_fb_cell));
		if (cell_chunks[chunk] == (struct terminal_fb_cell *)0) {
			while (chunk != 0ULL) kfree(cell_chunks[--chunk]);
			return -1;
		}
		for (u64 index = 0ULL; index < entries; ++index)
			cell_chunks[chunk][index].codepoint = ' ';
	}
	utf8_decoder_init(&decoder);
	cursor_row = 0U;
	cursor_column = 0U;
	cursor_visible = 1U;
	pointer_x = (unsigned int)(display_width() / 2ULL);
	pointer_y = (unsigned int)(display_height() / 2ULL);
	pointer_visible = 1U;
	active = 1U;
	graphics_clear(FB_TERMINAL_BACKGROUND);
	display_flush(0ULL, 0ULL, display_width(), display_height());
	fb_show_cursor_at(cursor_row, cursor_column);
	fb_draw_pointer();
	return 0;
}

static void terminal_fb_put_codepoint(u32 codepoint)
{
	if (active == 0U) return;
	fb_hide_cursor_at(cursor_row, cursor_column);
	if (codepoint == '\r') {
		cursor_column = 0U;
	} else if (codepoint == '\n') {
		fb_newline();
	} else if (codepoint == '\b') {
		if (cursor_column != 0U) {
			--cursor_column;
			fb_cell(cursor_row, cursor_column)->codepoint = ' ';
			fb_flush_cell(cursor_row, cursor_column);
		}
	} else if (codepoint >= 32U && codepoint != 127U) {
		if (cursor_column == columns) fb_newline();
		fb_cell(cursor_row, cursor_column)->codepoint = codepoint;
		fb_flush_cell(cursor_row, cursor_column);
		++cursor_column;
		if (cursor_column == columns) fb_newline();
	}
	fb_show_cursor_at(cursor_row, cursor_column < columns ? cursor_column : 0U);
}

void terminal_fb_putc(char character)
{
	terminal_fb_put_codepoint((u32)(unsigned char)character);
}

void terminal_fb_write(const char *text)
{
	while (*text != '\0') {
		u32 output[2];
		unsigned int count = utf8_decoder_push(&decoder,
				(unsigned char)*text++, output);
		for (unsigned int index = 0U; index < count; ++index)
			terminal_fb_put_codepoint(output[index]);
	}
}

void terminal_fb_clear(void)
{
	if (active == 0U) return;
	for (unsigned int row = 0U; row < rows; ++row)
		for (unsigned int column = 0U; column < columns; ++column)
			fb_cell(row, column)->codepoint = ' ';
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
		fb_cell(cursor_row, column)->codepoint = ' ';
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

void terminal_fb_handle_pointer_event(const struct pointer_event *event)
{
	unsigned int old_x;
	unsigned int old_y;

	if (active == 0U || event == (const struct pointer_event *)0) return;
	if (event->kind == POINTER_MOVE) {
		old_x = pointer_x;
		old_y = pointer_y;
		pointer_x = event->x < display_width() ? event->x :
			(unsigned int)(display_width() - 1ULL);
		pointer_y = event->y < display_height() ? event->y :
			(unsigned int)(display_height() - 1ULL);
		if (pointer_visible == 0U || (old_x == pointer_x && old_y == pointer_y))
			return;
		fb_redraw_pointer_area(old_x, old_y);
		fb_draw_pointer();
	}
}

unsigned int terminal_fb_rows(void) { return rows; }
unsigned int terminal_fb_columns(void) { return columns; }

int terminal_fb_self_test(void)
{
	return active != 0U && rows != 0U && columns != 0U && utf8_self_test() != 0 ? 1 : 0;
}
