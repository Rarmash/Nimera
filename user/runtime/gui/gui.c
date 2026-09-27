#include <nimera/gui.h>

const unsigned char *nimera_gui_font_rows(u32 codepoint);

static long long min_ll(long long a, long long b) { return a < b ? a : b; }
static long long max_ll(long long a, long long b) { return a > b ? a : b; }

static struct nimera_gui_rect canvas_bounds(const struct nimera_gui_canvas *canvas)
{
	struct nimera_gui_rect result = {0, 0, canvas->width, canvas->height};
	return result;
}

static unsigned int utf8_next(const char **text)
{
	const unsigned char *p = (const unsigned char *)*text;
	u32 value;
	if (*p < 0x80U) { ++*text; return *p; }
	if ((*p & 0xe0U) == 0xc0U && p[1] != 0U) {
		value = ((u32)(p[0] & 0x1fU) << 6) | (u32)(p[1] & 0x3fU); *text += 2; return value;
	}
	if ((*p & 0xf0U) == 0xe0U && p[1] != 0U && p[2] != 0U) {
		value = ((u32)(p[0] & 0x0fU) << 12) | ((u32)(p[1] & 0x3fU) << 6) | (u32)(p[2] & 0x3fU); *text += 3; return value;
	}
	if ((*p & 0xf8U) == 0xf0U && p[1] != 0U && p[2] != 0U && p[3] != 0U) {
		value = ((u32)(p[0] & 7U) << 18) | ((u32)(p[1] & 0x3fU) << 12) |
			((u32)(p[2] & 0x3fU) << 6) | (u32)(p[3] & 0x3fU); *text += 4; return value;
	}
	++*text;
	return 0xfffdU;
}

static int rect_empty(struct nimera_gui_rect rect) { return rect.width == 0ULL || rect.height == 0ULL; }

struct nimera_gui_rect nimera_gui_rect_intersection(struct nimera_gui_rect a,
	struct nimera_gui_rect b)
{
	long long left = max_ll(a.x, b.x), top = max_ll(a.y, b.y);
	long long right = min_ll(a.x + (long long)a.width, b.x + (long long)b.width);
	long long bottom = min_ll(a.y + (long long)a.height, b.y + (long long)b.height);
	struct nimera_gui_rect result = {left, top, 0ULL, 0ULL};
	if (right > left && bottom > top) { result.width = (u64)(right - left); result.height = (u64)(bottom - top); }
	return result;
}

int nimera_gui_rect_contains(struct nimera_gui_rect rect, long long x, long long y)
{
	return x >= rect.x && y >= rect.y && x < rect.x + (long long)rect.width && y < rect.y + (long long)rect.height;
}

void nimera_gui_canvas_init(struct nimera_gui_canvas *canvas, u32 *pixels,
	u64 width, u64 height, u64 stride)
{
	canvas->pixels = pixels; canvas->width = width; canvas->height = height; canvas->stride = stride;
	canvas->clip = canvas_bounds(canvas);
}

void nimera_gui_canvas_set_clip(struct nimera_gui_canvas *canvas, struct nimera_gui_rect clip)
{ canvas->clip = nimera_gui_rect_intersection(canvas_bounds(canvas), clip); }

void nimera_gui_fill_rect(struct nimera_gui_canvas *canvas, struct nimera_gui_rect rect, u32 color)
{
	struct nimera_gui_rect clipped = nimera_gui_rect_intersection(canvas->clip, rect);
	if (rect_empty(clipped) || canvas->pixels == (u32 *)0) return;
	for (u64 y = 0ULL; y < clipped.height; ++y)
		for (u64 x = 0ULL; x < clipped.width; ++x)
			canvas->pixels[(u64)(clipped.y + (long long)y) * canvas->stride + (u64)(clipped.x + (long long)x)] = color;
}

void nimera_gui_clear(struct nimera_gui_canvas *canvas, u32 color)
{ nimera_gui_fill_rect(canvas, canvas_bounds(canvas), color); }

void nimera_gui_draw_rect(struct nimera_gui_canvas *canvas, struct nimera_gui_rect rect, u32 color)
{
	if (rect.width == 0ULL || rect.height == 0ULL) return;
	nimera_gui_fill_rect(canvas, (struct nimera_gui_rect){rect.x, rect.y, rect.width, 2ULL}, color);
	nimera_gui_fill_rect(canvas, (struct nimera_gui_rect){rect.x, rect.y + (long long)rect.height - 2LL, rect.width, 2ULL}, color);
	nimera_gui_fill_rect(canvas, (struct nimera_gui_rect){rect.x, rect.y, 2ULL, rect.height}, color);
	nimera_gui_fill_rect(canvas, (struct nimera_gui_rect){rect.x + (long long)rect.width - 2LL, rect.y, 2ULL, rect.height}, color);
}

