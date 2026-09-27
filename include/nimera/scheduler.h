#ifndef NIMERA_SCHEDULER_H
#define NIMERA_SCHEDULER_H

#include <nimera/types.h>

enum thread_state {
	THREAD_RUNNING,
	THREAD_READY,
	THREAD_WAITING,
	THREAD_TERMINATED
};

struct irq_frame {
	u64 x[31];
	u64 reserved0;
	u64 elr;
	u64 spsr;
	u64 sp_el0;
	u64 esr;
	u64 far;
	u64 reserved;
};

struct thread {
	u64 id;
	volatile enum thread_state state;
	struct irq_frame *frame;
	u64 stack_base;
	u64 stack_top;
	const char *name;
	u64 run_count;
	u64 switch_count;
};

void scheduler_init(void);
struct irq_frame *scheduler_schedule(struct irq_frame *current_frame);
void scheduler_block_current(void);
void scheduler_wake_console_input(void);
int scheduler_console_waiting(void);
unsigned int scheduler_thread_count(void);
const struct thread *scheduler_thread(unsigned int index);
u64 scheduler_context_switches(void);
u64 scheduler_worker_counter(void);
int scheduler_worker_saw_shell_waiting(void);
int scheduler_stack_ok(void);
void scheduler_test(void);
void scheduler_enable_user_task(u64 entry, u64 stack_top, u64 argument);
struct irq_frame *scheduler_terminate_current(struct irq_frame *frame);
int scheduler_user_done(void);
long long scheduler_user_exit_status(void);
void scheduler_set_user_exit_status(long long status);
__attribute__((noreturn)) void thread_entry_returned(void);

#endif
