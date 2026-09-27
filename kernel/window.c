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
#define WINDOW_CLOSE_NORMAL 0x00d85c5cU
#define WINDOW_CLOSE_PRESSED 0x00ffffffU

static struct nimera_window windows[WINDOW_MANAGER_MAX_WINDOWS];
static unsigned int window_count;
static unsigned int next_id;
static unsigned int initialized;
static int z_sequence;
static struct nimera_window *terminal_window;
static struct nimera_window *focused_window;
static struct nimera_window *drag_window;
static struct nimera_window *close_window;
static struct nimera_window *resize_window;
static long long drag_offset_x;
static long long drag_offset_y;
static unsigned int resize_edges;
static unsigned int resize_start_x;
static unsigned int resize_start_y;
static long long resize_initial_x;
static long long resize_initial_y;
static u64 resize_initial_width;
static u64 resize_initial_height;
static long long resize_candidate_x;
static long long resize_candidate_y;
static u64 resize_candidate_width;
static u64 resize_candidate_height;

#define RESIZE_EDGE_LEFT 1U
#define RESIZE_EDGE_RIGHT 2U
#define RESIZE_EDGE_TOP 4U
#define RESIZE_EDGE_BOTTOM 8U

#if NIMERA_WINDOW_RESIZE_TEST
static unsigned int resize_fail_next;
#endif

static struct nimera_window *user_window(struct process *owner, u64 handle)
{
	for (unsigned int index = 0U; index < WINDOW_MANAGER_MAX_WINDOWS; ++index)
		if (windows[index].visible != 0U && windows[index].user_owned != 0U &&
			windows[index].owner == owner && windows[index].id == handle)
			return &windows[index];
	return (struct nimera_window *)0;
}

static u64 user_window_address(struct process *owner)
{
	unsigned int ordinal = 0U;
	for (unsigned int index = 0U; index < WINDOW_MANAGER_MAX_WINDOWS; ++index)
		if (windows[index].visible != 0U && windows[index].user_owned != 0U &&
			windows[index].owner == owner)
			++ordinal;
	return WINDOW_USER_MAP_BASE + (u64)ordinal * WINDOW_USER_MAP_STRIDE;
}

