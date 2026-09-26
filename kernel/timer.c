#include <nimera/panic.h>
#include <nimera/timer.h>

extern u64 arch_timer_frequency(void);
extern u64 arch_timer_ticks(void);

static u64 boot_ticks;
static unsigned int initialized;

void timer_init(void)
{
	// Validate the architectural frequency before recording Nimera's reference
	// point. A zero frequency would make every elapsed-time conversion invalid.
	(void)timer_frequency();
	boot_ticks = timer_ticks();
	initialized = 1U;
}

u64 timer_frequency(void)
{
	u64 frequency = arch_timer_frequency();

	if (frequency == 0ULL) {
		panic("AArch64 timer frequency is zero");
	}

	return frequency;
}

u64 timer_ticks(void)
{
	return arch_timer_ticks();
}

u64 timer_uptime_ms(void)
{
	u64 frequency = timer_frequency();
	u64 elapsed_ticks;
	u64 seconds;
	u64 remainder;

	if (initialized == 0U) {
		panic("timer used before timer_init");
	}

	// Unsigned subtraction preserves the elapsed distance even if the
	// hardware counter eventually wraps around.
	elapsed_ticks = timer_ticks() - boot_ticks;
	seconds = elapsed_ticks / frequency;
	remainder = elapsed_ticks % frequency;

	// Split the calculation so the full counter value is not multiplied by
	// 1000 before division. The result is uptime since timer_init().
	return seconds * 1000ULL + (remainder * 1000ULL) / frequency;
}
