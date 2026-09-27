#include <nimera/compositor.h>
#include <nimera/graphics.h>
#include <nimera/heap.h>
#include <nimera/terminal_fb.h>
#include <nimera/utf8.h>
#include <nimera/window.h>

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
static struct utf8_decoder decoder;

static struct nimera_surface *fb_surface(void)
{
#if NIMERA_WINDOW_TEST
	return window_manager_terminal_client_surface();
#else
	return compositor_terminal_surface();
#endif
}

static struct terminal_fb_cell *fb_cell(unsigned int row, unsigned int column)
{
	u64 index = (u64)row * columns + column;
	return &cell_chunks[index / FB_CELL_CHUNK_CAPACITY]
		[index % FB_CELL_CHUNK_CAPACITY];
}

static void fb_flush_cell(unsigned int row, unsigned int column)
{
	struct nimera_surface *surface = fb_surface();
	if (surface == (struct nimera_surface *)0) return;
	compositor_render_begin(surface);
	graphics_draw_codepoint((u64)column * graphics_cell_width(),
			   (u64)row * graphics_cell_height(),
			   fb_cell(row, column)->codepoint,
			   FB_TERMINAL_FOREGROUND, FB_TERMINAL_BACKGROUND);
	compositor_render_end();
	compositor_mark_dirty(surface->x + (long long)column * graphics_cell_width(),
				      surface->y + (long long)row * graphics_cell_height(),
				      graphics_cell_width(), graphics_cell_height());
	compositor_present();
}

static void fb_flush_cursor_cell(unsigned int row, unsigned int column,
				 unsigned int cursor)
{
	if (cursor == 0U) {
		fb_flush_cell(row, column);
		return;
	}
	{
		struct nimera_surface *surface = fb_surface();
		if (surface == (struct nimera_surface *)0) return;
		compositor_render_begin(surface);
	graphics_draw_codepoint((u64)column * graphics_cell_width(),
			   (u64)row * graphics_cell_height(),
			   fb_cell(row, column)->codepoint,
			   FB_TERMINAL_BACKGROUND, FB_TERMINAL_CURSOR);
		compositor_render_end();
	}
	compositor_mark_dirty(fb_surface()->x + (long long)column * graphics_cell_width(),
				      fb_surface()->y + (long long)row * graphics_cell_height(),
				      graphics_cell_width(), graphics_cell_height());
	compositor_present();
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
		fb_cell(rows - 1U, column)->codepoint = ' ';
	compositor_render_begin(fb_surface());
	graphics_clear(FB_TERMINAL_BACKGROUND);
	for (unsigned int row = 0U; row < rows; ++row)
		for (unsigned int column = 0U; column < columns; ++column)
			graphics_draw_codepoint((u64)column * graphics_cell_width(),
					   (u64)row * graphics_cell_height(),
					   fb_cell(row, column)->codepoint,
					   FB_TERMINAL_FOREGROUND,
					   FB_TERMINAL_BACKGROUND);
	compositor_render_end();
	compositor_mark_dirty(fb_surface()->x, fb_surface()->y,
			      fb_surface()->width, fb_surface()->height);
	compositor_present();
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
#if NIMERA_WINDOW_TEST
	if (window_manager_init() != 0) return -1;
#else
	if (compositor_init() != 0) return -1;
#endif
	columns = (unsigned int)(fb_surface()->width / graphics_cell_width());
	rows = (unsigned int)(fb_surface()->height / graphics_cell_height());
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
	active = 1U;
	compositor_render_begin(fb_surface());
	graphics_clear(FB_TERMINAL_BACKGROUND);
	compositor_render_end();
	compositor_mark_dirty(fb_surface()->x, fb_surface()->y,
			      fb_surface()->width, fb_surface()->height);
	compositor_present();
	fb_show_cursor_at(cursor_row, cursor_column);
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
	compositor_render_begin(fb_surface());
	graphics_clear(FB_TERMINAL_BACKGROUND);
	compositor_render_end();
	compositor_mark_dirty(fb_surface()->x, fb_surface()->y,
			      fb_surface()->width, fb_surface()->height);
	compositor_present();
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

unsigned int terminal_fb_rows(void) { return rows; }
unsigned int terminal_fb_columns(void) { return columns; }

int terminal_fb_self_test(void)
{
	return active != 0U && rows != 0U && columns != 0U && utf8_self_test() != 0 ? 1 : 0;
}