unsigned int nimera_gui_text_width(const char *text)
{
	unsigned int count = 0U;
	while (*text != '\0') { (void)utf8_next(&text); ++count; }
	return count * 10U;
}

unsigned int nimera_gui_font_line_height(void) { return 18U; }

void nimera_gui_draw_text(struct nimera_gui_canvas *canvas, long long x, long long y,
	const char *text, u32 color)
{
	while (*text != '\0') {
		u32 cp = utf8_next(&text);
		const unsigned char *rows = nimera_gui_font_rows(cp);
		if (rows != (const unsigned char *)0)
			for (unsigned int row = 0U; row < 7U; ++row)
				for (unsigned int column = 0U; column < 5U; ++column)
					if ((rows[row] & (1U << (4U - column))) != 0U)
						nimera_gui_fill_rect(canvas, (struct nimera_gui_rect){x + (long long)column * 2LL, y + (long long)row * 2LL, 2ULL, 2ULL}, color);
		x += 10LL;
	}
}

void nimera_gui_damage_reset(struct nimera_gui_damage *damage)
{ damage->rect = (struct nimera_gui_rect){0, 0, 0ULL, 0ULL}; damage->dirty = 0U; }

void nimera_gui_damage_add(struct nimera_gui_damage *damage, struct nimera_gui_rect rect)
{
	if (rect_empty(rect)) return;
	if (damage->dirty == 0U) damage->rect = rect;
	else {
		long long left = min_ll(damage->rect.x, rect.x), top = min_ll(damage->rect.y, rect.y);
		long long right = max_ll(damage->rect.x + (long long)damage->rect.width, rect.x + (long long)rect.width);
		long long bottom = max_ll(damage->rect.y + (long long)damage->rect.height, rect.y + (long long)rect.height);
		damage->rect = (struct nimera_gui_rect){left, top, (u64)(right - left), (u64)(bottom - top)};
	}
	damage->dirty = 1U;
}

int nimera_gui_window_create(struct nimera_gui_window *window, unsigned int width,
	unsigned int height, const char *title, u64 title_length, u32 flags)
{
	struct nimera_window_info info;
	long long result = nimera_window_create(width, height, title, title_length, flags, &info);
	if (result < 0LL) return (int)result;
	window->handle = (u64)result;
	nimera_gui_canvas_init(&window->canvas, (u32 *)(unsigned long)info.client_address,
		info.width, info.height, info.stride_pixels);
	return 0;
}

void nimera_gui_window_destroy(struct nimera_gui_window *window)
{ if (window->handle != 0ULL) { (void)nimera_window_destroy(window->handle); window->handle = 0ULL; } }

int nimera_gui_window_present(struct nimera_gui_window *window, const struct nimera_gui_damage *damage)
{
	if (damage != (const struct nimera_gui_damage *)0 && damage->dirty != 0U)
		return nimera_window_present(window->handle, (u64)damage->rect.x, (u64)damage->rect.y, damage->rect.width, damage->rect.height);
	return nimera_window_present(window->handle, 0ULL, 0ULL, window->canvas.width, window->canvas.height);
}

int nimera_gui_window_apply_event(struct nimera_gui_window *window, const struct nimera_window_event *event)
{
	if (event->type != NIMERA_WINDOW_EVENT_RESIZED) return 0;
	nimera_gui_canvas_init(&window->canvas, (u32 *)(unsigned long)event->client_address,
		event->client_width, event->client_height, event->stride_pixels);
	return 1;
}

int nimera_gui_window_next_event(struct nimera_gui_window *window, struct nimera_window_event *event)
{
	int result = nimera_window_read_event(window->handle, event);
	if (result == 0) (void)nimera_gui_window_apply_event(window, event);
	return result;
}

void nimera_gui_label_draw(struct nimera_gui_canvas *canvas, const struct nimera_gui_label *label, u32 color)
{ nimera_gui_draw_text(canvas, label->rect.x, label->rect.y, label->text, color); }

void nimera_gui_button_draw(struct nimera_gui_canvas *canvas, const struct nimera_gui_button *button)
{
	u32 background = button->pressed != 0U ? NIMERA_GUI_COLOR_PRESSED :
		(button->hovered != 0U ? NIMERA_GUI_COLOR_HOVER : NIMERA_GUI_COLOR_BUTTON);
	nimera_gui_fill_rect(canvas, button->rect, background);
	nimera_gui_draw_rect(canvas, button->rect, NIMERA_GUI_COLOR_BORDER);
	unsigned int width = nimera_gui_text_width(button->label);
	long long x = button->rect.x + ((long long)button->rect.width - (long long)width) / 2LL;
	long long y = button->rect.y + ((long long)button->rect.height - 14LL) / 2LL;
	nimera_gui_draw_text(canvas, x, y, button->label, NIMERA_GUI_COLOR_TEXT);
}

