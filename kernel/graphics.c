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

static const unsigned char glyph_replacement[5] = {0x1f, 0x11, 0x15, 0x11, 0x1f};

static u32 *graphics_target_pixels;
static u64 graphics_target_width;
static u64 graphics_target_height;
static u64 graphics_target_stride;

static u32 *graphics_pixels(void)
{
	return graphics_target_pixels != (u32 *)0 ? graphics_target_pixels :
		display_framebuffer();
}

static u64 graphics_width(void)
{
	return graphics_target_pixels != (u32 *)0 ? graphics_target_width :
		display_width();
}

static u64 graphics_height(void)
{
	return graphics_target_pixels != (u32 *)0 ? graphics_target_height :
		display_height();
}

static u64 graphics_stride(void)
{
	return graphics_target_pixels != (u32 *)0 ? graphics_target_stride :
		display_pitch() / 4ULL;
}

static u32 graphics_cyrillic_ascii(u32 codepoint)
{
	/* The first Nimera Mono cut covers the common Cyrillic letters used by
	 * boot text; visually equivalent Latin shapes keep the font compact. */
	static const u32 upper[] = {
		'A','B','V','G','D','E','Z','Z','I','J','K','L','M','N','O','P',
		'R','S','T','U','F','H','C','C','S','S','Y','E','U','A','B','V','G','D'
	};
	if (codepoint >= 0x0410U && codepoint <= 0x0431U)
		return upper[codepoint - 0x0410U];
	if (codepoint >= 0x0430U && codepoint <= 0x0451U)
		return graphics_cyrillic_ascii(codepoint - 0x20U);
	return 0U;
}

static const unsigned char *graphics_glyph(u32 codepoint)
{
	u32 mapped = graphics_cyrillic_ascii(codepoint);
	if (mapped != 0U) codepoint = mapped;
	if (codepoint >= 'a' && codepoint <= 'z') codepoint = codepoint - 'a' + 'A';
	if (codepoint >= 'A' && codepoint <= 'Z') return font[codepoint - 'A' + 1];
	if (codepoint >= '0' && codepoint <= '9') return digits[codepoint - '0'];
	if (codepoint == '!') return punctuation[0];
	if (codepoint == ':') return punctuation[1];
	if (codepoint == '-') return punctuation[3];
	if (codepoint == '.') return punctuation[5];
	if (codepoint == '/') return punctuation[6];
	if (codepoint == '?') return glyph_question;
	if (codepoint == '$') return glyph_dollar;
	if (codepoint == '_') return glyph_underscore;
	if (codepoint == '[' || codepoint == ']') return glyph_bracket;
	if (codepoint == ' ') return (const unsigned char *)0;
	return glyph_replacement;
}

void graphics_put_pixel(u64 x, u64 y, u32 color)
{
	if (!display_available() || graphics_pixels() == (u32 *)0 ||
		x >= graphics_width() || y >= graphics_height()) return;
	graphics_pixels()[y * graphics_stride() + x] = color;
}

void graphics_fill_rect(u64 x, u64 y, u64 w, u64 h, u32 color)
{
	if (!display_available() || graphics_pixels() == (u32 *)0 ||
		x >= graphics_width() || y >= graphics_height()) return;
	if (w > graphics_width() - x) w = graphics_width() - x;
	if (h > graphics_height() - y) h = graphics_height() - y;
	for (u64 row = 0; row < h; ++row)
		for (u64 column = 0; column < w; ++column)
			graphics_put_pixel(x + column, y + row, color);
}

void graphics_clear(u32 color) { graphics_fill_rect(0, 0, graphics_width(), graphics_height(), color); }

void graphics_set_target(u32 *pixels, u64 width, u64 height, u64 stride)
{
	graphics_target_pixels = pixels;
	graphics_target_width = width;
	graphics_target_height = height;
	graphics_target_stride = stride;
}

void graphics_reset_target(void)
{
	graphics_target_pixels = (u32 *)0;
	graphics_target_width = 0ULL;
	graphics_target_height = 0ULL;
	graphics_target_stride = 0ULL;
}

unsigned int graphics_glyph_width(void) { return 5U; }
unsigned int graphics_glyph_height(void) { return 10U; }
unsigned int graphics_cell_width(void) { return 8U; }
unsigned int graphics_cell_height(void) { return 16U; }

void graphics_draw_codepoint(u64 x, u64 y, u32 codepoint, u32 foreground,
				u32 background)
{
	const unsigned char *glyph = graphics_glyph(codepoint);

	graphics_fill_rect(x, y, graphics_cell_width(), graphics_cell_height(),
			   background);
	if (glyph == (const unsigned char *)0) return;
	for (u64 column = 0; column < 5ULL; ++column)
		for (u64 row = 0; row < 5ULL; ++row)
			if ((glyph[column] >> row) & 1U)
				graphics_fill_rect(x + column, y + 3ULL + row * 2ULL,
					1ULL, 2ULL, foreground);
}

void graphics_draw_char(u64 x, u64 y, char character, u32 foreground,
				u32 background)
{
	graphics_draw_codepoint(x, y, (u32)(unsigned char)character,
				foreground, background);
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
