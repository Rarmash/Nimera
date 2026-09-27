#ifndef NIMERA_SCHEDULER_H
#define NIMERA_SCHEDULER_H

#include <nimera/types.h>

struct process;

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
	struct process *process;
};

void scheduler_init(void);
struct irq_frame *scheduler_schedule(struct irq_frame *current_frame);
void scheduler_block_current(void);
void scheduler_block_input_current(void);
void scheduler_wake_input_waiter(void);
struct irq_frame *scheduler_block_current_thread(struct irq_frame *frame);
void scheduler_wake_thread(unsigned int index);
unsigned int scheduler_current_thread_id(void);
int scheduler_input_waiting(void);
unsigned int scheduler_thread_count(void);
const struct thread *scheduler_thread(unsigned int index);
u64 scheduler_context_switches(void);
u64 scheduler_worker_counter(void);
int scheduler_worker_saw_shell_waiting(void);
int scheduler_stack_ok(void);
void scheduler_test(void);
void scheduler_enable_user_task(u64 entry, u64 stack_top, u64 argument);
void scheduler_enable_user_task_argv(u64 entry, u64 stack_top, u64 argc,
				     u64 argv);
void scheduler_enable_user_task_for_process(struct process *process, u64 entry,
				     u64 stack_top, u64 argc, u64 argv);
struct irq_frame *scheduler_terminate_current(struct irq_frame *frame);
struct process *scheduler_current_process(void);
void scheduler_release_process(struct process *process);
int scheduler_user_done(void);
long long scheduler_user_exit_status(void);
void scheduler_set_user_exit_status(long long status);
void scheduler_release_user_task(void);
__attribute__((noreturn)) void thread_entry_returned(void);

#endif
