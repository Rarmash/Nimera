#ifndef NIMERA_GRAPHICS_H
#define NIMERA_GRAPHICS_H

#include <nimera/types.h>

void graphics_put_pixel(u64 x, u64 y, u32 color);
void graphics_fill_rect(u64 x, u64 y, u64 width, u64 height, u32 color);
void graphics_clear(u32 color);
void graphics_draw_text(u64 x, u64 y, const char *text, u32 color);
void graphics_test(void);

#endif
