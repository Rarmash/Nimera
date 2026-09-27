#include <nimera/display.h>

static u32 *framebuffer;
static u64 width;
static u64 height;
static u64 pitch;
static enum display_pixel_format format;
static int (*flush_callback)(u64, u64, u64, u64);

int display_available(void) { return framebuffer != (u32 *)0; }
u64 display_width(void) { return width; }
u64 display_height(void) { return height; }
u64 display_pitch(void) { return pitch; }
enum display_pixel_format display_pixel_format(void) { return format; }
u32 *display_framebuffer(void) { return framebuffer; }

void display_register(u32 *pixels, u64 w, u64 h, u64 p,
	                      enum display_pixel_format f,
	                      int (*flush)(u64, u64, u64, u64))
{
	framebuffer = pixels;
	width = w;
	height = h;
	pitch = p;
	format = f;
	flush_callback = flush;
}

int display_flush(u64 x, u64 y, u64 w, u64 h)
{
	if (!display_available() || flush_callback == (int (*)(u64, u64, u64, u64))0)
		return -1;
	if (x >= width || y >= height) return -1;
	if (w > width - x) w = width - x;
	if (h > height - y) h = height - y;
	return flush_callback(x, y, w, h);
}
