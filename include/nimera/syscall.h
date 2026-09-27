#ifndef NIMERA_SYSCALL_H
#define NIMERA_SYSCALL_H

struct irq_frame;
struct irq_frame *syscall_handle(struct irq_frame *frame);

#endif
