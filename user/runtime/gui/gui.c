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

static int text_decode(const char *text, u64 length, u64 offset, u32 *codepoint,
	u64 *encoded_length)
{
	unsigned char first;
	u32 value;
	unsigned int need;
	if (offset >= length) return 0;
	first = (unsigned char)text[offset];
	if (first < 0x80U) { *codepoint = first; *encoded_length = 1ULL; return 1; }
	if ((first & 0xe0U) == 0xc0U) { value = first & 0x1fU; need = 1U; }
	else if ((first & 0xf0U) == 0xe0U) { value = first & 0x0fU; need = 2U; }
	else if ((first & 0xf8U) == 0xf0U) { value = first & 7U; need = 3U; }
	else return 0;
	if (offset + (u64)need >= length) return 0;
	for (unsigned int i = 0U; i < need; ++i) {
		unsigned char continuation = (unsigned char)text[offset + 1ULL + (u64)i];
		if ((continuation & 0xc0U) != 0x80U) return 0;
		value = (value << 6) | (u32)(continuation & 0x3fU);
	}
	if ((need == 1U && value < 0x80U) || (need == 2U && value < 0x800U) ||
		(need == 3U && value < 0x10000U) || value > 0x10ffffU ||
		(value >= 0xd800U && value <= 0xdfffU)) return 0;
	*codepoint = value;
	*encoded_length = (u64)need + 1ULL;
	return 1;
}

static int text_valid(const char *text, u64 length)
{
	u64 offset = 0ULL;
	u32 codepoint;
	u64 encoded_length;
	while (offset < length) {
		if (text_decode(text, length, offset, &codepoint, &encoded_length) == 0) return 0;
		offset += encoded_length;
	}
	return 1;
}

static u64 text_previous_boundary(const char *text, u64 length, u64 offset)
{
	(void)length;
	if (offset == 0ULL) return 0ULL;
	--offset;
	while (offset > 0ULL && (((unsigned char)text[offset] & 0xc0U) == 0x80U)) --offset;
	return offset;
}

static u64 text_next_boundary(const char *text, u64 length, u64 offset)
{
	u32 codepoint;
	u64 encoded_length;
	if (offset >= length || text_decode(text, length, offset, &codepoint, &encoded_length) == 0)
		return length;
	return offset + encoded_length;
}

static unsigned int text_codepoint_count(const char *text, u64 length)
{
	u64 offset = 0ULL;
	unsigned int count = 0U;
	u32 codepoint;
	u64 encoded_length;
	while (offset < length && text_decode(text, length, offset, &codepoint, &encoded_length) != 0) {
		offset += encoded_length;
		++count;
	}
	return count;
}

static u64 text_prefix_pixels(const struct nimera_gui_text_field *field)
{
	return (u64)text_codepoint_count(field->text, field->cursor) * 10ULL;
}

static void text_field_scroll_into_view(struct nimera_gui_text_field *field)
{
	u64 inner_width = field->rect.width > 8ULL ? field->rect.width - 8ULL : 0ULL;
	u64 cursor_pixels = text_prefix_pixels(field);
	u64 text_pixels = (u64)text_codepoint_count(field->text, field->length) * 10ULL;
	if (cursor_pixels < field->scroll_x) field->scroll_x = cursor_pixels;
	if (inner_width > 2ULL && cursor_pixels > field->scroll_x + inner_width - 2ULL)
		field->scroll_x = cursor_pixels - inner_width + 2ULL;
	if (field->scroll_x > text_pixels) field->scroll_x = text_pixels;
}