int nimera_gui_button_handle_event(struct nimera_gui_button *button,
	const struct nimera_window_event *event, unsigned int *clicked)
{
	unsigned int old_hovered = button->hovered, old_pressed = button->pressed;
	*clicked = 0U;
	if (event->type == NIMERA_WINDOW_POINTER_MOVE)
		button->hovered = nimera_gui_rect_contains(button->rect, (long long)event->x, (long long)event->y) ? 1U : 0U;
	else if (event->type == NIMERA_WINDOW_POINTER_BUTTON_DOWN && event->button == 1U &&
		nimera_gui_rect_contains(button->rect, (long long)event->x, (long long)event->y)) button->pressed = 1U;
	else if (event->type == NIMERA_WINDOW_POINTER_BUTTON_UP && event->button == 1U) {
		if (button->pressed != 0U && nimera_gui_rect_contains(button->rect, (long long)event->x, (long long)event->y)) *clicked = 1U;
		button->pressed = 0U;
	}
	return old_hovered != button->hovered || old_pressed != button->pressed || *clicked != 0U;
}

int nimera_gui_runtime_self_test(void)
{
	u32 pixels[64 * 40];
	struct nimera_gui_canvas canvas;
	struct nimera_gui_button button = {{8, 8, 48ULL, 20ULL}, "OK", 0U, 0U};
	struct nimera_gui_window window;
	struct nimera_window_event event = {0};
	struct nimera_gui_damage damage;
	nimera_gui_canvas_init(&canvas, pixels, 64ULL, 40ULL, 64ULL);
	nimera_gui_clear(&canvas, 0U);
	nimera_gui_fill_rect(&canvas, (struct nimera_gui_rect){-4, -3, 12ULL, 12ULL}, 1U);
	if (pixels[0] != 1U || nimera_gui_text_width("Nimera") != 60U) return 0;
	nimera_gui_draw_text(&canvas, 1, 1, "Привет", 2U);
	event.type = NIMERA_WINDOW_POINTER_MOVE; event.x = 10ULL; event.y = 10ULL;
	(void)nimera_gui_button_handle_event(&button, &event, &event.reserved);
	if (button.hovered == 0U) return 0;
	event.type = NIMERA_WINDOW_POINTER_BUTTON_DOWN; event.button = 1U;
	(void)nimera_gui_button_handle_event(&button, &event, &event.reserved);
	if (button.pressed == 0U) return 0;
	event.type = NIMERA_WINDOW_POINTER_BUTTON_UP; event.x = 20ULL; event.y = 20ULL;
	(void)nimera_gui_button_handle_event(&button, &event, &event.reserved);
	if (event.reserved == 0U) return 0;
	event.type = NIMERA_WINDOW_POINTER_BUTTON_DOWN; event.x = 10ULL; event.y = 10ULL;
	(void)nimera_gui_button_handle_event(&button, &event, &event.reserved);
	event.type = NIMERA_WINDOW_POINTER_BUTTON_UP; event.x = 60ULL; event.y = 30ULL;
	(void)nimera_gui_button_handle_event(&button, &event, &event.reserved);
	if (event.reserved != 0U || button.pressed != 0U) return 0;
	nimera_gui_canvas_init(&window.canvas, pixels, 64ULL, 40ULL, 64ULL);
	event.type = NIMERA_WINDOW_EVENT_RESIZED;
	event.client_address = (u64)(unsigned long)&pixels[1];
	event.client_width = 32ULL; event.client_height = 24ULL; event.stride_pixels = 40ULL;
	if (nimera_gui_window_apply_event(&window, &event) == 0 ||
		window.canvas.pixels != (u32 *)(unsigned long)event.client_address ||
		window.canvas.width != 32ULL || window.canvas.height != 24ULL || window.canvas.stride != 40ULL) return 0;
	nimera_gui_damage_reset(&damage);
	nimera_gui_damage_add(&damage, (struct nimera_gui_rect){2, 3, 4ULL, 5ULL});
	nimera_gui_damage_add(&damage, (struct nimera_gui_rect){8, 7, 4ULL, 5ULL});
	return damage.dirty != 0U && damage.rect.x == 2 && damage.rect.y == 3 && damage.rect.width == 10ULL && damage.rect.height == 9ULL;
}
