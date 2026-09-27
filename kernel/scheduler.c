#include <nimera/console.h>
#include <nimera/format.h>
#include <nimera/irq.h>
#include <nimera/panic.h>
#include <nimera/scheduler.h>
#include <nimera/timer.h>

#define MAX_THREADS 3U
#define WORKER_STACK_SIZE (16U * 1024U)
#define STACK_CANARY 0x4e494d4552415354ULL

static struct thread threads[MAX_THREADS];
static unsigned int current_thread;
static u64 context_switches;
static volatile u64 worker_counter;
static volatile u64 worker_saw_shell_waiting;
static unsigned char worker_stack[WORKER_STACK_SIZE]
	__attribute__((aligned(4096)));
static unsigned char user_kernel_stack[WORKER_STACK_SIZE]
	__attribute__((aligned(4096)));
static unsigned int user_enabled;
static long long user_exit_status;

static void worker_entry(void *argument)
{
	(void)argument;
	for (;;) {
		++worker_counter;
		if (threads[0].state == THREAD_WAITING) {
			worker_saw_shell_waiting = 1ULL;
		}
	}
}

extern void thread_bootstrap(void);

static void make_worker_frame(void)
{
	struct irq_frame *frame = (struct irq_frame *)(void *)
		(worker_stack + WORKER_STACK_SIZE - sizeof(struct irq_frame));

	frame->x[0] = 0ULL;
	frame->x[1] = (u64)(unsigned long)worker_entry;
	frame->x[30] = 0ULL;
	frame->elr = (u64)(unsigned long)thread_bootstrap;
	frame->sp_el0 = 0ULL;
	/* EL1h with IRQs unmasked; the first return enters the bootstrap. */
	frame->spsr = 0x5ULL;
	threads[1].frame = frame;
}

void scheduler_init(void)
{
	*(u64 *)(void *)worker_stack = STACK_CANARY;
	threads[0].id = 0ULL;
	threads[0].state = THREAD_RUNNING;
	threads[0].frame = (struct irq_frame *)0;
	threads[0].stack_base = 0ULL;
	threads[0].stack_top = 0ULL;
	threads[0].name = "shell";
	threads[0].run_count = 0ULL;
	threads[0].switch_count = 0ULL;
	threads[1].id = 1ULL;
	threads[1].state = THREAD_READY;
	threads[1].stack_base = (u64)(unsigned long)worker_stack;
	threads[1].stack_top = threads[1].stack_base + WORKER_STACK_SIZE;
	threads[1].name = "worker";
	threads[1].run_count = 0ULL;
	threads[1].switch_count = 0ULL;
	threads[2].id = 2ULL;
	threads[2].state = THREAD_TERMINATED;
	threads[2].frame = (struct irq_frame *)0;
	threads[2].stack_base = (u64)(unsigned long)user_kernel_stack;
	threads[2].stack_top = threads[2].stack_base + WORKER_STACK_SIZE;
	threads[2].name = "user-test";
	threads[2].run_count = 0ULL;
	threads[2].switch_count = 0ULL;
	make_worker_frame();
	current_thread = 0U;
	context_switches = 0ULL;
	worker_counter = 0ULL;
	worker_saw_shell_waiting = 0ULL;
	user_enabled = 0U;
	user_exit_status = -1LL;
}

static int valid_frame(const struct thread *thread, struct irq_frame *frame)
{
	u64 address = (u64)(unsigned long)frame;

	if (thread->id == 0ULL) {
		return address != 0ULL && (address & 15ULL) == 0ULL;
	}
	return address >= thread->stack_base &&
	       address + sizeof(struct irq_frame) <= thread->stack_top &&
	       (address & 15ULL) == 0ULL;
}

struct irq_frame *scheduler_schedule(struct irq_frame *current_frame)
{
	unsigned int offset;
	unsigned int next = current_thread;
	u64 irq_state = irq_save_disable();

	if (current_thread >= MAX_THREADS || current_frame == (struct irq_frame *)0) {
		irq_restore(irq_state);
		panic("invalid current scheduler thread");
	}
	threads[current_thread].frame = current_frame;
	if (threads[current_thread].state == THREAD_RUNNING) {
		threads[current_thread].state = THREAD_READY;
	}
	for (offset = 1U; offset <= MAX_THREADS; ++offset) {
		unsigned int candidate = (current_thread + offset) % MAX_THREADS;

		if (threads[candidate].state == THREAD_READY &&
		    valid_frame(&threads[candidate], threads[candidate].frame)) {
			next = candidate;
			break;
		}
	}
	if (next == current_thread &&
	    threads[current_thread].state != THREAD_READY) {
		/* No runnable thread exists. Keep the current frame as a safe idle
		 * fallback; a UART or timer IRQ can make work runnable again. */
		irq_restore(irq_state);
		return current_frame;
	}
	if (next == current_thread && threads[current_thread].state == THREAD_READY) {
		threads[current_thread].state = THREAD_RUNNING;
		irq_restore(irq_state);
		return current_frame;
	}
	current_thread = next;
	threads[current_thread].state = THREAD_RUNNING;
	++threads[current_thread].run_count;
	++threads[current_thread].switch_count;
	++context_switches;
	irq_restore(irq_state);
	return threads[current_thread].frame;
}

