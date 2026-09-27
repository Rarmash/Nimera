#ifndef NIMERA_WINDOW_H
#define NIMERA_WINDOW_H

#include <nimera/input.h>
#include <nimera/surface.h>

#define WINDOW_MANAGER_MAX_WINDOWS 16U
#define WINDOW_TITLE_MAX 63U
#define WINDOW_BORDER 2ULL
#define WINDOW_TITLE_HEIGHT 28ULL

enum window_hit_region {
	WINDOW_HIT_OUTSIDE,
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
};

int window_manager_init(void);
struct nimera_window *window_create(const char *title, u64 width, u64 height,
					long long x, long long y);
void window_destroy(struct nimera_window *window);
void window_show(struct nimera_window *window, unsigned int visible);
void window_focus(struct nimera_window *window);
void window_move(struct nimera_window *window, long long x, long long y);
struct nimera_window *window_at(unsigned int x, unsigned int y);
enum window_hit_region window_hit_test(const struct nimera_window *window,
					       unsigned int x, unsigned int y);
void window_manager_handle_pointer_event(const struct pointer_event *event);
struct nimera_surface *window_manager_terminal_client_surface(void);
int window_manager_terminal_focused(void);
int window_manager_test(void);

#endif
