#ifndef NIMERA_RENDER_STATS_H
#define NIMERA_RENDER_STATS_H

#include <nimera/types.h>

struct render_stats {
	u64 glyph_draws;
	u64 dirty_marks;
	u64 compositor_composes;
	u64 display_flushes;
	u64 gpu_transfers;
	u64 gpu_flushes;
};

void render_stats_reset(void);
void render_stats_snapshot(struct render_stats *stats);
void render_stats_glyph_draw(void);
void render_stats_dirty_mark(void);
void render_stats_compositor_compose(void);
void render_stats_display_flush(void);
void render_stats_gpu_transfer(void);
void render_stats_gpu_flush(void);

#endif
