#ifndef NIMERA_IRQ_H
#define NIMERA_IRQ_H

#include <nimera/types.h>

struct irq_platform_info {
	u64 gic_distributor_base;
	u64 gic_distributor_size;
	u64 gic_cpu_base;
	u64 gic_cpu_size;
	u64 uart_base;
	u64 uart_size;
	u64 uart_intid;
	u64 timer_intid;
};

struct irq_platform_info irq_platform_discover(void);

void irq_init(void);
void irq_enable(void);
void irq_disable(void);
u64 irq_timer_ticks(void);
u64 irq_uart_count(void);
u64 irq_uart_dropped_bytes(void);
void irq_handle(void);

void arch_irq_enable(void);
void arch_irq_disable(void);
void arch_wait_for_event(void);
void arch_signal_event(void);
void arch_timer_irq_init(void);
void arch_timer_irq_rearm(void);
void arch_timer_irq_stop(void);

#endif