void scheduler_enable_user_task(u64 entry, u64 stack_top, u64 argument)
{
	struct irq_frame *frame = (struct irq_frame *)(void *)
		(user_kernel_stack + WORKER_STACK_SIZE - sizeof(struct irq_frame));

	if (entry == 0ULL || stack_top == 0ULL || user_enabled != 0U) {
		panic("invalid user task setup");
	}
	for (unsigned int index = 0U; index < 31U; ++index) frame->x[index] = 0ULL;
	frame->x[0] = argument;
	frame->x[30] = 0ULL;
	frame->elr = entry;
	frame->spsr = 0ULL; /* EL0t with interrupts unmasked. */
	frame->sp_el0 = stack_top;
	frame->esr = 0ULL;
	frame->far = 0ULL;
	threads[2].frame = frame;
	threads[2].state = THREAD_READY;
	user_enabled = 1U;
}

struct irq_frame *scheduler_terminate_current(struct irq_frame *frame)
{
	if (current_thread != 2U || user_enabled == 0U) {
		panic("non-user task attempted termination");
	}
	threads[current_thread].frame = frame;
	threads[current_thread].state = THREAD_TERMINATED;
	scheduler_wake_console_input();
	return scheduler_schedule(frame);
}

int scheduler_user_done(void)
{
	return user_enabled != 0U && threads[2].state == THREAD_TERMINATED;
}

long long scheduler_user_exit_status(void)
{
	return user_exit_status;
}

void scheduler_set_user_exit_status(long long status)
{
	user_exit_status = status;
}

void scheduler_release_user_task(void)
{
	if (current_thread == 2U) panic("released running user task");
	threads[2].frame = (struct irq_frame *)0;
	threads[2].state = THREAD_TERMINATED;
	user_enabled = 0U;
}

unsigned int scheduler_thread_count(void)
{
	return user_enabled != 0U ? MAX_THREADS : 2U;
}

void scheduler_block_current(void)
{
	if (current_thread != 0U) {
		panic("non-console thread attempted block");
	}
	/* The shell may be transiently READY when a UART IRQ woke it before
	 * the interrupted instruction resumed. Re-enter WAITING atomically. */
	threads[0].state = THREAD_WAITING;
}

void scheduler_wake_console_input(void)
{
	u64 irq_state = irq_save_disable();

	if (threads[0].state == THREAD_WAITING) {
		threads[0].state = current_thread == 0U ? THREAD_RUNNING : THREAD_READY;
	}
	irq_restore(irq_state);
}

int scheduler_console_waiting(void)
{
	return threads[0].state == THREAD_WAITING;
}

const struct thread *scheduler_thread(unsigned int index)
{
	if (index >= MAX_THREADS) {
		return (const struct thread *)0;
	}
	return &threads[index];
}

u64 scheduler_context_switches(void)
{
	return context_switches;
}

u64 scheduler_worker_counter(void)
{
	return worker_counter;
}

int scheduler_worker_saw_shell_waiting(void)
{
	return worker_saw_shell_waiting != 0ULL;
}

int scheduler_stack_ok(void)
{
	return *(const u64 *)(const void *)worker_stack == STACK_CANARY &&
	       valid_frame(&threads[1], threads[1].frame);
}

void scheduler_test(void)
{
	volatile u64 local_a = 0x1111222233334444ULL;
	volatile u64 local_b = 0xaaaabbbbccccddddULL;
	u64 before = scheduler_worker_counter();
	u64 switches_before = scheduler_context_switches();
	u64 deadline = timer_ticks() + timer_frequency() * 2ULL;

	console_write("Nimera scheduler test\r\nThreads: 2\r\n");
	console_write("Timer preemption: enabled\r\nWorker counter before: ");
	format_u64_decimal(before);
	console_write("\r\n");
	while (timer_ticks() < deadline) {
		if (local_a != 0x1111222233334444ULL ||
		    local_b != 0xaaaabbbbccccddddULL) {
			panic("scheduler context integrity failed");
		}
	}
	console_write("Worker counter after: ");
	format_u64_decimal(scheduler_worker_counter());
	console_write("\r\nContext switches: ");
	format_u64_decimal(scheduler_context_switches() - switches_before);
	console_write("\r\nContext integrity: OK\r\nStack canary: ");
	console_write(scheduler_stack_ok() != 0 ? "OK\r\n" : "FAILED\r\n");
	if (scheduler_worker_counter() <= before ||
	    scheduler_context_switches() == switches_before ||
	    scheduler_stack_ok() == 0) {
		panic("scheduler preemption test failed");
	}
	irq_disable();
	arch_timer_irq_stop();
	console_write("Scheduler test complete.\r\n");
}

__attribute__((noreturn))
void thread_entry_returned(void)
{
	panic("kernel thread returned");
}
