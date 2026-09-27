#ifndef NIMERA_INPUT_H
#define NIMERA_INPUT_H

#include <nimera/types.h>

enum key_code {
	KEY_CHAR,
	KEY_ENTER,
	KEY_BACKSPACE,
	KEY_DELETE,
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_HOME,
	KEY_END,
	KEY_ESCAPE,
	KEY_TAB
};

struct key_event {
	enum key_code code;
	char ch;
	unsigned int ctrl;
};

enum pointer_event_kind {
	POINTER_MOVE,
	POINTER_BUTTON_DOWN,
	POINTER_BUTTON_UP
};

enum pointer_button {
	POINTER_BUTTON_LEFT = 1U,
	POINTER_BUTTON_RIGHT = 2U
};

struct pointer_event {
	enum pointer_event_kind kind;
	u32 x;
	u32 y;
	unsigned int buttons;
	enum pointer_button button;
};

void input_init(void);
int input_push_event(struct key_event event);
int input_try_get_event(struct key_event *event);
struct key_event input_read_event(void);
int input_push_pointer_event(struct pointer_event event);
int input_try_get_pointer_event(struct pointer_event *event);
void input_wait_for_activity(void);
void input_set_hardware_available(int available);
int input_hardware_available(void);
u64 input_dropped_events(void);
int input_self_test(void);

#endif
