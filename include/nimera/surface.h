#ifndef NIMERA_SURFACE_H
#define NIMERA_SURFACE_H

#include <nimera/types.h>

/* All kernel surfaces use the display's single B8G8R8X8 pixel format. */
struct nimera_surface {
	u32 *pixels;
	u64 width;
	u64 height;
	u64 stride; /* pixels per row */
	long long x;
	long long y;
	unsigned int visible;
	int z_order;
	u64 page_count;
};

int surface_create(struct nimera_surface *surface, u64 width, u64 height,
			   long long x, long long y, int z_order, u32 clear_color);
void surface_destroy(struct nimera_surface *surface);
void surface_clear(struct nimera_surface *surface, u32 color);

#endif
