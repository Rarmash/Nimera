#ifndef NIMERA_WINDOW_H
#define NIMERA_WINDOW_H

#include <nimera/input.h>
#include <nimera/surface.h>
#include <nimera/abi/window.h>

struct process;

#define WINDOW_MANAGER_MAX_WINDOWS 16U
#define WINDOW_TITLE_MAX 63U
#define WINDOW_BORDER 2ULL
#define WINDOW_TITLE_HEIGHT 28ULL
#define WINDOW_USER_MAP_BASE 0x14000000ULL
#define WINDOW_USER_MAP_STRIDE 0x00400000ULL
#define WINDOW_USER_RESIZE_MAP_BASE 0x18000000ULL
#define WINDOW_EVENT_QUEUE_CAPACITY 32U
#define WINDOW_CLOSE_BOX_SIZE 14ULL
#define WINDOW_CLOSE_BOX_Y 7ULL
#define WINDOW_CLOSE_BOX_MARGIN 7ULL
#define WINDOW_RESIZE_BORDER 6ULL
#define WINDOW_MIN_CLIENT_WIDTH 192ULL
#define WINDOW_MIN_CLIENT_HEIGHT 112ULL

enum window_hit_region {
	WINDOW_HIT_OUTSIDE,
	WINDOW_HIT_BORDER,
	WINDOW_HIT_CLOSE,
	WINDOW_HIT_RESIZE_LEFT,
	WINDOW_HIT_RESIZE_RIGHT,
	WINDOW_HIT_RESIZE_TOP,
	WINDOW_HIT_RESIZE_BOTTOM,
	WINDOW_HIT_RESIZE_TOP_LEFT,
	WINDOW_HIT_RESIZE_TOP_RIGHT,
	WINDOW_HIT_RESIZE_BOTTOM_LEFT,
	WINDOW_HIT_RESIZE_BOTTOM_RIGHT,
	WINDOW_HIT_TITLE,
	WINDOW_HIT_CLIENT
};

struct nimera_window {
	unsigned int id;
	long long x;
	long long y;
	u64 width;
	u64 height;
	unsigned int visible;
	unsigned int focused;
	int z_order;
	char title[WINDOW_TITLE_MAX + 1U];
	struct nimera_surface frame_surface;
	struct nimera_surface client_surface;
	struct process *owner;
	u64 user_address;
	u64 user_map_home;
	u64 user_map_pages;
	struct nimera_window_event events[WINDOW_EVENT_QUEUE_CAPACITY];
	unsigned int event_read;
	unsigned int event_write;
	unsigned int user_owned;
	u32 flags;
	unsigned int close_pressed;
};

int window_manager_init(void);
struct nimera_window *window_create(const char *title, u64 width, u64 height,
					long long x, long long y, u32 flags);
void window_destroy(struct nimera_window *window);
void window_show(struct nimera_window *window, unsigned int visible);
void window_focus(struct nimera_window *window);
void window_move(struct nimera_window *window, long long x, long long y);
struct nimera_window *window_at(unsigned int x, unsigned int y);
enum window_hit_region window_hit_test(const struct nimera_window *window,
					       unsigned int x, unsigned int y);
void window_manager_handle_pointer_event(const struct pointer_event *event);
int window_manager_create_user(struct process *owner, const char *title,
				       u64 width, u64 height, u32 flags,
				       struct nimera_window_info *info);
int window_manager_destroy_user(struct process *owner, u64 handle);
int window_manager_present_user(struct process *owner, u64 handle,
					u64 x, u64 y, u64 width, u64 height);
int window_manager_read_user_event(struct process *owner, u64 handle,
					struct nimera_window_event *event);
void window_manager_destroy_process_windows(struct process *owner);
struct nimera_surface *window_manager_terminal_client_surface(void);
int window_manager_terminal_focused(void);
int window_manager_test(void);

#endif
