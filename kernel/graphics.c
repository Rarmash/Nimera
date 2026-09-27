#include <nimera/display.h>
#include <nimera/graphics.h>
#include <nimera/timer.h>
#include <nimera/console.h>

static const unsigned char font[27][5] = {
	{0,0,0,0,0}, {0x1e,0x05,0x05,0x1e,0}, {0x1f,0x15,0x15,0x0a,0},
	{0x0e,0x11,0x11,0x11,0}, {0x1f,0x11,0x11,0x0e,0}, {0x1f,0x15,0x15,0x11,0},
	{0x1f,0x05,0x05,0x01,0}, {0x0e,0x11,0x15,0x1d,0}, {0x1f,0x04,0x04,0x1f,0},
	{0x11,0x1f,0x11,0,0}, {0x08,0x10,0x10,0x0f,0}, {0x1f,0x04,0x0a,0x11,0},
	{0x1f,0x10,0x10,0,0}, {0x1f,0x02,0x04,0x02,0x1f}, {0x1f,0x02,0x04,0x08,0x1f},
	{0x0e,0x11,0x11,0x0e,0}, {0x1f,0x05,0x05,0x02,0}, {0x0e,0x11,0x19,0x1e,0},
	{0x1f,0x05,0x05,0x1a,0}, {0x12,0x15,0x15,0x09,0}, {0x01,0x01,0x1f,0x01,0x01},
	{0x0f,0x10,0x10,0x0f,0}, {0x07,0x08,0x10,0x08,0x07}, {0x1f,0x08,0x04,0x08,0x1f},
	{0x11,0x0a,0x04,0x0a,0x11}, {0x03,0x04,0x18,0x04,0x03}, {0x19,0x15,0x13,0x11,0}
};

static const unsigned char digits[10][5] = {
	{0x0e,0x11,0x11,0x11,0x0e}, {0x00,0x12,0x1f,0x10,0x00},
	{0x12,0x19,0x15,0x12,0x00}, {0x11,0x15,0x15,0x0a,0x00},
	{0x07,0x04,0x04,0x1f,0x04}, {0x17,0x15,0x15,0x09,0x00},
	{0x0e,0x15,0x15,0x08,0x00}, {0x01,0x01,0x1d,0x03,0x01},
	{0x0a,0x15,0x15,0x0a,0x00}, {0x02,0x15,0x15,0x0e,0x00}
};

static const unsigned char punctuation[16][5] = {
	{0x00,0x04,0x00,0x00,0x00}, /* ! */
	{0x00,0x04,0x00,0x04,0x00}, /* : */
	{0x00,0x00,0x00,0x00,0x00}, /* unused */
	{0x00,0x00,0x1f,0x00,0x00}, /* - */
	{0x00,0x00,0x00,0x00,0x00}, /* unused */
	{0x00,0x00,0x00,0x00,0x04}, /* . */
	{0x02,0x04,0x08,0x10,0x00}, /* / */
	{0x00,0x00,0x00,0x00,0x00}, /* unused */
	{0x00,0x00,0x00,0x00,0x00}, /* unused */
	{0x00,0x00,0x00,0x00,0x00}, /* unused */
	{0x00,0x00,0x00,0x00,0x00}, /* unused */
	{0x00,0x00,0x00,0x00,0x00}, /* unused */
	{0x00,0x00,0x00,0x00,0x00}, /* unused */
	{0x00,0x00,0x00,0x00,0x00}, /* unused */
	{0x00,0x00,0x00,0x00,0x00}, /* unused */
	{0x00,0x00,0x00,0x00,0x00}  /* unused */
};
static const unsigned char glyph_dollar[5] = {0x0a, 0x1f, 0x0a, 0x00, 0x00};
static const unsigned char glyph_underscore[5] = {0x10, 0x10, 0x10, 0x10, 0x10};
static const unsigned char glyph_bracket[5] = {0x1f, 0x11, 0x11, 0x00, 0x00};
static const unsigned char glyph_question[5] = {0x02, 0x01, 0x15, 0x02, 0x00};

