#include <nimera/render_stats.h>

static struct render_stats stats;

void render_stats_reset(void)
{
	stats = (struct render_stats){0};
}

void render_stats_snapshot(struct render_stats *result)
{
	if (result != (struct render_stats *)0) *result = stats;
}

void render_stats_glyph_draw(void) { ++stats.glyph_draws; }
void render_stats_dirty_mark(void) { ++stats.dirty_marks; }
void render_stats_compositor_compose(void) { ++stats.compositor_composes; }
void render_stats_display_flush(void) { ++stats.display_flushes; }
void render_stats_gpu_transfer(void) { ++stats.gpu_transfers; }
void render_stats_gpu_flush(void) { ++stats.gpu_flushes; }
