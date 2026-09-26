#include <nimera/types.h>

u64 arch_timer_frequency(void)
{
	u64 frequency;

	// CNTFRQ_EL0 is the architectural counter frequency in ticks per second.
	// It is a system register, not a QEMU-specific memory-mapped device.
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
	return frequency;
}

u64 arch_timer_ticks(void)
{
	u64 ticks;

	// CNTPCT_EL0 is the always-on physical counter. It only moves forward,
	// making it suitable for measuring elapsed time, not calendar time.
	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
	return ticks;
}
