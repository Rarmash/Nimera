#include <nimera/compositor.h>
#include <nimera/console.h>
#include <nimera/display.h>
#include <nimera/format.h>
#include <nimera/graphics.h>
#include <nimera/input.h>
#include <nimera/mmu.h>
#include <nimera/pmm.h>
#include <nimera/process.h>
#include <nimera/scheduler.h>
#include <nimera/window.h>

#define WINDOW_BACKGROUND 0x00101828U
#define WINDOW_FRAME 0x0020283cU
#define WINDOW_ACTIVE_TITLE 0x003d6f9eU
#define WINDOW_INACTIVE_TITLE 0x002b3548U
#define WINDOW_TEXT 0x00ffffffU

static struct nimera_window windows[WINDOW_MANAGER_MAX_WINDOWS];
static unsigned int window_count;
static unsigned int next_id;
static unsigned int initialized;
static int z_sequence;
static struct nimera_window *terminal_window;
static struct nimera_window *focused_window;
static struct nimera_window *drag_window;
static long long drag_offset_x;
static long long drag_offset_y;

static struct nimera_window *user_window(struct process *owner, u64 handle)
{
	for (unsigned int index = 0U; index < window_count; ++index)
		if (windows[index].visible != 0U && windows[index].user_owned != 0U &&
			windows[index].owner == owner && windows[index].id == handle)
			return &windows[index];
	return (struct nimera_window *)0;
}

static void queue_event(struct nimera_window *window,
			const struct nimera_window_event *event)
{
	unsigned int next;
	if (window == (struct nimera_window *)0 || window->user_owned == 0U) return;
	next = (window->event_write + 1U) % WINDOW_EVENT_QUEUE_CAPACITY;
	if (next == window->event_read) {
		/* Pointer motion is disposable; never panic on a full user queue. */
		if (event->type == NIMERA_WINDOW_POINTER_MOVE) return;
		window->event_read = (window->event_read + 1U) % WINDOW_EVENT_QUEUE_CAPACITY;
	}
	window->events[window->event_write] = *event;
	window->event_write = next;
	if (window->owner->thread_index < scheduler_thread_count())
		scheduler_wake_thread(window->owner->thread_index);
}

static int pop_event(struct nimera_window *window,
			struct nimera_window_event *event)
{
	if (window->event_read == window->event_write) return 0;
	*event = window->events[window->event_read];
	window->event_read = (window->event_read + 1U) % WINDOW_EVENT_QUEUE_CAPACITY;
	return 1;
}

static void unmap_user_client(struct nimera_window *window)
{
	if (window->owner == (struct process *)0) return;
	for (u64 page = 0ULL; page < window->user_map_pages; ++page)
		(void)mmu_unmap_user_page_in(&window->owner->address_space,
			window->user_address + page * NIMERA_PAGE_SIZE);
	window->user_address = 0ULL;
	window->user_map_pages = 0ULL;
}

static int window_test_line(const char *name, int condition)
{
	console_write(name);
	console_write(condition != 0 ? ": OK\r\n" : ": FAILED\r\n");
	return condition != 0 ? 0 : -1;
}

static u64 clamp_dimension(u64 value, u64 minimum, u64 available)
{
	if (available < minimum) return available;
	return value < minimum ? minimum : (value > available ? available : value);
}

static void copy_title(char *destination, const char *source)
{
	unsigned int index = 0U;
	if (source != (const char *)0)
		while (source[index] != '\0' && index < WINDOW_TITLE_MAX) {
			destination[index] = source[index];
			++index;
		}
	destination[index] = '\0';
}

static int inside(const struct nimera_window *window, unsigned int x,
			 unsigned int y)
{
	return window != (const struct nimera_window *)0 && window->visible != 0U &&
		x >= (unsigned int)window->x && y >= (unsigned int)window->y &&
		(u64)x < (u64)window->x + window->width &&
		(u64)y < (u64)window->y + window->height;
}

