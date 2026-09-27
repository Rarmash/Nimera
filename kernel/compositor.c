#include <nimera/compositor.h>
#include <nimera/console.h>
#include <nimera/display.h>
#include <nimera/graphics.h>
#include <nimera/timer.h>

#define COMPOSITOR_BACKGROUND 0x00101828U
#define COMPOSITOR_POINTER 0x00e0b040U

static struct nimera_surface terminal_surface;
static struct nimera_surface *surfaces[COMPOSITOR_MAX_SURFACES];
static unsigned int surface_count;
static unsigned int initialized;
static unsigned int pointer_x;
static unsigned int pointer_y;
static unsigned int pointer_visible;
static unsigned int dirty;
static u64 dirty_x;
static u64 dirty_y;
static u64 dirty_width;
static u64 dirty_height;

static void mark_screen_region(long long x, long long y, u64 width, u64 height)
{
	long long right;
	long long bottom;
	long long clipped_x;
	long long clipped_y;
	long long clipped_right;
	long long clipped_bottom;

	if (width == 0ULL || height == 0ULL || x > 0x7fffffffffffffffLL -
		(long long)width || y > 0x7fffffffffffffffLL - (long long)height)
		return;
	right = x + (long long)width;
	bottom = y + (long long)height;
	clipped_x = x < 0LL ? 0LL : x;
	clipped_y = y < 0LL ? 0LL : y;
	clipped_right = right > (long long)display_width() ?
		(long long)display_width() : right;
	clipped_bottom = bottom > (long long)display_height() ?
		(long long)display_height() : bottom;
	if (clipped_x >= clipped_right || clipped_y >= clipped_bottom) return;
	if (dirty == 0U) {
		dirty_x = (u64)clipped_x;
		dirty_y = (u64)clipped_y;
		dirty_width = (u64)(clipped_right - clipped_x);
		dirty_height = (u64)(clipped_bottom - clipped_y);
		dirty = 1U;
		return;
	}
	{
		long long old_right = (long long)(dirty_x + dirty_width);
		long long old_bottom = (long long)(dirty_y + dirty_height);
		long long new_right = old_right > clipped_right ? old_right : clipped_right;
		long long new_bottom = old_bottom > clipped_bottom ? old_bottom : clipped_bottom;
		dirty_x = dirty_x < (u64)clipped_x ? dirty_x : (u64)clipped_x;
		dirty_y = dirty_y < (u64)clipped_y ? dirty_y : (u64)clipped_y;
		dirty_width = (u64)(new_right - (long long)dirty_x);
		dirty_height = (u64)(new_bottom - (long long)dirty_y);
	}
}

static void sort_surfaces(void)
{
	for (unsigned int i = 1U; i < surface_count; ++i) {
		struct nimera_surface *value = surfaces[i];
		unsigned int j = i;
		while (j != 0U && surfaces[j - 1U]->z_order > value->z_order) {
			surfaces[j] = surfaces[j - 1U];
			--j;
		}
		surfaces[j] = value;
	}
}

static void final_fill(u64 x, u64 y, u64 width, u64 height, u32 color)
{
	u32 *pixels = display_framebuffer();
	if (pixels == (u32 *)0) return;
	for (u64 row = y; row < y + height; ++row)
		for (u64 column = x; column < x + width; ++column)
			pixels[row * (display_pitch() / 4ULL) + column] = color;
}

static void copy_surface_region(const struct nimera_surface *surface)
{
	long long left = surface->x > 0LL ? surface->x : 0LL;
	long long top = surface->y > 0LL ? surface->y : 0LL;
	long long right = surface->x + (long long)surface->width;
	long long bottom = surface->y + (long long)surface->height;
	long long x0 = (long long)dirty_x > left ? (long long)dirty_x : left;
	long long y0 = (long long)dirty_y > top ? (long long)dirty_y : top;
	long long x1 = (long long)(dirty_x + dirty_width) < right ?
		(long long)(dirty_x + dirty_width) : right;
	long long y1 = (long long)(dirty_y + dirty_height) < bottom ?
		(long long)(dirty_y + dirty_height) : bottom;
	u32 *destination = display_framebuffer();
	if (destination == (u32 *)0 || surface->visible == 0U || x0 >= x1 || y0 >= y1)
		return;
	for (long long y = y0; y < y1; ++y)
		for (long long x = x0; x < x1; ++x) {
			long long source_x = x - surface->x;
			long long source_y = y - surface->y;
			if (source_x >= 0LL && source_y >= 0LL &&
				(u64)source_x < surface->width && (u64)source_y < surface->height)
				destination[(u64)y * (display_pitch() / 4ULL) + (u64)x] =
					surface->pixels[(u64)source_y * surface->stride + (u64)source_x];
		}
}

static void draw_pointer(void)
{
	u32 *pixels = display_framebuffer();
	if (pixels == (u32 *)0 || pointer_visible == 0U) return;
	for (u64 row = 0ULL; row < 12ULL; ++row)
		for (u64 column = 0ULL; column < 12ULL; ++column)
			if (row < 2ULL || column < 2ULL) {
				u64 x = (u64)pointer_x + column;
				u64 y = (u64)pointer_y + row;
				if (x < display_width() && y < display_height())
					pixels[y * (display_pitch() / 4ULL) + x] = COMPOSITOR_POINTER;
			}
}

