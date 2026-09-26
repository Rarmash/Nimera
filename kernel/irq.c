#include <nimera/irq.h>
#include <nimera/console.h>
#include <nimera/format.h>
#include <nimera/panic.h>

extern void platform_gic_init(const struct irq_platform_info *info);
extern u64 platform_gic_acknowledge(void);
extern void platform_gic_end(u64 interrupt_id);
extern void uart_init(u64 base);
extern void uart_enable_rx_interrupt(void);
extern void uart_handle_irq(void);
extern u64 uart_rx_irq_count(void);
extern u64 uart_dropped_bytes(void);

static struct irq_platform_info platform_info;
static volatile u64 timer_irq_count;

void irq_init(void)
{
	platform_info = irq_platform_discover();
	uart_init(platform_info.uart_base);
	platform_gic_init(&platform_info);
	arch_timer_irq_init();
	uart_enable_rx_interrupt();
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
	if (interrupt_id == platform_info.timer_intid) {
		++timer_irq_count;
		arch_timer_irq_rearm();
		platform_gic_end(interrupt_id);
		return;
	}
	if (interrupt_id == platform_info.uart_intid) {
		uart_handle_irq();
		platform_gic_end(interrupt_id);
		return;
	}
	console_write("Unexpected GIC interrupt: ");
	format_u64_decimal(interrupt_id);
	console_write("\r\n");
	panic("unexpected GIC interrupt");
}

u64 irq_uart_count(void)
{
	return uart_rx_irq_count();
}

u64 irq_uart_dropped_bytes(void)
{
	return uart_dropped_bytes();
}
