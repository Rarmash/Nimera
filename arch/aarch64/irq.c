#include <nimera/irq.h>

void arch_irq_enable(void)
{
	__asm__ volatile("msr daifclr, #2" ::: "memory");
}

void arch_irq_disable(void)
{
	__asm__ volatile("msr daifset, #2" ::: "memory");
}

void arch_wait_for_event(void)
{
	__asm__ volatile("wfe" ::: "memory");
}