static int text_encode(u32 codepoint, char encoded[4], u64 *length)
{
	if (codepoint <= 0x7fU) { encoded[0] = (char)codepoint; *length = 1ULL; return 1; }
	if (codepoint <= 0x7ffU) {
		encoded[0] = (char)(0xc0U | (codepoint >> 6)); encoded[1] = (char)(0x80U | (codepoint & 0x3fU));
		*length = 2ULL; return 1;
	}
	if (codepoint >= 0xd800U && codepoint <= 0xdfffU) return 0;
	if (codepoint <= 0xffffU) {
		encoded[0] = (char)(0xe0U | (codepoint >> 12)); encoded[1] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
		encoded[2] = (char)(0x80U | (codepoint & 0x3fU)); *length = 3ULL; return 1;
	}
	if (codepoint > 0x10ffffU) return 0;
	encoded[0] = (char)(0xf0U | (codepoint >> 18)); encoded[1] = (char)(0x80U | ((codepoint >> 12) & 0x3fU));
	encoded[2] = (char)(0x80U | ((codepoint >> 6) & 0x3fU)); encoded[3] = (char)(0x80U | (codepoint & 0x3fU));
	*length = 4ULL; return 1;
}

void nimera_gui_focus_init(struct nimera_gui_focus *focus)
{ focus->focused_widget = (void *)0; }

void nimera_gui_focus_clear(struct nimera_gui_focus *focus)
{ focus->focused_widget = (void *)0; }

int nimera_gui_text_field_init(struct nimera_gui_text_field *field,
	struct nimera_gui_rect rect, char *buffer, u64 capacity, const char *initial_text)
{
	u64 length = 0ULL;
	while (initial_text[length] != '\0') ++length;
	if (buffer == (char *)0 || capacity == 0ULL || length >= capacity ||
		text_valid(initial_text, length) == 0) return -1;
	for (u64 i = 0ULL; i < length; ++i) buffer[i] = initial_text[i];
	buffer[length] = '\0';
	field->rect = rect; field->text = buffer; field->capacity = capacity;
	field->length = length; field->cursor = length; field->scroll_x = 0ULL;
	field->focused = 0U; field->hovered = 0U; field->pressed = 0U;
	return 0;
}

void nimera_gui_text_field_set_focus(struct nimera_gui_text_field *field,
	struct nimera_gui_focus *focus, unsigned int focused)
{
	if (focused != 0U) {
		if (focus->focused_widget != (void *)0 && focus->focused_widget != field)
			((struct nimera_gui_text_field *)focus->focused_widget)->focused = 0U;
		focus->focused_widget = field; field->focused = 1U;
	} else {
		if (focus->focused_widget == field) focus->focused_widget = (void *)0;
		field->focused = 0U;
	}
}

static enum nimera_gui_text_field_result text_field_insert(
	struct nimera_gui_text_field *field, u32 codepoint)
{
	char encoded[4];
	u64 encoded_length;
	if (text_encode(codepoint, encoded, &encoded_length) == 0 ||
		field->length + encoded_length >= field->capacity) return NIMERA_GUI_TEXT_NONE;
	for (u64 i = field->length; i > field->cursor; --i)
		field->text[i + encoded_length - 1ULL] = field->text[i - 1ULL];
	for (u64 i = 0ULL; i < encoded_length; ++i) field->text[field->cursor + i] = encoded[i];
	field->length += encoded_length; field->cursor += encoded_length; field->text[field->length] = '\0';
	text_field_scroll_into_view(field);
	return NIMERA_GUI_TEXT_CHANGED;
}

