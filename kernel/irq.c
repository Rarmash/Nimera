#include <nimera/irq.h>
#include <nimera/console.h>
#include <nimera/format.h>
#include <nimera/panic.h>
#include <nimera/scheduler.h>
#include <nimera/input.h>
#include <nimera/virtio.h>

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
static volatile u64 user_preemption_count;

void irq_init(void)
{
	platform_info = irq_platform_discover();
	uart_init(platform_info.uart_base);
	platform_gic_init(&platform_info);
	arch_timer_irq_init();
	uart_enable_rx_interrupt();
	input_init();
	if (virtio_input_init() == 0) {
		platform_gic_enable_interrupt(virtio_input_interrupt());
		if (virtio_pointer_available() != 0)
			platform_gic_enable_interrupt(virtio_pointer_interrupt());
		input_set_hardware_available(1);
	}
	timer_irq_count = 0ULL;
	user_preemption_count = 0ULL;
}

void irq_enable(void)
{
	arch_irq_enable();
}

void irq_disable(void)
{
	arch_irq_disable();
}

u64 irq_save_disable(void)
{
	return arch_irq_save_disable();
}

void irq_restore(u64 state)
{
	arch_irq_restore(state);
}

u64 irq_timer_ticks(void)
{
	return timer_irq_count;
}

struct irq_frame *irq_handle(struct irq_frame *frame)
{
	u64 interrupt_id = platform_gic_acknowledge();

	if (interrupt_id == 1023ULL) {
		return frame;
	}
	if (interrupt_id == platform_info.timer_intid) {
		++timer_irq_count;
		if ((frame->spsr & 0x0fULL) == 0ULL) ++user_preemption_count;
		arch_timer_irq_rearm();
		frame = scheduler_schedule(frame);
		platform_gic_end(interrupt_id);
		return frame;
	}
	if (interrupt_id == platform_info.uart_intid) {
		uart_handle_irq();
		platform_gic_end(interrupt_id);
		return frame;
	}
	if (virtio_input_available() != 0 &&
		interrupt_id == virtio_input_interrupt()) {
		int shell_waiting = scheduler_input_waiting();
		virtio_input_handle_irq();
		/* A key can wake the shell while the timer worker is running. */
		if (shell_waiting != 0) frame = scheduler_schedule(frame);
		platform_gic_end(interrupt_id);
		return frame;
	}
	if (virtio_pointer_available() != 0 &&
		interrupt_id == virtio_pointer_interrupt()) {
		int shell_waiting = scheduler_input_waiting();
		virtio_pointer_handle_irq();
		if (shell_waiting != 0) frame = scheduler_schedule(frame);
		platform_gic_end(interrupt_id);
		return frame;
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

u64 irq_user_preemptions(void)
{
	return user_preemption_count;
}