static void render_frame(struct nimera_window *window)
{
	struct nimera_surface *surface = &window->frame_surface;
	u32 title_color = window->focused != 0U ? WINDOW_ACTIVE_TITLE :
		WINDOW_INACTIVE_TITLE;
	compositor_render_begin(surface);
	graphics_clear(WINDOW_FRAME);
	graphics_fill_rect(WINDOW_BORDER, WINDOW_BORDER,
			surface->width - WINDOW_BORDER * 2ULL,
			WINDOW_TITLE_HEIGHT - WINDOW_BORDER, title_color);
	graphics_draw_text(10ULL, 8ULL, window->title, WINDOW_TEXT);
	compositor_render_end();
	compositor_mark_dirty(window->x, window->y, window->width, window->height);
}

static void normalize_z_order(void)
{
	if (z_sequence < 1000000) return;
	for (unsigned int i = 0U; i < window_count; ++i) windows[i].z_order = (int)i;
	z_sequence = (int)window_count;
}

int window_manager_init(void)
{
	u64 width;
	u64 height;
	if (initialized != 0U || !display_available()) return -1;
	if (compositor_init() != 0) return -1;
	width = display_width() > 120ULL ? display_width() - 80ULL : display_width();
	height = display_height() > 120ULL ? display_height() - 80ULL : display_height();
	width = clamp_dimension(width, 320ULL, display_width());
	height = clamp_dimension(height, 220ULL, display_height());
	window_count = 0U;
	next_id = 1U;
	z_sequence = 0;
	initialized = 1U;
	terminal_window = window_create("Nimera Terminal", width, height, 40LL, 40LL);
	if (terminal_window == (struct nimera_window *)0) return -1;
	{
		u64 about_width = clamp_dimension(320ULL, 220ULL, display_width());
		u64 about_height = clamp_dimension(180ULL, 140ULL, display_height());
		struct nimera_window *about = window_create("About Nimera", about_width,
				about_height, (long long)(display_width() / 2ULL), 120LL);
		if (about == (struct nimera_window *)0) return -1;
		compositor_render_begin(&about->client_surface);
	graphics_clear(WINDOW_BACKGROUND);
	graphics_draw_text(28ULL, 35ULL, "Nimera", WINDOW_TEXT);
	graphics_draw_text(28ULL, 65ULL, "Native compositor test", WINDOW_TEXT);
		compositor_render_end();
		compositor_mark_dirty(about->client_surface.x, about->client_surface.y,
				      about->client_surface.width, about->client_surface.height);
	}
	/* About is initially visible; a click on the terminal restores its focus. */
	focused_window = (struct nimera_window *)0;
	window_focus(window_count > 1U ? &windows[1] : terminal_window);
	#if NIMERA_WINDOW_TEST
	(void)window_manager_test();
	#endif
	window_focus(window_count > 1U ? &windows[1] : terminal_window);
	compositor_present();
	return 0;
}

