#ifndef NIMERA_DISPLAY_H
#define NIMERA_DISPLAY_H

#include <nimera/types.h>

enum display_pixel_format {
	DISPLAY_PIXEL_B8G8R8X8 = 1
};

int display_available(void);
u64 display_width(void);
u64 display_height(void);
u64 display_pitch(void);
enum display_pixel_format display_pixel_format(void);
u32 *display_framebuffer(void);
int display_flush(u64 x, u64 y, u64 width, u64 height);
void display_register(u32 *framebuffer, u64 width, u64 height, u64 pitch,
	                      enum display_pixel_format format,
	                      int (*flush)(u64, u64, u64, u64));

#endif
