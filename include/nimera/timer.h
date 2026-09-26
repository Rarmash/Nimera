#ifndef NIMERA_TIMER_H
#define NIMERA_TIMER_H

#include <nimera/types.h>

void timer_init(void);
u64 timer_frequency(void);
u64 timer_ticks(void);
u64 timer_uptime_ms(void);

#endif