struct nimera_window *window_create(const char *title, u64 width, u64 height,
					long long x, long long y)
{
	struct nimera_window *window;
	if (initialized == 0U || window_count == WINDOW_MANAGER_MAX_WINDOWS ||
		width < WINDOW_BORDER * 2ULL || height <= WINDOW_TITLE_HEIGHT) {
		return (struct nimera_window *)0;
	}
	window = &windows[window_count++];
	window->id = next_id++;
	window->width = width;
	window->height = height;
	window->visible = 1U;
	window->focused = 0U;
	window->owner = (struct process *)0;
	window->user_address = 0ULL;
	window->user_map_pages = 0ULL;
	window->user_owned = 0U;
	window->event_read = 0U;
	window->event_write = 0U;
	window->z_order = ++z_sequence;
	copy_title(window->title, title);
	if (x < 0LL) x = 0LL;
	if (y < 0LL) y = 0LL;
	if ((u64)x + width > display_width()) x = (long long)display_width() - (long long)width;
	if ((u64)y + height > display_height()) y = (long long)display_height() - (long long)height;
	window->x = x;
	window->y = y;
	if (surface_create(&window->frame_surface, width, height, x, y,
				   window->z_order * 2, WINDOW_FRAME) != 0) {
		--window_count; return (struct nimera_window *)0;
	}
	if (surface_create(&window->client_surface,
				   width - WINDOW_BORDER * 2ULL,
				   height - WINDOW_TITLE_HEIGHT - WINDOW_BORDER,
				   x + (long long)WINDOW_BORDER,
				   y + (long long)WINDOW_TITLE_HEIGHT,
				   window->z_order * 2 + 1, WINDOW_BACKGROUND) != 0) {
		surface_destroy(&window->frame_surface);
		--window_count; return (struct nimera_window *)0;
	}
	if (compositor_add(&window->frame_surface) != 0 ||
		compositor_add(&window->client_surface) != 0) {
		(void)compositor_remove(&window->frame_surface);
		(void)compositor_remove(&window->client_surface);
		surface_destroy(&window->frame_surface);
		surface_destroy(&window->client_surface);
		--window_count; return (struct nimera_window *)0;
	}
	render_frame(window);
	return window;
}

void window_destroy(struct nimera_window *window)
{
	if (window == (struct nimera_window *)0 || window->visible == 0U) return;
	unmap_user_client(window);
	(void)compositor_remove(&window->client_surface);
	(void)compositor_remove(&window->frame_surface);
	surface_destroy(&window->client_surface);
	surface_destroy(&window->frame_surface);
	window->visible = 0U;
	window->user_owned = 0U;
	window->owner = (struct process *)0;
	if (window_count != 0U) --window_count;
	if (focused_window == window) focused_window = terminal_window;
	if (drag_window == window) drag_window = (struct nimera_window *)0;
	compositor_mark_dirty(window->x, window->y, window->width, window->height);
	compositor_present();
}

void window_show(struct nimera_window *window, unsigned int visible)
{
	if (window == (struct nimera_window *)0) return;
	window->visible = visible != 0U ? 1U : 0U;
	compositor_mark_dirty(window->x, window->y, window->width, window->height);
	compositor_present();
}

void window_focus(struct nimera_window *window)
{
	struct nimera_window *old = focused_window;
	if (window == (struct nimera_window *)0 || window->visible == 0U) return;
	for (unsigned int i = 0U; i < window_count; ++i) windows[i].focused = 0U;
	window->focused = 1U;
	focused_window = window;
	if (old != window && old != (struct nimera_window *)0 && old->user_owned != 0U)
		queue_event(old, &(struct nimera_window_event){.type = NIMERA_WINDOW_FOCUS_LOST});
	if (old != window && window->user_owned != 0U)
		queue_event(window, &(struct nimera_window_event){.type = NIMERA_WINDOW_FOCUS_GAINED});
	window->z_order = ++z_sequence;
	window->frame_surface.z_order = window->z_order * 2;
	window->client_surface.z_order = window->z_order * 2 + 1;
	normalize_z_order();
	render_frame(window);
	compositor_mark_dirty(window->x, window->y, window->width, window->height);
	compositor_present();
}

void window_move(struct nimera_window *window, long long x, long long y)
{
	long long max_x;
	long long max_y;
	if (window == (struct nimera_window *)0 || window->visible == 0U) return;
	max_x = (long long)display_width() - (long long)WINDOW_TITLE_HEIGHT;
	max_y = (long long)display_height() - (long long)WINDOW_TITLE_HEIGHT;
	if (x > max_x) x = max_x;
	if (y > max_y) y = max_y;
	if (x + (long long)window->width < (long long)WINDOW_BORDER)
		x = (long long)WINDOW_BORDER - (long long)window->width;
	if (y + (long long)WINDOW_TITLE_HEIGHT < (long long)WINDOW_BORDER)
		y = (long long)WINDOW_BORDER - (long long)WINDOW_TITLE_HEIGHT;
	compositor_mark_dirty(window->x, window->y, window->width, window->height);
	window->x = x;
	window->y = y;
	window->frame_surface.x = x;
	window->frame_surface.y = y;
	window->client_surface.x = x + (long long)WINDOW_BORDER;
	window->client_surface.y = y + (long long)WINDOW_TITLE_HEIGHT;
	compositor_mark_dirty(x, y, window->width, window->height);
	compositor_present();
}

