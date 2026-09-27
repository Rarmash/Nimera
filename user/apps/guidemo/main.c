#include <nimera/gui.h>

static void make_greeting(char *output, const struct nimera_gui_text_field *field)
{
	unsigned int index = 0U;
	const char prefix[] = "Hello, ";
	for (unsigned int i = 0U; prefix[i] != '\0'; ++i) output[index++] = prefix[i];
	for (u64 i = 0ULL; i < field->length; ++i) output[index++] = field->text[i];
	output[index++] = '!'; output[index] = '\0';
}

static void draw_ui(struct nimera_gui_window *window, struct nimera_gui_text_field *name,
	struct nimera_gui_button *greet, struct nimera_gui_button *reset,
	const char *greeting, unsigned int full)
{
	if (full != 0U) nimera_gui_clear(&window->canvas, NIMERA_GUI_COLOR_BACKGROUND);
	nimera_gui_fill_rect(&window->canvas, (struct nimera_gui_rect){16, 14,
		window->canvas.width > 32ULL ? window->canvas.width - 32ULL : 0ULL, 112ULL}, NIMERA_GUI_COLOR_PANEL);
	nimera_gui_draw_rect(&window->canvas, (struct nimera_gui_rect){16, 14,
		window->canvas.width > 32ULL ? window->canvas.width - 32ULL : 0ULL, 112ULL}, NIMERA_GUI_COLOR_BORDER);
	nimera_gui_draw_text(&window->canvas, 24, 22, "Nimera GUI Demo", NIMERA_GUI_COLOR_TEXT);
	nimera_gui_draw_text(&window->canvas, 24, 48, "Name:", NIMERA_GUI_COLOR_TEXT);
	nimera_gui_text_field_draw(&window->canvas, name);
	nimera_gui_draw_text(&window->canvas, 24, 94, greeting, NIMERA_GUI_COLOR_ACCENT);
	nimera_gui_button_draw(&window->canvas, greet);
	nimera_gui_button_draw(&window->canvas, reset);
}

static void present_full(struct nimera_gui_window *window)
{ (void)nimera_gui_window_present(window, (const struct nimera_gui_damage *)0); }

int main(int argc, char **argv)
{
	struct nimera_gui_window window;
	struct nimera_window_event event;
	struct nimera_gui_focus focus;
	struct nimera_gui_text_field name;
	struct nimera_gui_button greet = {{24, 142, 116ULL, 34ULL}, "Greet", 0U, 0U};
	struct nimera_gui_button reset = {{154, 142, 106ULL, 34ULL}, "Reset", 0U, 0U};
	char name_buffer[256];
	char greeting[272];
	(void)argc; (void)argv;
	if (nimera_gui_window_create(&window, 520U, 220U, "Nimera GUI Demo", 16ULL,
		NIMERA_WINDOW_CLOSABLE | NIMERA_WINDOW_RESIZABLE) != 0) return 1;
	if (nimera_gui_text_field_init(&name, (struct nimera_gui_rect){92, 42, 250ULL, 26ULL},
		name_buffer, sizeof(name_buffer), "") < 0) { nimera_gui_window_destroy(&window); return 1; }
	nimera_gui_focus_init(&focus); make_greeting(greeting, &name);
	draw_ui(&window, &name, &greet, &reset, greeting, 1U); present_full(&window);
	for (;;) {
		enum nimera_gui_text_field_result text_result = NIMERA_GUI_TEXT_NONE;
		unsigned int clicked = 0U, changed = 0U;
		if (nimera_gui_window_next_event(&window, &event) != 0) break;
		if (event.type == NIMERA_WINDOW_EVENT_CLOSE_REQUEST) break;
		if (event.type == NIMERA_WINDOW_KEY &&
			(event.ch == (u32)'q' || event.ch == (u32)'Q' || event.ch == 27U)) break;
		changed |= (unsigned int)nimera_gui_text_field_handle_event(&name, &focus, &event, &text_result);
		if (text_result == NIMERA_GUI_TEXT_CHANGED) {
			nimera_gui_text_field_draw(&window.canvas, &name);
			struct nimera_gui_damage damage;
			nimera_gui_damage_reset(&damage); nimera_gui_damage_add(&damage, name.rect);
			(void)nimera_gui_window_present(&window, &damage); continue;
		}
		changed |= (unsigned int)nimera_gui_button_handle_event(&greet, &event, &clicked);
		if (clicked != 0U) {
			make_greeting(greeting, &name); draw_ui(&window, &name, &greet, &reset, greeting, 1U); present_full(&window); continue;
		}
		changed |= (unsigned int)nimera_gui_button_handle_event(&reset, &event, &clicked);
		if (clicked != 0U) {
			(void)nimera_gui_text_field_init(&name, name.rect, name_buffer, sizeof(name_buffer), "");
			nimera_gui_text_field_set_focus(&name, &focus, 0U);
			make_greeting(greeting, &name); draw_ui(&window, &name, &greet, &reset, greeting, 1U); present_full(&window); continue;
		}
		if (event.type == NIMERA_WINDOW_EVENT_RESIZED) {
			name.rect.width = window.canvas.width > 120ULL ? window.canvas.width - 120ULL : 80ULL;
			greet.rect.y = window.canvas.height > 190ULL ? (long long)window.canvas.height - 78LL : 80LL;
			reset.rect.y = greet.rect.y;
			draw_ui(&window, &name, &greet, &reset, greeting, 1U); present_full(&window); continue;
		}
		if (changed != 0U) { draw_ui(&window, &name, &greet, &reset, greeting, 0U); present_full(&window); }
	}
	nimera_gui_window_destroy(&window); return 0;
}
