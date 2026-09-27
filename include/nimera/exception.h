#ifndef NIMERA_EXCEPTION_H
#define NIMERA_EXCEPTION_H

#include <nimera/types.h>

void exception_init(void);
u64 exception_current_el(void);
__attribute__((noreturn)) void exception_test_trigger(void);

__attribute__((noreturn)) void exception_fatal(u64 type, u64 esr, u64 elr,
						 u64 far, u64 current_el);
struct irq_frame;
struct irq_frame *exception_sync_handle(struct irq_frame *frame);

#endif
