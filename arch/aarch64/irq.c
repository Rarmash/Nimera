#include <nimera/irq.h>

void arch_irq_enable(void)
{
	__asm__ volatile("msr daifclr, #2" ::: "memory");
}

void arch_irq_disable(void)
{
	__asm__ volatile("msr daifset, #2" ::: "memory");
}

u64 arch_irq_save_disable(void)
{
	u64 state;

	__asm__ volatile("mrs %0, daif\n\tmsr daifset, #2" : "=r"(state) : : "memory");
	return state;
}

void arch_irq_restore(u64 state)
{
	__asm__ volatile("msr daif, %0\n\tisb" :: "r"(state) : "memory");
}

void arch_wait_for_event(void)
{
	__asm__ volatile("wfe" ::: "memory");
}

void arch_signal_event(void)
{
	__asm__ volatile("sev" ::: "memory");
}
