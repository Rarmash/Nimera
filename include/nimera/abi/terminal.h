#ifndef NIMERA_ABI_TERMINAL_H
#define NIMERA_ABI_TERMINAL_H

#include <nimera/types.h>

enum nimera_key_code {
	NIMERA_KEY_CHAR = 0U,
	NIMERA_KEY_ENTER = 1U,
	NIMERA_KEY_BACKSPACE = 2U,
	NIMERA_KEY_DELETE = 3U,
	NIMERA_KEY_UP = 4U,
	NIMERA_KEY_DOWN = 5U,
	NIMERA_KEY_LEFT = 6U,
	NIMERA_KEY_RIGHT = 7U,
	NIMERA_KEY_HOME = 8U,
	NIMERA_KEY_END = 9U,
	NIMERA_KEY_ESCAPE = 10U,
	NIMERA_KEY_TAB = 11U
};

#define NIMERA_KEY_MOD_CTRL 0x00000001U

struct nimera_key_event {
	u32 code;
	u32 ch;
	u32 modifiers;
	u32 reserved;
};

struct nimera_terminal_size {
	u32 columns;
	u32 rows;
};

#endif