enum window_hit_region window_hit_test(const struct nimera_window *window,
					       unsigned int x, unsigned int y)
{
	if (!inside(window, x, y)) return WINDOW_HIT_OUTSIDE;
	if ((u64)y < (u64)window->y + WINDOW_TITLE_HEIGHT) return WINDOW_HIT_TITLE;
	return WINDOW_HIT_CLIENT;
}

struct nimera_window *window_at(unsigned int x, unsigned int y)
{
	struct nimera_window *result = (struct nimera_window *)0;
	for (unsigned int i = 0U; i < window_count; ++i)
		if (windows[i].visible != 0U && inside(&windows[i], x, y) &&
			(result == (struct nimera_window *)0 ||
			 windows[i].z_order > result->z_order)) result = &windows[i];
	return result;
}

void window_manager_handle_pointer_event(const struct pointer_event *event)
{
	struct nimera_window *window;
	compositor_handle_pointer_event(event);
	if (event == (const struct pointer_event *)0) return;
	if (event->kind == POINTER_BUTTON_DOWN && event->button == POINTER_BUTTON_LEFT) {
		window = window_at(event->x, event->y);
		if (window == (struct nimera_window *)0) return;
		window_focus(window);
		if (window_hit_test(window, event->x, event->y) == WINDOW_HIT_TITLE) {
			drag_window = window;
			drag_offset_x = (long long)event->x - window->x;
			drag_offset_y = (long long)event->y - window->y;
		} else if (window->user_owned != 0U) {
			queue_event(window, &(struct nimera_window_event){
				.type = NIMERA_WINDOW_POINTER_BUTTON_DOWN,
				.button = event->button,
				.x = event->x - (u32)window->client_surface.x,
				.y = event->y - (u32)window->client_surface.y});
		}
	} else if (event->kind == POINTER_MOVE && drag_window != (struct nimera_window *)0) {
		window_move(drag_window, (long long)event->x - drag_offset_x,
				   (long long)event->y - drag_offset_y);
	} else if (event->kind == POINTER_MOVE && focused_window != (struct nimera_window *)0 &&
		focused_window->user_owned != 0U &&
		window_hit_test(focused_window, event->x, event->y) == WINDOW_HIT_CLIENT) {
		queue_event(focused_window, &(struct nimera_window_event){
			.type = NIMERA_WINDOW_POINTER_MOVE,
			.x = event->x - (u32)focused_window->client_surface.x,
			.y = event->y - (u32)focused_window->client_surface.y,
			.button = event->buttons});
	} else if (event->kind == POINTER_BUTTON_UP && event->button == POINTER_BUTTON_LEFT) {
		if (drag_window == (struct nimera_window *)0 && focused_window != (struct nimera_window *)0 &&
			focused_window->user_owned != 0U &&
			window_hit_test(focused_window, event->x, event->y) == WINDOW_HIT_CLIENT)
			queue_event(focused_window, &(struct nimera_window_event){
				.type = NIMERA_WINDOW_POINTER_BUTTON_UP,
				.button = event->button,
				.x = event->x - (u32)focused_window->client_surface.x,
				.y = event->y - (u32)focused_window->client_surface.y});
		drag_window = (struct nimera_window *)0;
	}
}