static const unsigned char *graphics_glyph(char character)
{
	if (character >= 'a' && character <= 'z') character =
		(char)(character - 'a' + 'A');
	if (character >= 'A' && character <= 'Z') return font[character - 'A' + 1];
	if (character >= '0' && character <= '9') return digits[character - '0'];
	if (character == '!') return punctuation[0];
	if (character == ':') return punctuation[1];
	if (character == '-') return punctuation[3];
	if (character == '.') return punctuation[5];
	if (character == '/') return punctuation[6];
	if (character == '$') return glyph_dollar;
	if (character == '_') return glyph_underscore;
	if (character == '[' || character == ']') return glyph_bracket;
	return glyph_question;
}

void graphics_put_pixel(u64 x, u64 y, u32 color)
{
	if (!display_available() || x >= display_width() || y >= display_height()) return;
	display_framebuffer()[y * (display_pitch() / 4ULL) + x] = color;
}

void graphics_fill_rect(u64 x, u64 y, u64 w, u64 h, u32 color)
{
	if (!display_available() || x >= display_width() || y >= display_height()) return;
	if (w > display_width() - x) w = display_width() - x;
	if (h > display_height() - y) h = display_height() - y;
	for (u64 row = 0; row < h; ++row)
		for (u64 column = 0; column < w; ++column)
			graphics_put_pixel(x + column, y + row, color);
}

void graphics_clear(u32 color) { graphics_fill_rect(0, 0, display_width(), display_height(), color); }

unsigned int graphics_glyph_width(void) { return 10U; }
unsigned int graphics_glyph_height(void) { return 10U; }
unsigned int graphics_cell_width(void) { return 16U; }
unsigned int graphics_cell_height(void) { return 16U; }

void graphics_draw_char(u64 x, u64 y, char character, u32 foreground,
				u32 background)
{
	const unsigned char *glyph = graphics_glyph(character);

	graphics_fill_rect(x, y, graphics_cell_width(), graphics_cell_height(),
			   background);
	if (glyph == (const unsigned char *)0) return;
	for (u64 column = 0; column < 5ULL; ++column)
		for (u64 row = 0; row < 5ULL; ++row)
			if ((glyph[column] >> row) & 1U)
				graphics_fill_rect(x + column * 2ULL,
					y + row * 2ULL, 2ULL, 2ULL, foreground);
}

void graphics_draw_text(u64 x, u64 y, const char *text, u32 color)
{
	while (*text != '\0') {
		graphics_draw_char(x, y, *text++, color, 0U);
		x += graphics_cell_width();
	}
}

void graphics_test(void)
{
	if (!display_available()) { console_write("graphics: unavailable\r\n"); return; }
	console_write("Nimera graphics test\r\n");
	graphics_clear(0x00101828U);
	graphics_draw_text(24, 24, "NIMERA", 0x00ffffffU);
	graphics_draw_text(24, 48, "FIRST GRAPHICS OUTPUT", 0x0088ddffU);
	graphics_fill_rect(24, 80, display_width() / 3, 32, 0x00e05050U);
	graphics_fill_rect(48 + display_width() / 3, 80, display_width() / 3, 32, 0x0050d080U);
	graphics_fill_rect(72 + (display_width() * 2) / 3, 80, display_width() / 3 - 72, 32, 0x005080e0U);
	if (display_flush(0, 0, display_width(), display_height()) != 0) { console_write("initial flush: FAILED\r\n"); return; }
	console_write("initial flush: OK\r\n");
	{
		u64 start = timer_uptime_ms();
		while (timer_uptime_ms() - start < 250ULL) { }
	}
	graphics_fill_rect(24, 80, display_width() / 3, 32, 0x00f0b040U);
	console_write(display_flush(24, 80, display_width() / 3, 32) == 0 ? "partial update: OK\r\n" : "partial update: FAILED\r\n");
	console_write("Graphics test complete.\r\n");
}
