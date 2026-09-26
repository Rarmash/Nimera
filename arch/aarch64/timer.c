#include <nimera/types.h>
#include <nimera/irq.h>
#include <nimera/panic.h>

static u64 irq_period_ticks;
static u64 irq_next_deadline;

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

void arch_timer_irq_init(void)
{
	u64 frequency = arch_timer_frequency();

	if (frequency == 0ULL) {
		panic("AArch64 timer IRQ frequency is zero");
	}
	irq_period_ticks = frequency / 10ULL;
	if (irq_period_ticks == 0ULL) {
		irq_period_ticks = 1ULL;
	}
	irq_next_deadline = arch_timer_ticks() + irq_period_ticks;
	__asm__ volatile("msr cntp_cval_el0, %0" :: "r"(irq_next_deadline)
				 : "memory");
	__asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(1ULL) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

void arch_timer_irq_rearm(void)
{
	u64 now = arch_timer_ticks();

	if (irq_period_ticks == 0ULL) {
		return;
	}
	do {
		irq_next_deadline += irq_period_ticks;
	} while (irq_next_deadline <= now);
	__asm__ volatile("msr cntp_cval_el0, %0" :: "r"(irq_next_deadline)
				 : "memory");
}

void arch_timer_irq_stop(void)
{
	__asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(0ULL) : "memory");
	__asm__ volatile("isb" ::: "memory");
}