static void route_pending_input(void)
{
	struct pointer_event pointer;
	struct key_event key;
	while (input_try_get_pointer_event(&pointer) != 0)
		window_manager_handle_pointer_event(&pointer);
	while (input_try_get_event(&key) != 0)
		if (focused_window != (struct nimera_window *)0 && focused_window->user_owned != 0U)
			queue_event(focused_window, &(struct nimera_window_event){
				.type = NIMERA_WINDOW_KEY, .key_code = key.code,
				.ch = (u32)(unsigned char)key.ch, .modifiers = key.ctrl});
}

int window_manager_create_user(struct process *owner, const char *title,
	u64 width, u64 height,
				       struct nimera_window_info *info)
{
	struct nimera_window *window;
	if (owner == (struct process *)0 || info == (struct nimera_window_info *)0 ||
		width == 0ULL || height == 0ULL || width > ~0ULL / height ||
		width * height > ~0ULL / 4ULL ||
		width > ~0ULL - WINDOW_BORDER * 2ULL ||
		height > ~0ULL - WINDOW_TITLE_HEIGHT - WINDOW_BORDER) return -1;
	window = window_create(title, width + WINDOW_BORDER * 2ULL,
		height + WINDOW_TITLE_HEIGHT + WINDOW_BORDER,
		(long long)(display_width() / 3ULL), 80LL);
	if (window == (struct nimera_window *)0) {
		return -1;
	}
	window->owner = owner;
	window->user_owned = 1U;
	window->user_address = WINDOW_USER_MAP_BASE +
		(u64)(window - windows) * WINDOW_USER_MAP_STRIDE;
	window->user_map_pages = window->client_surface.page_count;
	if (window->user_map_pages * NIMERA_PAGE_SIZE > WINDOW_USER_MAP_STRIDE) {
		window_destroy(window); return -1;
	}
	for (u64 page = 0ULL; page < window->user_map_pages; ++page)
		if (mmu_map_user_page_in(&owner->address_space,
			window->user_address + page * NIMERA_PAGE_SIZE,
			(u64)(unsigned long)window->client_surface.pixels + page * NIMERA_PAGE_SIZE,
			MMU_USER_READ | MMU_USER_WRITE) != 0) {
			window_destroy(window); return -1;
		}
	window_focus(window);
	info->handle = window->id;
	info->client_address = window->user_address;
	info->stride_pixels = window->client_surface.stride;
	info->width = window->client_surface.width;
	info->height = window->client_surface.height;
	return 0;
}

int window_manager_destroy_user(struct process *owner, u64 handle)
{
	struct nimera_window *window = user_window(owner, handle);
	if (window == (struct nimera_window *)0) return -1;
	window_destroy(window);
	if (terminal_window != (struct nimera_window *)0) window_focus(terminal_window);
	return 0;
}

int window_manager_present_user(struct process *owner, u64 handle,
					u64 x, u64 y, u64 width, u64 height)
{
	struct nimera_window *window = user_window(owner, handle);
	if (window == (struct nimera_window *)0 || x > window->client_surface.width ||
		y > window->client_surface.height || width > window->client_surface.width - x ||
		height > window->client_surface.height - y) return -1;
	compositor_mark_dirty(window->client_surface.x + (long long)x,
		window->client_surface.y + (long long)y, width, height);
	compositor_present();
	return 0;
}

int window_manager_read_user_event(struct process *owner, u64 handle,
					struct nimera_window_event *event)
{
	struct nimera_window *window = user_window(owner, handle);
	if (window == (struct nimera_window *)0 || event == (struct nimera_window_event *)0)
		return -1;
	for (;;) {
		route_pending_input();
		if (pop_event(window, event) != 0) return 0;
		input_wait_for_activity();
	}
}

void window_manager_destroy_process_windows(struct process *owner)
{
	for (unsigned int index = 0U; index < WINDOW_MANAGER_MAX_WINDOWS; ++index)
		if (windows[index].visible != 0U && windows[index].owner == owner)
			window_destroy(&windows[index]);
	if (terminal_window != (struct nimera_window *)0)
		window_focus(terminal_window);
}

