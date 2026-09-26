#include <nimera/irq.h>
#include <nimera/panic.h>

extern void platform_gic_init(const struct irq_platform_info *info);
extern u64 platform_gic_acknowledge(void);
extern void platform_gic_end(u64 interrupt_id);

static struct irq_platform_info platform_info;
static volatile u64 timer_irq_count;

void irq_init(void)
{
	platform_info = irq_platform_discover();
	platform_gic_init(&platform_info);
	arch_timer_irq_init();
	timer_irq_count = 0ULL;
}

void irq_enable(void)
{
	arch_irq_enable();
}

void irq_disable(void)
{
	arch_irq_disable();
}

u64 irq_timer_ticks(void)
{
	return timer_irq_count;
}

void irq_handle(void)
{
	u64 interrupt_id = platform_gic_acknowledge();

	if (interrupt_id == 1023ULL) {
		return;
	}
	if (interrupt_id != platform_info.timer_intid) {
		panic("unexpected GIC interrupt");
	}
	++timer_irq_count;
	arch_timer_irq_rearm();
	platform_gic_end(interrupt_id);
}
