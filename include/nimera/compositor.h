#ifndef NIMERA_COMPOSITOR_H
#define NIMERA_COMPOSITOR_H

#include <nimera/input.h>
#include <nimera/surface.h>

#define COMPOSITOR_MAX_SURFACES 16U

int compositor_init(void);
int compositor_add(struct nimera_surface *surface);
int compositor_remove(struct nimera_surface *surface);
struct nimera_surface *compositor_terminal_surface(void);
void compositor_render_begin(struct nimera_surface *surface);
void compositor_render_end(void);
void compositor_mark_dirty(long long x, long long y, u64 width, u64 height);
void compositor_present(void);
void compositor_handle_pointer_event(const struct pointer_event *event);

int compositor_test(void);

#endif
