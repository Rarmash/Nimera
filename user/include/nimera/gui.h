#ifndef NIMERA_USER_GUI_H
#define NIMERA_USER_GUI_H

#include <nimera/user.h>

struct nimera_gui_rect {
	long long x;
	long long y;
	u64 width;
	u64 height;
};

struct nimera_gui_canvas {
	u32 *pixels;
	u64 width;
	u64 height;
	u64 stride;
	struct nimera_gui_rect clip;
};

struct nimera_gui_damage {
	struct nimera_gui_rect rect;
	unsigned int dirty;
};

struct nimera_gui_window {
	u64 handle;
	struct nimera_gui_canvas canvas;
};

struct nimera_gui_label {
	struct nimera_gui_rect rect;
	const char *text;
};

struct nimera_gui_button {
	struct nimera_gui_rect rect;
	const char *label;
	unsigned int hovered;
	unsigned int pressed;
};

struct nimera_gui_focus {
	void *focused_widget;
};

struct nimera_gui_text_field {
	struct nimera_gui_rect rect;
	char *text;
	u64 capacity;
	u64 length;
	u64 cursor;
	unsigned long long scroll_x;
	unsigned int focused;
	unsigned int hovered;
	unsigned int pressed;
};

enum nimera_gui_text_field_result {
	NIMERA_GUI_TEXT_NONE = 0,
	NIMERA_GUI_TEXT_CHANGED = 1,
	NIMERA_GUI_TEXT_SUBMIT = 2
};

#define NIMERA_GUI_COLOR_BACKGROUND 0x00101828U
#define NIMERA_GUI_COLOR_PANEL      0x001b2a3dU
#define NIMERA_GUI_COLOR_TEXT       0x00ffffffU
#define NIMERA_GUI_COLOR_ACCENT     0x00e0b040U
#define NIMERA_GUI_COLOR_BORDER     0x005b7898U
#define NIMERA_GUI_COLOR_BUTTON     0x002b4660U
#define NIMERA_GUI_COLOR_HOVER      0x003e6688U
#define NIMERA_GUI_COLOR_PRESSED    0x00e0b040U

int nimera_gui_window_create(struct nimera_gui_window *window,
	unsigned int width, unsigned int height, const char *title,
	u64 title_length, u32 flags);
void nimera_gui_window_destroy(struct nimera_gui_window *window);
int nimera_gui_window_present(struct nimera_gui_window *window,
	const struct nimera_gui_damage *damage);
int nimera_gui_window_next_event(struct nimera_gui_window *window,
	struct nimera_window_event *event);
int nimera_gui_window_apply_event(struct nimera_gui_window *window,
	const struct nimera_window_event *event);

void nimera_gui_canvas_init(struct nimera_gui_canvas *canvas, u32 *pixels,
	u64 width, u64 height, u64 stride);
void nimera_gui_canvas_set_clip(struct nimera_gui_canvas *canvas,
	struct nimera_gui_rect clip);
void nimera_gui_clear(struct nimera_gui_canvas *canvas, u32 color);
void nimera_gui_fill_rect(struct nimera_gui_canvas *canvas,
	struct nimera_gui_rect rect, u32 color);
void nimera_gui_draw_rect(struct nimera_gui_canvas *canvas,
	struct nimera_gui_rect rect, u32 color);
unsigned int nimera_gui_text_width(const char *text);
unsigned int nimera_gui_font_line_height(void);
void nimera_gui_draw_text(struct nimera_gui_canvas *canvas, long long x,
	long long y, const char *text, u32 color);

int nimera_gui_rect_contains(struct nimera_gui_rect rect, long long x,
	long long y);
struct nimera_gui_rect nimera_gui_rect_intersection(
	struct nimera_gui_rect first, struct nimera_gui_rect second);
void nimera_gui_damage_reset(struct nimera_gui_damage *damage);
void nimera_gui_damage_add(struct nimera_gui_damage *damage,
	struct nimera_gui_rect rect);

void nimera_gui_label_draw(struct nimera_gui_canvas *canvas,
	const struct nimera_gui_label *label, u32 color);
int nimera_gui_button_handle_event(struct nimera_gui_button *button,
	const struct nimera_window_event *event, unsigned int *clicked);
void nimera_gui_button_draw(struct nimera_gui_canvas *canvas,
	const struct nimera_gui_button *button);

void nimera_gui_focus_init(struct nimera_gui_focus *focus);
void nimera_gui_focus_clear(struct nimera_gui_focus *focus);
int nimera_gui_text_field_init(struct nimera_gui_text_field *field,
	struct nimera_gui_rect rect, char *buffer, u64 capacity,
	const char *initial_text);
void nimera_gui_text_field_set_focus(struct nimera_gui_text_field *field,
	struct nimera_gui_focus *focus, unsigned int focused);
int nimera_gui_text_field_handle_event(struct nimera_gui_text_field *field,
	struct nimera_gui_focus *focus, const struct nimera_window_event *event,
	enum nimera_gui_text_field_result *result);
void nimera_gui_text_field_draw(struct nimera_gui_canvas *canvas,
	const struct nimera_gui_text_field *field);
int nimera_gui_text_field_self_test(void);

/* Model and pixel checks used by make run-gui-runtime. */
int nimera_gui_runtime_self_test(void);

#endif
