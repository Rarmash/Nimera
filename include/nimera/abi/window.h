#ifndef NIMERA_ABI_WINDOW_H
#define NIMERA_ABI_WINDOW_H

#include <nimera/types.h>

/* Client pixels are shared as 32-bit B8G8R8X8 values, matching the display. */
struct nimera_window_info {
	u64 handle;
	u64 client_address;
	u64 stride_pixels;
	u64 width;
	u64 height;
};

#define NIMERA_WINDOW_CLOSABLE 0x00000001U

enum nimera_window_event_type {
	NIMERA_WINDOW_FOCUS_GAINED = 1U,
	NIMERA_WINDOW_FOCUS_LOST = 2U,
	NIMERA_WINDOW_POINTER_MOVE = 3U,
	NIMERA_WINDOW_POINTER_BUTTON_DOWN = 4U,
	NIMERA_WINDOW_POINTER_BUTTON_UP = 5U,
	NIMERA_WINDOW_KEY = 6U,
	NIMERA_WINDOW_EVENT_CLOSE_REQUEST = 7U
};

struct nimera_window_event {
	u32 type;
	u32 button;
	u32 key_code;
	u32 modifiers;
	u32 ch;
	u32 reserved;
	u64 x;
	u64 y;
};

#endif