int nimera_gui_text_field_handle_event(struct nimera_gui_text_field *field,
	struct nimera_gui_focus *focus, const struct nimera_window_event *event,
	enum nimera_gui_text_field_result *result)
{
	unsigned int old_hovered = field->hovered;
	*result = NIMERA_GUI_TEXT_NONE;
	if (event->type == NIMERA_WINDOW_POINTER_MOVE) {
		field->hovered = nimera_gui_rect_contains(field->rect, (long long)event->x, (long long)event->y) ? 1U : 0U;
	} else if (event->type == NIMERA_WINDOW_POINTER_BUTTON_DOWN && event->button == 1U &&
		nimera_gui_rect_contains(field->rect, (long long)event->x, (long long)event->y)) {
		nimera_gui_text_field_set_focus(field, focus, 1U); field->pressed = 1U;
		{
			long long local = (long long)event->x - field->rect.x - 4LL + (long long)field->scroll_x;
			u64 count = (u64)text_codepoint_count(field->text, field->length);
			u64 selected = local <= 0LL ? 0ULL : (u64)((local + 5LL) / 10LL);
			if (selected > count) selected = count;
			field->cursor = 0ULL;
			for (u64 i = 0ULL; i < selected; ++i) field->cursor = text_next_boundary(field->text, field->length, field->cursor);
			text_field_scroll_into_view(field);
		}
	} else if (event->type == NIMERA_WINDOW_POINTER_BUTTON_UP && event->button == 1U) {
		field->pressed = 0U;
	} else if (event->type == NIMERA_WINDOW_KEY && field->focused != 0U) {
		if (event->key_code == NIMERA_KEY_CHAR && (event->modifiers & NIMERA_KEY_MOD_CTRL) == 0U &&
			event->ch >= 0x20U && event->ch != 0x7fU) *result = text_field_insert(field, event->ch);
		else if (event->key_code == NIMERA_KEY_BACKSPACE && field->cursor != 0ULL) {
			u64 previous = text_previous_boundary(field->text, field->length, field->cursor);
			for (u64 i = previous; i <= field->length - (field->cursor - previous); ++i)
				field->text[i] = field->text[i + (field->cursor - previous)];
			field->length -= field->cursor - previous; field->cursor = previous; *result = NIMERA_GUI_TEXT_CHANGED;
		} else if (event->key_code == NIMERA_KEY_DELETE && field->cursor < field->length) {
			u64 next = text_next_boundary(field->text, field->length, field->cursor);
			for (u64 i = field->cursor; i <= field->length - (next - field->cursor); ++i)
				field->text[i] = field->text[i + (next - field->cursor)];
			field->length -= next - field->cursor; *result = NIMERA_GUI_TEXT_CHANGED;
		} else if (event->key_code == NIMERA_KEY_LEFT) field->cursor = text_previous_boundary(field->text, field->length, field->cursor);
		else if (event->key_code == NIMERA_KEY_RIGHT) field->cursor = text_next_boundary(field->text, field->length, field->cursor);
		else if (event->key_code == NIMERA_KEY_HOME) field->cursor = 0ULL;
		else if (event->key_code == NIMERA_KEY_END) field->cursor = field->length;
		else if (event->key_code == NIMERA_KEY_ENTER) *result = NIMERA_GUI_TEXT_SUBMIT;
		text_field_scroll_into_view(field);
	}
	return old_hovered != field->hovered || field->pressed != 0U || *result != NIMERA_GUI_TEXT_NONE;
}

void nimera_gui_text_field_draw(struct nimera_gui_canvas *canvas,
	const struct nimera_gui_text_field *field)
{
	struct nimera_gui_rect old_clip = canvas->clip;
	u32 background = field->focused != 0U ? NIMERA_GUI_COLOR_PANEL : NIMERA_GUI_COLOR_BUTTON;
	nimera_gui_fill_rect(canvas, field->rect, background);
	nimera_gui_draw_rect(canvas, field->rect, field->focused != 0U ? NIMERA_GUI_COLOR_ACCENT : NIMERA_GUI_COLOR_BORDER);
	nimera_gui_canvas_set_clip(canvas, (struct nimera_gui_rect){field->rect.x + 4LL, field->rect.y + 3LL,
		field->rect.width > 8ULL ? field->rect.width - 8ULL : 0ULL, field->rect.height > 6ULL ? field->rect.height - 6ULL : 0ULL});
	nimera_gui_draw_text(canvas, field->rect.x + 4LL - (long long)field->scroll_x,
		field->rect.y + 7LL, field->text, NIMERA_GUI_COLOR_TEXT);
	if (field->focused != 0U) nimera_gui_fill_rect(canvas, (struct nimera_gui_rect){field->rect.x + 4LL + (long long)text_prefix_pixels(field) - (long long)field->scroll_x,
		field->rect.y + 4LL, 2ULL, field->rect.height > 8ULL ? field->rect.height - 8ULL : 0ULL}, NIMERA_GUI_COLOR_ACCENT);
	canvas->clip = old_clip;
}

