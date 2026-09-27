#include <nimera/gui.h>

static void decimal(char *buffer, unsigned long long value)
{
	char reversed[24]; unsigned int count = 0U;
	if (value == 0ULL) { buffer[0] = '0'; buffer[1] = '\0'; return; }
	while (value != 0ULL) { reversed[count++] = (char)('0' + value % 10ULL); value /= 10ULL; }
	for (unsigned int i = 0U; i < count; ++i) buffer[i] = reversed[count - i - 1U];
	buffer[count] = '\0';
}

static void draw_ui(struct nimera_gui_window *window, struct nimera_gui_button *increment,
	struct nimera_gui_button *reset, unsigned long long counter, unsigned int full)
{
	char count_text[24]; struct nimera_gui_label title = {{24, 22, 300ULL, 20ULL}, "Nimera GUI"};
	struct nimera_gui_label hello = {{24, 52, 300ULL, 20ULL}, "Hello from userspace."};
	struct nimera_gui_label counter_label = {{24, 88, 300ULL, 20ULL}, "Counter:"};
	decimal(count_text, counter);
	if (full != 0U) nimera_gui_clear(&window->canvas, NIMERA_GUI_COLOR_BACKGROUND);
	nimera_gui_fill_rect(&window->canvas, (struct nimera_gui_rect){16, 14, window->canvas.width - 32ULL, 112ULL}, NIMERA_GUI_COLOR_PANEL);
	nimera_gui_draw_rect(&window->canvas, (struct nimera_gui_rect){16, 14, window->canvas.width - 32ULL, 112ULL}, NIMERA_GUI_COLOR_BORDER);
	nimera_gui_label_draw(&window->canvas, &title, NIMERA_GUI_COLOR_TEXT);
	nimera_gui_label_draw(&window->canvas, &hello, NIMERA_GUI_COLOR_TEXT);
	nimera_gui_label_draw(&window->canvas, &counter_label, NIMERA_GUI_COLOR_TEXT);
	nimera_gui_draw_text(&window->canvas, 104, 88, count_text, NIMERA_GUI_COLOR_ACCENT);
	nimera_gui_button_draw(&window->canvas, increment);
	nimera_gui_button_draw(&window->canvas, reset);
}

int main(int argc, char **argv)
{
	struct nimera_gui_window window; struct nimera_window_event event;
	struct nimera_gui_button increment = {{24, 142, 148ULL, 34ULL}, "Increment", 0U, 0U};
	struct nimera_gui_button reset = {{190, 142, 112ULL, 34ULL}, "Reset", 0U, 0U};
	unsigned long long counter = 0ULL; (void)argc; (void)argv;
	if (nimera_gui_window_create(&window, 520U, 220U, "Nimera GUI Demo", 16ULL,
		NIMERA_WINDOW_CLOSABLE | NIMERA_WINDOW_RESIZABLE) != 0) return 1;
	draw_ui(&window, &increment, &reset, counter, 1U);
	if (nimera_gui_window_present(&window, (const struct nimera_gui_damage *)0) != 0) { nimera_gui_window_destroy(&window); return 1; }
	for (;;) {
		unsigned int clicked = 0U, changed = 0U;
		if (nimera_gui_window_next_event(&window, &event) != 0) break;
		if (event.type == NIMERA_WINDOW_EVENT_CLOSE_REQUEST) break;
		if (event.type == NIMERA_WINDOW_KEY && (event.ch == (u32)'q' || event.ch == (u32)'Q' || event.ch == 27U)) break;
		changed |= (unsigned int)nimera_gui_button_handle_event(&increment, &event, &clicked);
		if (clicked != 0U) { ++counter; draw_ui(&window, &increment, &reset, counter, 0U); (void)nimera_gui_window_present(&window, (const struct nimera_gui_damage *)0); continue; }
		changed |= (unsigned int)nimera_gui_button_handle_event(&reset, &event, &clicked);
		if (clicked != 0U) { counter = 0ULL; draw_ui(&window, &increment, &reset, counter, 0U); (void)nimera_gui_window_present(&window, (const struct nimera_gui_damage *)0); continue; }
		if (event.type == NIMERA_WINDOW_EVENT_RESIZED) { increment.rect.y = window.canvas.height > 190ULL ? (long long)window.canvas.height - 78LL : 112LL; reset.rect.y = increment.rect.y; draw_ui(&window, &increment, &reset, counter, 1U); (void)nimera_gui_window_present(&window, (const struct nimera_gui_damage *)0); continue; }
		if (changed != 0U) { draw_ui(&window, &increment, &reset, counter, 0U); (void)nimera_gui_window_present(&window, (const struct nimera_gui_damage *)0); }
	}
	nimera_gui_window_destroy(&window); return 0;
}