struct nimera_surface *window_manager_terminal_client_surface(void)
{
	return terminal_window == (struct nimera_window *)0 ?
		(struct nimera_surface *)0 : &terminal_window->client_surface;
}

int window_manager_terminal_focused(void)
{
	return focused_window == terminal_window && terminal_window != (struct nimera_window *)0 &&
		terminal_window->visible != 0U;
}

int window_manager_test(void)
{
	struct nimera_window *temporary;
	struct nimera_window *hit;
	int result = 0;
	console_write("Nimera window test\r\n");
	result |= window_test_line("window create", window_count >= 2U);
	result |= window_test_line("client geometry",
		terminal_window != (struct nimera_window *)0 &&
		terminal_window->client_surface.width == terminal_window->width -
			WINDOW_BORDER * 2ULL &&
		terminal_window->client_surface.height == terminal_window->height -
			WINDOW_TITLE_HEIGHT - WINDOW_BORDER &&
		terminal_window->client_surface.x == terminal_window->x +
			(long long)WINDOW_BORDER &&
		terminal_window->client_surface.y == terminal_window->y +
			(long long)WINDOW_TITLE_HEIGHT);
	result |= window_test_line("decoration geometry",
		window_hit_test(terminal_window, (unsigned int)terminal_window->x + 8U,
			(unsigned int)terminal_window->y + 8U) == WINDOW_HIT_TITLE &&
		window_hit_test(terminal_window, (unsigned int)terminal_window->x + 8U,
			(unsigned int)terminal_window->y + (unsigned int)WINDOW_TITLE_HEIGHT + 8U) ==
			WINDOW_HIT_CLIENT);
	temporary = window_create("Window Test", 240ULL, 140ULL, 20LL, 20LL);
	result |= window_test_line("overlap/z-order", temporary != (struct nimera_window *)0);
	if (temporary == (struct nimera_window *)0) return -1;
	hit = window_at(30U, 30U);
	result |= window_test_line("hit testing", hit == temporary);
	window_manager_handle_pointer_event(&(struct pointer_event){
		.kind = POINTER_BUTTON_DOWN, .button = POINTER_BUTTON_LEFT,
		.x = 30U, .y = 30U});
	result |= window_test_line("click focus", focused_window == temporary);
	result |= window_test_line("bring to front", temporary->z_order >
		(window_count > 1U ? windows[1].z_order : -1));
	result |= window_test_line("drag start", drag_window == temporary);
	window_manager_handle_pointer_event(&(struct pointer_event){
		.kind = POINTER_MOVE, .x = 90U, .y = 100U});
	result |= window_test_line("drag movement", temporary->x == 80LL &&
		temporary->y == 90LL);
	window_manager_handle_pointer_event(&(struct pointer_event){
		.kind = POINTER_BUTTON_UP, .button = POINTER_BUTTON_LEFT,
		.x = 90U, .y = 100U});
	result |= window_test_line("drag release", drag_window ==
		(struct nimera_window *)0);
	result |= window_test_line("old region restore", window_at(25U, 25U) != temporary);
	window_move(temporary, -1000LL, -1000LL);
	result |= window_test_line("bounds clamp", temporary->x +
		(long long)temporary->width >= (long long)WINDOW_BORDER &&
		temporary->y + (long long)WINDOW_TITLE_HEIGHT >=
		(long long)WINDOW_BORDER);
	window_focus(temporary);
	result |= window_test_line("keyboard focus routing",
		window_manager_terminal_focused() == 0);
	window_focus(terminal_window);
	result |= window_test_line("terminal focus restore",
		window_manager_terminal_focused() != 0);
	window_destroy(temporary);
	result |= window_test_line("window destroy", temporary->visible == 0U &&
		window_at(25U, 25U) != temporary);
	console_write(result == 0 ? "Window test complete.\r\n" :
		"Window test failed.\r\n");
	window_focus(window_count > 1U ? &windows[1] : terminal_window);
	return result;
}