int nimera_gui_text_field_self_test(void)
{
	char buffer[32], small_buffer[8], long_buffer[64];
	struct nimera_gui_text_field field;
	struct nimera_gui_text_field other;
	struct nimera_gui_text_field small;
	struct nimera_gui_text_field long_field;
	struct nimera_gui_focus focus;
	struct nimera_window_event event = {0};
	enum nimera_gui_text_field_result result;
	if (nimera_gui_text_field_init(&field, (struct nimera_gui_rect){2, 2, 80ULL, 24ULL}, buffer, sizeof(buffer), "Привет") != 0) return 0;
	if (nimera_gui_text_field_init(&other, (struct nimera_gui_rect){2, 2, 80ULL, 24ULL}, small_buffer, sizeof(small_buffer), "") != 0) return 0;
	nimera_gui_focus_init(&focus); nimera_gui_text_field_set_focus(&field, &focus, 1U);
	if (field.focused == 0U || focus.focused_widget != &field) return 0;
	nimera_gui_text_field_set_focus(&other, &focus, 1U);
	if (field.focused != 0U || other.focused == 0U) return 0;
	nimera_gui_text_field_set_focus(&field, &focus, 1U);
	field.cursor = 0ULL; event.type = NIMERA_WINDOW_KEY; event.key_code = NIMERA_KEY_RIGHT;
	(void)nimera_gui_text_field_handle_event(&field, &focus, &event, &result);
	if (field.cursor != 2ULL) return 0;
	event.key_code = NIMERA_KEY_BACKSPACE; (void)nimera_gui_text_field_handle_event(&field, &focus, &event, &result);
	if (field.length != 10ULL) return 0;
	event.key_code = NIMERA_KEY_DELETE; (void)nimera_gui_text_field_handle_event(&field, &focus, &event, &result);
	if (field.length != 8ULL) return 0;
	event.key_code = NIMERA_KEY_END; (void)nimera_gui_text_field_handle_event(&field, &focus, &event, &result);
	event.key_code = NIMERA_KEY_CHAR; event.ch = 'X'; (void)nimera_gui_text_field_handle_event(&field, &focus, &event, &result);
	if (field.text[field.length - 1ULL] != 'X') return 0;
	event.type = NIMERA_WINDOW_POINTER_BUTTON_DOWN; event.button = 1U; event.x = 26ULL; event.y = 10ULL;
	(void)nimera_gui_text_field_handle_event(&field, &focus, &event, &result);
	if (field.cursor != 4ULL) return 0;
	if (nimera_gui_text_field_init(&small, (struct nimera_gui_rect){2, 2, 40ULL, 24ULL}, small_buffer, 5ULL, "1234") != 0) return 0;
	nimera_gui_text_field_set_focus(&small, &focus, 1U); event.type = NIMERA_WINDOW_KEY; event.key_code = NIMERA_KEY_CHAR; event.ch = 'x';
	(void)nimera_gui_text_field_handle_event(&small, &focus, &event, &result);
	if (small.length != 4ULL || small.text[4] != '\0') return 0;
	if (nimera_gui_text_field_init(&long_field, (struct nimera_gui_rect){2, 2, 30ULL, 24ULL}, long_buffer, sizeof(long_buffer), "") != 0) return 0;
	nimera_gui_text_field_set_focus(&long_field, &focus, 1U);
	for (unsigned int i = 0U; i < 8U; ++i) {
		event.ch = 'a'; (void)nimera_gui_text_field_handle_event(&long_field, &focus, &event, &result);
	}
	if (long_field.scroll_x == 0ULL) return 0;
	nimera_gui_text_field_set_focus(&field, &focus, 1U);
	event.key_code = NIMERA_KEY_ENTER; (void)nimera_gui_text_field_handle_event(&field, &focus, &event, &result);
	if (result != NIMERA_GUI_TEXT_SUBMIT) return 0;
	field.rect.width = 120ULL;
	return field.cursor == 4ULL && field.length == 9ULL;
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
	if (damage.dirty == 0U || damage.rect.x != 2 || damage.rect.y != 3 ||
		damage.rect.width != 10ULL || damage.rect.height != 9ULL) return 0;
	return nimera_gui_text_field_self_test() != 0;
}