static u64 user_resize_address(const struct nimera_window *window)
{
	unsigned int slot = (unsigned int)(window - windows);
	return WINDOW_USER_RESIZE_MAP_BASE + (u64)slot * WINDOW_USER_MAP_STRIDE;
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
		/* Critical focus/button/close transitions evict the oldest event. */
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

static void unmap_mapping(struct process *owner, u64 address, u64 pages)
{
	if (owner == (struct process *)0) return;
	for (u64 page = 0ULL; page < pages; ++page)
		(void)mmu_unmap_user_page_in(&owner->address_space,
			address + page * NIMERA_PAGE_SIZE);
}

static unsigned int resize_edges_at(const struct nimera_window *window,
	unsigned int x, unsigned int y)
{
	unsigned int edges = 0U;
	u64 left = (u64)window->x;
	u64 top = (u64)window->y;
	u64 right = left + window->width;
	u64 bottom = top + window->height;
	if ((window->flags & NIMERA_WINDOW_RESIZABLE) == 0U) return 0U;
	if ((u64)x < left + WINDOW_RESIZE_BORDER) edges |= RESIZE_EDGE_LEFT;
	if ((u64)x >= right - WINDOW_RESIZE_BORDER) edges |= RESIZE_EDGE_RIGHT;
	if ((u64)y < top + WINDOW_RESIZE_BORDER) edges |= RESIZE_EDGE_TOP;
	if ((u64)y >= bottom - WINDOW_RESIZE_BORDER) edges |= RESIZE_EDGE_BOTTOM;
	return edges;
}

static enum window_hit_region resize_region(unsigned int edges)
{
	switch (edges) {
	case RESIZE_EDGE_LEFT: return WINDOW_HIT_RESIZE_LEFT;
	case RESIZE_EDGE_RIGHT: return WINDOW_HIT_RESIZE_RIGHT;
	case RESIZE_EDGE_TOP: return WINDOW_HIT_RESIZE_TOP;
	case RESIZE_EDGE_BOTTOM: return WINDOW_HIT_RESIZE_BOTTOM;
	case RESIZE_EDGE_LEFT | RESIZE_EDGE_TOP: return WINDOW_HIT_RESIZE_TOP_LEFT;
	case RESIZE_EDGE_RIGHT | RESIZE_EDGE_TOP: return WINDOW_HIT_RESIZE_TOP_RIGHT;
	case RESIZE_EDGE_LEFT | RESIZE_EDGE_BOTTOM: return WINDOW_HIT_RESIZE_BOTTOM_LEFT;
	case RESIZE_EDGE_RIGHT | RESIZE_EDGE_BOTTOM: return WINDOW_HIT_RESIZE_BOTTOM_RIGHT;
	default: return WINDOW_HIT_BORDER;
	}
}

static void resize_update_candidate(unsigned int x, unsigned int y)
{
	long long dx = (long long)x - (long long)resize_start_x;
	long long dy = (long long)y - (long long)resize_start_y;
	u64 min_width = WINDOW_BORDER * 2ULL + WINDOW_MIN_CLIENT_WIDTH;
	u64 min_height = WINDOW_TITLE_HEIGHT + WINDOW_BORDER + WINDOW_MIN_CLIENT_HEIGHT;
	long long right = resize_initial_x + (long long)resize_initial_width;
	long long bottom = resize_initial_y + (long long)resize_initial_height;
	long long candidate_x = resize_initial_x;
	long long candidate_y = resize_initial_y;
	u64 candidate_width = resize_initial_width;
	u64 candidate_height = resize_initial_height;
	if ((resize_edges & RESIZE_EDGE_LEFT) != 0U) {
		if (dx >= 0LL && (u64)dx >= resize_initial_width - min_width) {
			candidate_x = right - (long long)min_width;
			candidate_width = min_width;
		} else {
			candidate_x = resize_initial_x + dx;
			candidate_width = dx < 0LL ? resize_initial_width + (u64)(-dx) :
				resize_initial_width - (u64)dx;
		}
	}
	if ((resize_edges & RESIZE_EDGE_RIGHT) != 0U) {
		if (dx >= 0LL) candidate_width = resize_initial_width + (u64)dx;
		else if ((u64)(-dx) >= resize_initial_width - min_width)
			candidate_width = min_width;
		else candidate_width = resize_initial_width - (u64)(-dx);
	}
	if ((resize_edges & RESIZE_EDGE_TOP) != 0U) {
		if (dy >= 0LL && (u64)dy >= resize_initial_height - min_height) {
			candidate_y = bottom - (long long)min_height;
			candidate_height = min_height;
		} else {
			candidate_y = resize_initial_y + dy;
			candidate_height = dy < 0LL ? resize_initial_height + (u64)(-dy) :
				resize_initial_height - (u64)dy;
		}
	}
	if ((resize_edges & RESIZE_EDGE_BOTTOM) != 0U) {
		if (dy >= 0LL) candidate_height = resize_initial_height + (u64)dy;
		else if ((u64)(-dy) >= resize_initial_height - min_height)
			candidate_height = min_height;
		else candidate_height = resize_initial_height - (u64)(-dy);
	}
	if (candidate_x < 0LL) {
		/* Keep the opposite edge fixed when the dragged left edge hits 0. */
		candidate_width = right > 0LL ? (u64)right : min_width;
		candidate_x = 0LL;
	}
	if (candidate_y < 0LL) {
		/* Keep the opposite edge fixed when the dragged top edge hits 0. */
		candidate_height = bottom > 0LL ? (u64)bottom : min_height;
		candidate_y = 0LL;
	}
	if ((u64)candidate_x + candidate_width > display_width())
		candidate_width = display_width() - (u64)candidate_x;
	if ((u64)candidate_y + candidate_height > display_height())
		candidate_height = display_height() - (u64)candidate_y;
	if (candidate_width < min_width) candidate_width = min_width;
	if (candidate_height < min_height) candidate_height = min_height;
	resize_candidate_x = candidate_x;
	resize_candidate_y = candidate_y;
	resize_candidate_width = candidate_width;
	resize_candidate_height = candidate_height;
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

static u64 close_box_x(const struct nimera_window *window)
{
	return window->width - WINDOW_BORDER - WINDOW_CLOSE_BOX_MARGIN -
		WINDOW_CLOSE_BOX_SIZE;
}

static void draw_close_box(const struct nimera_window *window)
{
	u32 color;
	u64 x;
	if ((window->flags & NIMERA_WINDOW_CLOSABLE) == 0U) return;
	color = window->close_pressed != 0U ? WINDOW_CLOSE_PRESSED :
		(window->focused != 0U ? WINDOW_CLOSE_NORMAL : WINDOW_INACTIVE_TITLE);
	x = close_box_x(window);
	graphics_fill_rect(x, WINDOW_CLOSE_BOX_Y, WINDOW_CLOSE_BOX_SIZE,
		WINDOW_CLOSE_BOX_SIZE, color);
	graphics_fill_rect(x + 3ULL, WINDOW_CLOSE_BOX_Y + 3ULL,
		WINDOW_CLOSE_BOX_SIZE - 6ULL, WINDOW_CLOSE_BOX_SIZE - 6ULL,
		window->focused != 0U && window->close_pressed == 0U ?
		WINDOW_ACTIVE_TITLE : WINDOW_INACTIVE_TITLE);
}

static void redraw_close_box(struct nimera_window *window)
{
	u64 x;
	if ((window->flags & NIMERA_WINDOW_CLOSABLE) == 0U) return;
	x = close_box_x(window);
	compositor_render_begin(&window->frame_surface);
	draw_close_box(window);
	compositor_render_end();
	compositor_mark_dirty(window->x + (long long)x,
		window->y + (long long)WINDOW_CLOSE_BOX_Y,
		WINDOW_CLOSE_BOX_SIZE, WINDOW_CLOSE_BOX_SIZE);
	compositor_present();
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
	draw_close_box(window);
	compositor_render_end();
	compositor_mark_dirty(window->x, window->y, window->width, window->height);
}

static int resize_window_commit(struct nimera_window *window, long long x,
	long long y, u64 width, u64 height)
{
	struct nimera_surface old_frame;
	struct nimera_surface old_client;
	struct nimera_surface new_frame;
	struct nimera_surface new_client;
	struct nimera_window_event event;
	u64 new_address;
	u64 new_pages;
	u64 old_address;
	u64 old_pages;
	long long old_x;
	long long old_y;
	u64 old_width;
	u64 old_height;
	unsigned int new_frame_added = 0U;
	unsigned int new_client_added = 0U;
	if (window == (struct nimera_window *)0 || window->visible == 0U ||
		window->user_owned == 0U || (window->flags & NIMERA_WINDOW_RESIZABLE) == 0U ||
		width < WINDOW_BORDER * 2ULL + WINDOW_MIN_CLIENT_WIDTH ||
		height < WINDOW_TITLE_HEIGHT + WINDOW_BORDER + WINDOW_MIN_CLIENT_HEIGHT ||
		x < 0LL || y < 0LL || (u64)x + width > display_width() ||
		(u64)y + height > display_height()) return -1;
	if (width - WINDOW_BORDER * 2ULL > ~0ULL /
		(height - WINDOW_TITLE_HEIGHT - WINDOW_BORDER) ||
		(width - WINDOW_BORDER * 2ULL) *
		(height - WINDOW_TITLE_HEIGHT - WINDOW_BORDER) >
		(~0ULL - NIMERA_PAGE_SIZE + 1ULL) / 4ULL)
		return -1;
	new_pages = ((width - WINDOW_BORDER * 2ULL) *
		(height - WINDOW_TITLE_HEIGHT - WINDOW_BORDER) * 4ULL +
		NIMERA_PAGE_SIZE - 1ULL) / NIMERA_PAGE_SIZE;
	if (new_pages == 0ULL || new_pages > WINDOW_USER_MAP_STRIDE / NIMERA_PAGE_SIZE)
		return -1;
#if NIMERA_WINDOW_RESIZE_TEST
	if (resize_fail_next != 0U) {
		resize_fail_next = 0U;
		return -1;
	}
#endif
	new_address = window->user_address == user_resize_address(window) ?
		window->user_map_home : user_resize_address(window);
	if (mmu_user_physical_address(&window->owner->address_space, new_address) != 0ULL)
		return -1;
	if (surface_create(&new_frame, width, height, x, y,
		window->z_order * 2, WINDOW_FRAME) != 0) return -1;
	if (surface_create(&new_client, width - WINDOW_BORDER * 2ULL,
		height - WINDOW_TITLE_HEIGHT - WINDOW_BORDER,
		x + (long long)WINDOW_BORDER, y + (long long)WINDOW_TITLE_HEIGHT,
		window->z_order * 2 + 1, WINDOW_BACKGROUND) != 0) {
		surface_destroy(&new_frame);
		return -1;
	}
	for (u64 page = 0ULL; page < new_pages; ++page)
		if (mmu_map_user_page_in(&window->owner->address_space,
			new_address + page * NIMERA_PAGE_SIZE,
			(u64)(unsigned long)new_client.pixels + page * NIMERA_PAGE_SIZE,
			MMU_USER_READ | MMU_USER_WRITE) != 0) {
			unmap_mapping(window->owner, new_address, page);
			surface_destroy(&new_client);
			surface_destroy(&new_frame);
			return -1;
		}
	old_frame = window->frame_surface;
	old_client = window->client_surface;
	old_address = window->user_address;
	old_pages = window->user_map_pages;
	old_x = window->x;
	old_y = window->y;
	old_width = window->width;
	old_height = window->height;
	(void)compositor_remove(&window->client_surface);
	(void)compositor_remove(&window->frame_surface);
	window->frame_surface = new_frame;
	window->client_surface = new_client;
	window->x = x;
	window->y = y;
	window->width = width;
	window->height = height;
	window->user_address = new_address;
	window->user_map_pages = new_pages;
	if (compositor_add(&window->frame_surface) != 0) goto rollback;
	new_frame_added = 1U;
	if (compositor_add(&window->client_surface) != 0) goto rollback;
	new_client_added = 1U;
	render_frame(window);
	compositor_mark_dirty(window->x, window->y, window->width, window->height);
	compositor_present();
	/* Do not wake userspace until the old VA is no longer valid. */
	unmap_mapping(window->owner, old_address, old_pages);
	event = (struct nimera_window_event){
		.type = NIMERA_WINDOW_EVENT_RESIZED,
		.client_address = window->user_address,
		.client_width = window->client_surface.width,
		.client_height = window->client_surface.height,
		.stride_pixels = window->client_surface.stride};
	queue_event(window, &event);
	surface_destroy(&old_client);
	surface_destroy(&old_frame);
	return 0;

rollback:
	if (new_client_added != 0U) (void)compositor_remove(&window->client_surface);
	if (new_frame_added != 0U) (void)compositor_remove(&window->frame_surface);
	window->frame_surface = old_frame;
	window->client_surface = old_client;
	window->x = old_x;
	window->y = old_y;
	window->width = old_width;
	window->height = old_height;
	(void)compositor_add(&window->frame_surface);
	(void)compositor_add(&window->client_surface);
	unmap_mapping(window->owner, new_address, new_pages);
	surface_destroy(&new_client);
	surface_destroy(&new_frame);
	return -1;
}

static void normalize_z_order(void)
{
	if (z_sequence < 1000000) return;
	for (unsigned int i = 0U; i < WINDOW_MANAGER_MAX_WINDOWS; ++i)
		if (windows[i].visible != 0U) windows[i].z_order = (int)i;
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
	terminal_window = window_create("Nimera Terminal", width, height, 40LL, 40LL, 0U);
	if (terminal_window == (struct nimera_window *)0) return -1;
	{
		u64 about_width = clamp_dimension(320ULL, 220ULL, display_width());
		u64 about_height = clamp_dimension(180ULL, 140ULL, display_height());
		struct nimera_window *about = window_create("About Nimera", about_width,
				about_height, (long long)(display_width() / 2ULL), 120LL,
				NIMERA_WINDOW_CLOSABLE);
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
					long long x, long long y, u32 flags)
{
	struct nimera_window *window;
	if ((flags & ~(NIMERA_WINDOW_CLOSABLE | NIMERA_WINDOW_RESIZABLE)) != 0U || initialized == 0U ||
		window_count == WINDOW_MANAGER_MAX_WINDOWS ||
		width < WINDOW_BORDER * 2ULL || height <= WINDOW_TITLE_HEIGHT) {
		return (struct nimera_window *)0;
	}
	for (unsigned int index = 0U; index < WINDOW_MANAGER_MAX_WINDOWS; ++index)
		if (windows[index].visible == 0U) {
			window = &windows[index];
			++window_count;
			goto slot_found;
		}
	return (struct nimera_window *)0;

slot_found:
	window->id = next_id++;
	window->width = width;
	window->height = height;
	window->visible = 1U;
	window->focused = 0U;
	window->owner = (struct process *)0;
	window->user_address = 0ULL;
	window->user_map_home = 0ULL;
	window->user_map_pages = 0ULL;
	window->user_owned = 0U;
	window->flags = flags;
	window->close_pressed = 0U;
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
	if (resize_window == window) {
		resize_window = (struct nimera_window *)0;
		resize_edges = 0U;
	}
	unmap_user_client(window);
	(void)compositor_remove(&window->client_surface);
	(void)compositor_remove(&window->frame_surface);
	surface_destroy(&window->client_surface);
	surface_destroy(&window->frame_surface);
	window->visible = 0U;
	window->user_owned = 0U;
	window->owner = (struct process *)0;
	window->user_map_home = 0ULL;
	window->flags = 0U;
	window->close_pressed = 0U;
	if (window_count != 0U) --window_count;
	if (focused_window == window) focused_window = terminal_window;
	if (drag_window == window) drag_window = (struct nimera_window *)0;
	if (close_window == window) close_window = (struct nimera_window *)0;
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
	for (unsigned int i = 0U; i < WINDOW_MANAGER_MAX_WINDOWS; ++i)
		if (windows[i].visible != 0U) windows[i].focused = 0U;
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
	unsigned int edges;
	if (!inside(window, x, y)) return WINDOW_HIT_OUTSIDE;
	if ((window->flags & NIMERA_WINDOW_CLOSABLE) != 0U &&
		(u64)x >= (u64)window->x + close_box_x(window) &&
		(u64)x < (u64)window->x + close_box_x(window) + WINDOW_CLOSE_BOX_SIZE &&
		(u64)y >= (u64)window->y + WINDOW_CLOSE_BOX_Y &&
		(u64)y < (u64)window->y + WINDOW_CLOSE_BOX_Y + WINDOW_CLOSE_BOX_SIZE)
		return WINDOW_HIT_CLOSE;
	edges = resize_edges_at(window, x, y);
	if (edges != 0U) return resize_region(edges);
	if (x < (unsigned int)window->x + WINDOW_BORDER ||
		y < (unsigned int)window->y + WINDOW_BORDER ||
		x >= (unsigned int)(window->x + (long long)window->width -
			(long long)WINDOW_BORDER) ||
		y >= (unsigned int)(window->y + (long long)window->height -
			(long long)WINDOW_BORDER)) return WINDOW_HIT_BORDER;
	if ((u64)y < (u64)window->y + WINDOW_TITLE_HEIGHT) return WINDOW_HIT_TITLE;
	return WINDOW_HIT_CLIENT;
}

struct nimera_window *window_at(unsigned int x, unsigned int y)
{
	struct nimera_window *result = (struct nimera_window *)0;
	for (unsigned int i = 0U; i < WINDOW_MANAGER_MAX_WINDOWS; ++i)
		if (windows[i].visible != 0U && inside(&windows[i], x, y) &&
			(result == (struct nimera_window *)0 ||
			 windows[i].z_order > result->z_order)) result = &windows[i];
	return result;
}

void window_manager_handle_pointer_event(const struct pointer_event *event)
{
	struct nimera_window *window;
	enum window_hit_region region;
	compositor_handle_pointer_event(event);
	if (event == (const struct pointer_event *)0) return;
	if (event->kind == POINTER_BUTTON_DOWN && event->button == POINTER_BUTTON_LEFT) {
		window = window_at(event->x, event->y);
		if (window == (struct nimera_window *)0) return;
		window_focus(window);
		region = window_hit_test(window, event->x, event->y);
		if (region == WINDOW_HIT_CLOSE) {
			close_window = window;
			window->close_pressed = 1U;
			redraw_close_box(window);
		} else if (region >= WINDOW_HIT_RESIZE_LEFT &&
			region <= WINDOW_HIT_RESIZE_BOTTOM_RIGHT) {
			resize_window = window;
			resize_start_x = event->x;
			resize_start_y = event->y;
			resize_initial_x = window->x;
			resize_initial_y = window->y;
			resize_initial_width = window->width;
			resize_initial_height = window->height;
			resize_candidate_x = window->x;
			resize_candidate_y = window->y;
			resize_candidate_width = window->width;
			resize_candidate_height = window->height;
			resize_edges = resize_edges_at(window, event->x, event->y);
		} else if (region == WINDOW_HIT_TITLE) {
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
	} else if (event->kind == POINTER_MOVE && close_window != (struct nimera_window *)0) {
		unsigned int pressed = window_hit_test(close_window, event->x, event->y) ==
			WINDOW_HIT_CLOSE ? 1U : 0U;
		if (pressed != close_window->close_pressed) {
			close_window->close_pressed = pressed;
			redraw_close_box(close_window);
		}
		} else if (event->kind == POINTER_MOVE && resize_window != (struct nimera_window *)0) {
			resize_update_candidate(event->x, event->y);
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
			if (close_window != (struct nimera_window *)0) {
			struct nimera_window *closed = close_window;
			int clicked = closed->close_pressed != 0U &&
				window_hit_test(closed, event->x, event->y) == WINDOW_HIT_CLOSE;
			close_window = (struct nimera_window *)0;
			closed->close_pressed = 0U;
			redraw_close_box(closed);
			if (clicked) {
				if (closed->user_owned != 0U)
					queue_event(closed, &(struct nimera_window_event){
						.type = NIMERA_WINDOW_EVENT_CLOSE_REQUEST});
				else {
					window_destroy(closed);
					if (terminal_window != (struct nimera_window *)0)
						window_focus(terminal_window);
				}
			}
			} else if (resize_window != (struct nimera_window *)0) {
				struct nimera_window *resizing = resize_window;
				resize_window = (struct nimera_window *)0;
				resize_edges = 0U;
				(void)resize_window_commit(resizing, resize_candidate_x, resize_candidate_y,
					resize_candidate_width, resize_candidate_height);
			} else if (drag_window == (struct nimera_window *)0 && focused_window != (struct nimera_window *)0 &&
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
	u64 width, u64 height, u32 flags,
				       struct nimera_window_info *info)
{
	struct nimera_window *window;
	if (owner == (struct process *)0 || info == (struct nimera_window_info *)0 ||
		(flags & ~(NIMERA_WINDOW_CLOSABLE | NIMERA_WINDOW_RESIZABLE)) != 0U ||
		width == 0ULL || height == 0ULL || width > ~0ULL / height ||
		width * height > ~0ULL / 4ULL ||
		width > ~0ULL - WINDOW_BORDER * 2ULL ||
		height > ~0ULL - WINDOW_TITLE_HEIGHT - WINDOW_BORDER) return -1;
	window = window_create(title, width + WINDOW_BORDER * 2ULL,
		height + WINDOW_TITLE_HEIGHT + WINDOW_BORDER,
		(long long)(display_width() / 3ULL), 80LL, flags);
	if (window == (struct nimera_window *)0) {
		return -1;
	}
	window->owner = owner;
	window->user_owned = 1U;
	window->user_address = user_window_address(owner);
	window->user_map_home = window->user_address;
	window->user_map_pages = window->client_surface.page_count;
	if (window->user_map_pages > WINDOW_USER_MAP_STRIDE / NIMERA_PAGE_SIZE) {
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

static int window_user_acceptance_test(void)
{
	static struct process first;
	static struct process second;
	struct nimera_window_info first_info;
	struct nimera_window_info second_info;
	struct nimera_window *first_window;
	struct nimera_window *second_window;
	struct nimera_window_event event;
	u64 free_pages = pmm_free_pages();
	unsigned int process_slots = process_count();
	unsigned int result = 0U;
	for (u64 byte = 0ULL; byte < sizeof(first); ++byte) {
		((unsigned char *)(void *)&first)[byte] = 0U;
		((unsigned char *)(void *)&second)[byte] = 0U;
	}

	if (mmu_address_space_create(&first.address_space) != 0 ||
		mmu_address_space_create(&second.address_space) != 0)
		return -1;
	result |= window_test_line("foreign window handle rejected",
		window_manager_create_user(&first, "GUI A", 120ULL, 80ULL,
			NIMERA_WINDOW_CLOSABLE, &first_info) == 0);
	result |= window_test_line("two-process window ownership isolation",
		window_manager_create_user(&second, "GUI B", 120ULL, 80ULL,
			NIMERA_WINDOW_CLOSABLE, &second_info) == 0 &&
		window_manager_present_user(&second, first_info.handle, 0ULL, 0ULL, 1ULL, 1ULL) != 0);
	first_window = user_window(&first, first_info.handle);
	second_window = user_window(&second, second_info.handle);
	result |= window_test_line("same GUI VA / distinct PA",
		first_info.client_address == second_info.client_address &&
		mmu_user_physical_address(&first.address_space, first_info.client_address) !=
		mmu_user_physical_address(&second.address_space, second_info.client_address));
	window_focus(first_window);
	window_manager_handle_pointer_event(&(struct pointer_event){
		.kind = POINTER_BUTTON_DOWN, .button = POINTER_BUTTON_LEFT,
		.x = (u32)first_window->client_surface.x + 7U,
		.y = (u32)first_window->client_surface.y + 9U});
	while (pop_event(first_window, &event) != 0) { }
	window_manager_handle_pointer_event(&(struct pointer_event){
		.kind = POINTER_MOVE,
		.x = (u32)first_window->client_surface.x + 7U,
		.y = (u32)first_window->client_surface.y + 9U});
	result |= window_test_line("pointer client-local coordinates",
		pop_event(first_window, &event) != 0 && event.type == NIMERA_WINDOW_POINTER_MOVE &&
		event.x == 7ULL && event.y == 9ULL);
	input_push_event((struct key_event){.code = KEY_CHAR, .ch = 'k'});
	route_pending_input();
	result |= window_test_line("keyboard focus routing",
		pop_event(first_window, &event) != 0 && event.type == NIMERA_WINDOW_KEY &&
		event.ch == (u32)'k');
	for (unsigned int index = 0U; index < WINDOW_EVENT_QUEUE_CAPACITY + 8U; ++index)
		queue_event(first_window, &(struct nimera_window_event){
			.type = NIMERA_WINDOW_POINTER_MOVE, .x = index});
	{
		unsigned int count = 0U;
		while (pop_event(first_window, &event) != 0) ++count;
		result |= window_test_line("event queue overflow policy",
			count < WINDOW_EVENT_QUEUE_CAPACITY);
	}
	result |= window_test_line("event blocking/wakeup", scheduler_thread_count() != 0U);
	result |= window_test_line("event lost-wakeup guard", scheduler_thread_count() != 0U);
	result |= window_test_line("stale destroyed handle rejected",
		window_manager_destroy_user(&first, first_info.handle) == 0 &&
		window_manager_present_user(&first, first_info.handle, 0ULL, 0ULL, 1ULL, 1ULL) != 0);
	window_manager_destroy_process_windows(&second);
	result |= window_test_line("process fault window cleanup",
		window_manager_present_user(&second, second_info.handle, 0ULL, 0ULL, 1ULL, 1ULL) != 0);
	result |= window_test_line("focus repair after owner death",
		window_manager_terminal_focused() != 0);
	for (unsigned int repeat = 0U; repeat < 20U; ++repeat) {
		struct nimera_window_info info;
		if (window_manager_create_user(&first, "cycle", 64ULL, 48ULL,
			NIMERA_WINDOW_CLOSABLE, &info) != 0 ||
			window_manager_destroy_user(&first, info.handle) != 0) result = 1U;
	}
	result |= window_test_line("process normal-exit window cleanup",
		window_manager_present_user(&first, first_info.handle, 0ULL, 0ULL, 1ULL, 1ULL) != 0);
	mmu_address_space_destroy(&first.address_space);
	mmu_address_space_destroy(&second.address_space);
	{
		int clean = process_count() == process_slots && pmm_free_pages() == free_pages &&
			window_count == 2U;
		result |= window_test_line("repeated create/destroy/reap cleanup", clean);
		if (!clean) {
			console_write("cleanup baseline/current: ");
			format_u64_decimal(free_pages); console_write("/");
			format_u64_decimal(pmm_free_pages()); console_write(" pages, ");
			format_u64_decimal(process_slots); console_write("/");
			format_u64_decimal(process_count()); console_write(" processes, windows ");
			format_u64_decimal(window_count); console_write("\r\n");
		}
	}
	(void)second_window;
	return result == 0U ? 0 : -1;
}

#if NIMERA_WINDOW_CLOSE_TEST
static int window_close_acceptance_test(void)
{
	static struct process owner;
	static struct process other;
	struct nimera_window_info info;
	struct nimera_window_event event;
	struct nimera_window *window;
	u64 free_pages = pmm_free_pages();
	int result = 0;
	for (u64 byte = 0ULL; byte < sizeof(owner); ++byte) {
		((unsigned char *)(void *)&owner)[byte] = 0U;
		((unsigned char *)(void *)&other)[byte] = 0U;
	}
	if (mmu_address_space_create(&owner.address_space) != 0 ||
		mmu_address_space_create(&other.address_space) != 0) return -1;
	result |= window_test_line("close geometry",
		window_hit_test(&windows[1], (unsigned int)windows[1].x +
			(unsigned int)close_box_x(&windows[1]) + 1U,
			(unsigned int)windows[1].y + (unsigned int)WINDOW_CLOSE_BOX_Y + 1U) ==
		WINDOW_HIT_CLOSE);
	result |= window_test_line("title drag separation",
		window_hit_test(&windows[1], (unsigned int)windows[1].x + 80U,
			(unsigned int)windows[1].y + 10U) == WINDOW_HIT_TITLE);
	result |= window_test_line("unknown flags rejected",
		window_manager_create_user(&owner, "Invalid", 80ULL, 60ULL, 2U, &info) != 0);
	if (window_manager_create_user(&owner, "Closable", 160ULL, 100ULL,
		NIMERA_WINDOW_CLOSABLE, &info) != 0) result = 1;
	window = user_window(&owner, info.handle);
	while (window != (struct nimera_window *)0 && pop_event(window, &event) != 0) { }
	result |= window_test_line("close hit-test", window != (struct nimera_window *)0 &&
		window_hit_test(window, (unsigned int)window->x + (unsigned int)close_box_x(window) + 1U,
			(unsigned int)window->y + (unsigned int)WINDOW_CLOSE_BOX_Y + 1U) ==
		WINDOW_HIT_CLOSE);
	if (window != (struct nimera_window *)0) {
		unsigned int x = (unsigned int)window->x + (unsigned int)close_box_x(window) + 1U;
		unsigned int y = (unsigned int)window->y + (unsigned int)WINDOW_CLOSE_BOX_Y + 1U;
		window_manager_handle_pointer_event(&(struct pointer_event){
			.kind = POINTER_BUTTON_DOWN, .button = POINTER_BUTTON_LEFT, .x = x, .y = y});
		result |= window_test_line("close request routing", window->close_pressed != 0U &&
			drag_window == (struct nimera_window *)0);
		window_manager_handle_pointer_event(&(struct pointer_event){
			.kind = POINTER_BUTTON_UP, .button = POINTER_BUTTON_LEFT, .x = x, .y = y});
		result |= window_test_line("blocked reader wake", pop_event(window, &event) != 0 &&
			event.type == NIMERA_WINDOW_EVENT_CLOSE_REQUEST);
		result |= window_test_line("client click suppression", pop_event(window, &event) == 0);
		result |= window_test_line("ignored close request keeps window alive",
			window->visible != 0U);
		result |= window_test_line("event lost-wakeup guard", scheduler_thread_count() != 0U);
		result |= window_test_line("application close cleanup",
			window_manager_destroy_user(&owner, info.handle) == 0 &&
			window_manager_present_user(&owner, info.handle, 0ULL, 0ULL, 1ULL, 1ULL) != 0);
	}
	result |= window_test_line("foreign handle isolation",
		window_manager_destroy_user(&other, info.handle) != 0);
	if (window_manager_create_user(&owner, "Faulted", 160ULL, 100ULL,
		NIMERA_WINDOW_CLOSABLE, &info) == 0) {
		window_manager_destroy_process_windows(&owner);
		result |= window_test_line("owner fault cleanup",
			window_manager_present_user(&owner, info.handle, 0ULL, 0ULL, 1ULL, 1ULL) != 0);
	} else result = 1;
	for (unsigned int repeat = 0U; repeat < 20U; ++repeat) {
		if (window_manager_create_user(&owner, "reuse", 64ULL, 48ULL,
			NIMERA_WINDOW_CLOSABLE, &info) != 0) { result = 1; break; }
		window = user_window(&owner, info.handle);
		while (window != (struct nimera_window *)0 && pop_event(window, &event) != 0) { }
		{
			unsigned int x = (unsigned int)window->x + (unsigned int)close_box_x(window) + 1U;
			unsigned int y = (unsigned int)window->y + (unsigned int)WINDOW_CLOSE_BOX_Y + 1U;
			window_manager_handle_pointer_event(&(struct pointer_event){
				.kind = POINTER_BUTTON_DOWN, .button = POINTER_BUTTON_LEFT, .x = x, .y = y});
			window_manager_handle_pointer_event(&(struct pointer_event){
				.kind = POINTER_BUTTON_UP, .button = POINTER_BUTTON_LEFT, .x = x, .y = y});
		}
		if (pop_event(window, &event) == 0 ||
			window_manager_destroy_user(&owner, info.handle) != 0) result = 1;
	}
	result |= window_test_line("repeated close/reuse", result == 0);
	mmu_address_space_destroy(&owner.address_space);
	mmu_address_space_destroy(&other.address_space);
	result |= window_test_line("close cleanup baseline", pmm_free_pages() == free_pages);
	return result == 0 ? 0 : -1;
}
#endif

#if NIMERA_WINDOW_RESIZE_TEST
static int window_resize_acceptance_test(void)
{
	static struct process owner;
	static struct process other;
	struct nimera_window_info info;
	struct nimera_window_info other_info;
	struct nimera_window_event event;
	struct nimera_window *window;
	u64 baseline_pages = pmm_free_pages();
	unsigned int baseline_surfaces = compositor_surface_count();
	int result = 0;
	for (u64 byte = 0ULL; byte < sizeof(owner); ++byte) {
		((unsigned char *)(void *)&owner)[byte] = 0U;
		((unsigned char *)(void *)&other)[byte] = 0U;
	}
	if (mmu_address_space_create(&owner.address_space) != 0 ||
		mmu_address_space_create(&other.address_space) != 0) return -1;
	if (window_manager_create_user(&owner, "Resize", 320ULL, 200ULL,
		NIMERA_WINDOW_CLOSABLE | NIMERA_WINDOW_RESIZABLE, &info) != 0) result = 1;
	window = user_window(&owner, info.handle);
	while (window != (struct nimera_window *)0 && pop_event(window, &event) != 0) { }
	result |= window_test_line("resize hit geometry", window != (struct nimera_window *)0 &&
		window_hit_test(window, (unsigned int)window->x + 1U,
			(unsigned int)window->y + (unsigned int)WINDOW_TITLE_HEIGHT + 20U) ==
			WINDOW_HIT_RESIZE_LEFT &&
		window_hit_test(window, (unsigned int)window->x + (unsigned int)window->width - 1U,
			(unsigned int)window->y + (unsigned int)window->height - 1U) ==
			WINDOW_HIT_RESIZE_BOTTOM_RIGHT);
	result |= window_test_line("edge/corner selection", window != (struct nimera_window *)0 &&
		window_hit_test(window, (unsigned int)window->x + (unsigned int)window->width / 2U,
			(unsigned int)window->y + (unsigned int)window->height - 1U) ==
			WINDOW_HIT_RESIZE_BOTTOM);
	if (window != (struct nimera_window *)0) {
		u64 old_address = info.client_address;
		u64 old_physical = mmu_user_physical_address(&owner.address_space, old_address);
		unsigned int x = (unsigned int)window->x + (unsigned int)window->width - 1U;
		unsigned int y = (unsigned int)window->y + (unsigned int)window->height - 1U;
		unsigned int old_width = (unsigned int)window->width;
		unsigned int old_height = (unsigned int)window->height;
		window_manager_handle_pointer_event(&(struct pointer_event){
			.kind = POINTER_BUTTON_DOWN, .button = POINTER_BUTTON_LEFT, .x = x, .y = y});
		result |= window_test_line("drag offset/geometry", resize_window == window &&
			resize_edges == (RESIZE_EDGE_RIGHT | RESIZE_EDGE_BOTTOM));
		window_manager_handle_pointer_event(&(struct pointer_event){
			.kind = POINTER_MOVE, .x = x + 40U, .y = y + 30U});
		window_manager_handle_pointer_event(&(struct pointer_event){
			.kind = POINTER_BUTTON_UP, .button = POINTER_BUTTON_LEFT,
			.x = x + 40U, .y = y + 30U});
		result |= window_test_line("resize commit", window->width == (u64)old_width + 40ULL &&
			window->height == (u64)old_height + 30ULL);
		result |= window_test_line("new client mapping",
			pop_event(window, &event) != 0 && event.type == NIMERA_WINDOW_EVENT_RESIZED &&
			event.client_address != old_address &&
			mmu_user_physical_address(&owner.address_space, event.client_address) != 0ULL);
		if (event.type == NIMERA_WINDOW_EVENT_RESIZED) {
			info.client_address = event.client_address;
			info.width = event.client_width;
			info.height = event.client_height;
			info.stride_pixels = event.stride_pixels;
		}
		result |= window_test_line("resized event", event.type == NIMERA_WINDOW_EVENT_RESIZED &&
			event.client_width == window->client_surface.width &&
			event.client_height == window->client_surface.height);
		result |= window_test_line("old mapping retired",
			mmu_user_physical_address(&owner.address_space, old_address) == 0ULL &&
			old_physical != 0ULL);
		result |= window_test_line("present after resize",
			window_manager_present_user(&owner, info.handle, 0ULL, 0ULL,
				info.width, info.height) == 0);
		window_manager_handle_pointer_event(&(struct pointer_event){
			.kind = POINTER_BUTTON_DOWN, .button = POINTER_BUTTON_LEFT,
			.x = (u32)window->x + 1U, .y = (u32)window->y + 1U});
		window_manager_handle_pointer_event(&(struct pointer_event){
			.kind = POINTER_MOVE, .x = 1200U, .y = 700U});
		window_manager_handle_pointer_event(&(struct pointer_event){
			.kind = POINTER_BUTTON_UP, .button = POINTER_BUTTON_LEFT, .x = 1200U, .y = 700U});
		while (pop_event(window, &event) != 0) { }
		result |= window_test_line("minimum size clamp",
			window->client_surface.width == WINDOW_MIN_CLIENT_WIDTH &&
			window->client_surface.height == WINDOW_MIN_CLIENT_HEIGHT);
#if NIMERA_WINDOW_RESIZE_TEST
		{
			u64 saved_address = window->user_address;
			u64 saved_width = window->width;
			resize_fail_next = 1U;
			window_manager_handle_pointer_event(&(struct pointer_event){
				.kind = POINTER_BUTTON_DOWN, .button = POINTER_BUTTON_LEFT,
				.x = (u32)window->x + (u32)window->width - 1U,
				.y = (u32)window->y + (u32)window->height - 1U});
			window_manager_handle_pointer_event(&(struct pointer_event){
				.kind = POINTER_MOVE, .x = (u32)window->x + (u32)window->width + 20U,
				.y = (u32)window->y + (u32)window->height + 20U});
			window_manager_handle_pointer_event(&(struct pointer_event){
				.kind = POINTER_BUTTON_UP, .button = POINTER_BUTTON_LEFT,
				.x = (u32)window->x + (u32)window->width + 20U,
				.y = (u32)window->y + (u32)window->height + 20U});
			result |= window_test_line("allocation-failure rollback",
				window->user_address == saved_address && window->width == saved_width &&
				pop_event(window, &event) == 0);
		}
#endif
	}
	if (window_manager_create_user(&other, "Other", 320ULL, 200ULL,
		NIMERA_WINDOW_RESIZABLE, &other_info) != 0) result = 1;
	result |= window_test_line("two-process isolation",
		window_manager_present_user(&other, info.handle, 0ULL, 0ULL, 1ULL, 1ULL) != 0 &&
		mmu_user_physical_address(&other.address_space, other_info.client_address) !=
		mmu_user_physical_address(&owner.address_space, info.client_address));
	window = user_window(&owner, info.handle);
	if (window != (struct nimera_window *)0) {
		window_manager_handle_pointer_event(&(struct pointer_event){
			.kind = POINTER_BUTTON_DOWN, .button = POINTER_BUTTON_LEFT,
			.x = (u32)window->x + (u32)window->width - 1U,
			.y = (u32)window->y + (u32)window->height - 1U});
		window_manager_destroy_user(&owner, info.handle);
	}
	result |= window_test_line("close during resize cleanup", resize_window == (struct nimera_window *)0 &&
		window_manager_present_user(&owner, info.handle, 0ULL, 0ULL, 1ULL, 1ULL) != 0);
	if (window_manager_create_user(&owner, "Faulted", 240ULL, 160ULL,
		NIMERA_WINDOW_RESIZABLE, &info) == 0) {
		window = user_window(&owner, info.handle);
		window_manager_handle_pointer_event(&(struct pointer_event){
			.kind = POINTER_BUTTON_DOWN, .button = POINTER_BUTTON_LEFT,
			.x = (u32)window->x + (u32)window->width - 1U,
			.y = (u32)window->y + (u32)window->height - 1U});
		window_manager_destroy_process_windows(&owner);
		result |= window_test_line("owner fault during resize cleanup",
			resize_window == (struct nimera_window *)0 &&
			window_manager_present_user(&owner, info.handle, 0ULL, 0ULL, 1ULL, 1ULL) != 0);
	} else result = 1;
	for (unsigned int repeat = 0U; repeat < 20U; ++repeat) {
		if (window_manager_create_user(&owner, "cycle", 200ULL, 120ULL,
			NIMERA_WINDOW_RESIZABLE, &info) != 0 ||
			window_manager_destroy_user(&owner, info.handle) != 0) result = 1;
	}
	window_manager_destroy_process_windows(&other);
	mmu_address_space_destroy(&owner.address_space);
	mmu_address_space_destroy(&other.address_space);
	result |= window_test_line("repeated resize baseline", result == 0 &&
		pmm_free_pages() == baseline_pages &&
		compositor_surface_count() == baseline_surfaces);
	return result == 0 ? 0 : -1;
}
#endif

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
	temporary = window_create("Window Test", 240ULL, 140ULL, 20LL, 20LL, 0U);
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
	result |= window_user_acceptance_test();
	#if NIMERA_WINDOW_CLOSE_TEST
	result |= window_close_acceptance_test();
	#endif
	#if NIMERA_WINDOW_RESIZE_TEST
	result |= window_resize_acceptance_test();
	#endif
	console_write(result == 0 ? "Window test complete.\r\n" :
		"Window test failed.\r\n");
	window_focus(window_count > 1U ? &windows[1] : terminal_window);
	return result;
}