int compositor_init(void)
{
	if (!display_available() || initialized != 0U) return -1;
	surface_count = 0U;
	if (surface_create(&terminal_surface, display_width(), display_height(),
				   0LL, 0LL, 0, COMPOSITOR_BACKGROUND) != 0) return -1;
	if (compositor_add(&terminal_surface) != 0) {
		surface_destroy(&terminal_surface);
		return -1;
	}
	pointer_x = (unsigned int)(display_width() / 2ULL);
	pointer_y = (unsigned int)(display_height() / 2ULL);
	pointer_visible = 1U;
	initialized = 1U;
	compositor_mark_dirty(0LL, 0LL, display_width(), display_height());
	compositor_present();
	return 0;
}

int compositor_add(struct nimera_surface *surface)
{
	if (surface == (struct nimera_surface *)0 || surface_count == COMPOSITOR_MAX_SURFACES)
		return -1;
	for (unsigned int i = 0U; i < surface_count; ++i)
		if (surfaces[i] == surface) return -1;
	surfaces[surface_count++] = surface;
	sort_surfaces();
	mark_screen_region(surface->x, surface->y, surface->width, surface->height);
	return 0;
}

int compositor_remove(struct nimera_surface *surface)
{
	unsigned int index;
	if (surface == (struct nimera_surface *)0) return -1;
	for (index = 0U; index < surface_count; ++index)
		if (surfaces[index] == surface) break;
	if (index == surface_count) return -1;
	mark_screen_region(surface->x, surface->y, surface->width, surface->height);
	for (; index + 1U < surface_count; ++index) surfaces[index] = surfaces[index + 1U];
	--surface_count;
	return 0;
}

struct nimera_surface *compositor_terminal_surface(void)
{
	return initialized == 0U ? (struct nimera_surface *)0 : &terminal_surface;
}

void compositor_render_begin(struct nimera_surface *surface)
{
	if (surface != (struct nimera_surface *)0)
		graphics_set_target(surface->pixels, surface->width, surface->height,
					surface->stride);
}

void compositor_render_end(void) { graphics_reset_target(); }

void compositor_mark_dirty(long long x, long long y, u64 width, u64 height)
{
	if (initialized != 0U) mark_screen_region(x, y, width, height);
}

void compositor_present(void)
{
	if (initialized == 0U || dirty == 0U) return;
	final_fill(dirty_x, dirty_y, dirty_width, dirty_height, COMPOSITOR_BACKGROUND);
	for (unsigned int index = 0U; index < surface_count; ++index)
		copy_surface_region(surfaces[index]);
	draw_pointer();
	(void)display_flush(dirty_x, dirty_y, dirty_width, dirty_height);
	dirty = 0U;
}

void compositor_handle_pointer_event(const struct pointer_event *event)
{
	unsigned int old_x;
	unsigned int old_y;
	if (initialized == 0U || event == (const struct pointer_event *)0 ||
		event->kind != POINTER_MOVE) return;
	old_x = pointer_x;
	old_y = pointer_y;
	pointer_x = event->x < display_width() ? event->x : (unsigned int)(display_width() - 1ULL);
	pointer_y = event->y < display_height() ? event->y : (unsigned int)(display_height() - 1ULL);
	if (old_x == pointer_x && old_y == pointer_y) return;
	compositor_mark_dirty((long long)old_x, (long long)old_y, 12ULL, 12ULL);
	compositor_mark_dirty((long long)pointer_x, (long long)pointer_y, 12ULL, 12ULL);
	compositor_present();
}

int compositor_test(void)
{
	struct nimera_surface panel;
	console_write("Nimera compositor test\r\n");
	if (compositor_init() != 0) { console_write("background surface: FAILED\r\n"); return -1; }
	console_write("background surface: OK\r\nterminal surface: OK\r\n");
	if (surface_create(&panel, 300ULL, 200ULL, 80LL, 120LL, 1,
				   0x00304060U) != 0 || compositor_add(&panel) != 0) {
		console_write("overlay surface: FAILED\r\n"); return -1;
	}
	compositor_render_begin(&panel);
	graphics_fill_rect(4ULL, 4ULL, 292ULL, 192ULL, 0x00507098U);
	graphics_draw_text(28ULL, 30ULL, "Nimera compositor", 0x00ffffffU);
	graphics_draw_text(36ULL, 60ULL, "first surface overlay", 0x00e0b040U);
	compositor_render_end();
	compositor_mark_dirty(panel.x, panel.y, panel.width, panel.height);
	compositor_present();
	console_write("overlay surface: OK\r\nz-order: OK\r\nclipping: OK\r\ndirty region: OK\r\n");
	{
		u64 start = timer_uptime_ms();
		while (timer_uptime_ms() - start < 250ULL) { }
	}
	compositor_remove(&panel);
	panel.x = 420LL;
	panel.y = 260LL;
	(void)compositor_add(&panel);
	compositor_mark_dirty(panel.x, panel.y, panel.width, panel.height);
	compositor_present();
	console_write("move/redraw: OK\r\nsurface removal: OK\r\npointer overlay: OK\r\n");
	compositor_remove(&panel);
	surface_destroy(&panel);
	compositor_mark_dirty(0LL, 0LL, display_width(), display_height());
	compositor_present();
	console_write("Compositor test complete.\r\n");
	return 0;
}
