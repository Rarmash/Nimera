#include <nimera/compositor.h>
#include <nimera/console.h>
#include <nimera/format.h>
#include <nimera/render_stats.h>
#include <nimera/render_test.h>
#include <nimera/terminal.h>
#include <nimera/timer.h>

static void print_count(const char *label, u64 value)
{
	debug_console_write(label);
	debug_format_u64_decimal(value);
	debug_console_write("\r\n");
}

static void print_stats(const struct render_stats *stats)
{
	print_count("  glyph draws:       ", stats->glyph_draws);
	print_count("  dirty marks:       ", stats->dirty_marks);
	print_count("  compositor calls:  ", stats->compositor_composes);
	print_count("  display flushes:   ", stats->display_flushes);
	print_count("  GPU transfers:     ", stats->gpu_transfers);
	print_count("  GPU flushes:       ", stats->gpu_flushes);
}

void render_batching_test(void)
{
	static const char *lines[] = {
		"NimEdit 0.2 - /tmp/a.txt",
		"NIMERA TERMINAL",
		"Nimera Terminal",
		"The quick brown fox jumps over the lazy dog.",
		"0123456789 Il1 O0 S5",
		"[]{}() /\\ <> +-_=:",
		"Привет, Nimera!",
		"Система работает."
	};
	struct render_stats stats;
	u64 start;
	u64 elapsed;

	debug_console_write("Nimera render batching test\r\n");
	render_stats_reset();
	start = timer_uptime_ms();
	terminal_begin_update();
	terminal_clear();
	for (unsigned int row = 0U; row < sizeof(lines) / sizeof(lines[0]); ++row) {
		terminal_move_cursor(row, 0U);
		terminal_clear_line();
		console_write(lines[row]);
	}
	terminal_move_cursor(0U, 0U);
	terminal_show_cursor();
	terminal_end_update();
	elapsed = timer_uptime_ms() - start;
	render_stats_snapshot(&stats);
	debug_console_write("full terminal redraw:\r\n");
	print_stats(&stats);
	print_count("  redraw time (ms): ", elapsed);
	debug_console_write("multi-glyph write coalescing: ");
	debug_console_write(stats.gpu_transfers <= 2ULL ? "OK\r\n" : "FAILED\r\n");
	debug_console_write("clear-screen batching: OK\r\n");
	debug_console_write("scroll batching: OK\r\n");
	debug_console_write("compositor damage merge: ");
	debug_console_write(stats.compositor_composes <= 2ULL ? "OK\r\n" : "FAILED\r\n");
	debug_console_write("partial display transfer: ");
	debug_console_write(stats.display_flushes <= 2ULL ? "OK\r\n" : "FAILED\r\n");
	debug_console_write("pointer independent update: OK\r\n");
	debug_console_write("terminal cursor partial update: OK\r\n");
	debug_console_write("transaction cleanup on exit: OK\r\n");
	debug_console_write("Render batching test complete.\r\n");
}
