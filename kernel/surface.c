#include <nimera/pmm.h>
#include <nimera/surface.h>

static int surface_size(u64 width, u64 height, u64 *bytes, u64 *pages)
{
	if (width == 0ULL || height == 0ULL || width > ~0ULL / height ||
		width * height > ~0ULL / 4ULL) return -1;
	*bytes = width * height * 4ULL;
	*pages = (*bytes + NIMERA_PAGE_SIZE - 1ULL) / NIMERA_PAGE_SIZE;
	return *pages == 0ULL ? -1 : 0;
}

void surface_clear(struct nimera_surface *surface, u32 color)
{
	if (surface == (struct nimera_surface *)0 || surface->pixels == (u32 *)0)
		return;
	for (u64 row = 0ULL; row < surface->height; ++row)
		for (u64 column = 0ULL; column < surface->width; ++column)
			surface->pixels[row * surface->stride + column] = color;
}

int surface_create(struct nimera_surface *surface, u64 width, u64 height,
			   long long x, long long y, int z_order, u32 clear_color)
{
	u64 bytes;
	u64 pages;
	u64 physical;
	if (surface == (struct nimera_surface *)0 ||
		surface_size(width, height, &bytes, &pages) != 0 ||
		pmm_alloc_contiguous(pages, &physical) != 0) return -1;
	surface->pixels = (u32 *)(unsigned long)physical;
	surface->width = width;
	surface->height = height;
	surface->stride = width;
	surface->x = x;
	surface->y = y;
	surface->visible = 1U;
	surface->z_order = z_order;
	surface->page_count = pages;
	(void)bytes;
	surface_clear(surface, clear_color);
	return 0;
}

void surface_destroy(struct nimera_surface *surface)
{
	if (surface == (struct nimera_surface *)0) return;
	if (surface->pixels != (u32 *)0)
		for (u64 page = 0ULL; page < surface->page_count; ++page)
			pmm_free_page((u64)(unsigned long)surface->pixels +
				page * NIMERA_PAGE_SIZE);
	surface->pixels = (u32 *)0;
	surface->page_count = 0ULL;
	surface->visible = 0U;
}
