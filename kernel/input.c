#include <nimera/input.h>
#include <nimera/irq.h>
#include <nimera/scheduler.h>

#define INPUT_QUEUE_CAPACITY 128U

static struct key_event queue[INPUT_QUEUE_CAPACITY];
static unsigned int read_index;
static unsigned int write_index;
static volatile u64 dropped_events;
static int hardware_available;
static struct pointer_event pointer_queue[INPUT_QUEUE_CAPACITY];
static unsigned int pointer_read_index;
static unsigned int pointer_write_index;
static volatile u64 dropped_pointer_events;

static struct key_event event(enum key_code code, char ch, unsigned int ctrl)
{
	struct key_event result = {code, ch, ctrl};
	return result;
}

void input_init(void)
{
	read_index = 0U;
	write_index = 0U;
	dropped_events = 0ULL;
	pointer_read_index = 0U;
	pointer_write_index = 0U;
	dropped_pointer_events = 0ULL;
	hardware_available = 0;
}

int input_push_event(struct key_event value)
{
	u64 irq_state = irq_save_disable();
	unsigned int next = (write_index + 1U) % INPUT_QUEUE_CAPACITY;

	if (next == read_index) {
		++dropped_events;
		irq_restore(irq_state);
		return 0;
	}
	queue[write_index] = value;
	__asm__ volatile("dmb ish" ::: "memory");
	write_index = next;
	irq_restore(irq_state);
	scheduler_wake_input_waiter();
	arch_signal_event();
	return 1;
}

int input_try_get_event(struct key_event *result)
{
	u64 irq_state = irq_save_disable();

	if (read_index == write_index) {
		irq_restore(irq_state);
		return 0;
	}
	*result = queue[read_index];
	__asm__ volatile("dmb ish" ::: "memory");
	read_index = (read_index + 1U) % INPUT_QUEUE_CAPACITY;
	irq_restore(irq_state);
	return 1;
}

int input_push_pointer_event(struct pointer_event value)
{
	u64 irq_state = irq_save_disable();
	unsigned int next = (pointer_write_index + 1U) % INPUT_QUEUE_CAPACITY;

	if (next == pointer_read_index) {
		++dropped_pointer_events;
		irq_restore(irq_state);
		return 0;
	}
	pointer_queue[pointer_write_index] = value;
	__asm__ volatile("dmb ish" ::: "memory");
	pointer_write_index = next;
	irq_restore(irq_state);
	scheduler_wake_input_waiter();
	arch_signal_event();
	return 1;
}

int input_try_get_pointer_event(struct pointer_event *result)
{
	u64 irq_state = irq_save_disable();

	if (pointer_read_index == pointer_write_index) {
		irq_restore(irq_state);
		return 0;
	}
	*result = pointer_queue[pointer_read_index];
	__asm__ volatile("dmb ish" ::: "memory");
	pointer_read_index = (pointer_read_index + 1U) % INPUT_QUEUE_CAPACITY;
	irq_restore(irq_state);
	return 1;
}

void input_wait_for_activity(void)
{
	for (;;) {
		u64 irq_state = irq_save_disable();

		if (read_index != write_index ||
			pointer_read_index != pointer_write_index) {
			irq_restore(irq_state);
			return;
		}
		/* The activity check and WAITING transition are one IRQ-disabled step. */
		scheduler_block_input_current();
		irq_restore(irq_state);
		arch_wait_for_event();
	}
}

struct key_event input_read_event(void)
{
	struct key_event result;

	for (;;) {
		u64 irq_state = irq_save_disable();
		if (read_index != write_index) {
			result = queue[read_index];
			__asm__ volatile("dmb ish" ::: "memory");
			read_index = (read_index + 1U) % INPUT_QUEUE_CAPACITY;
			irq_restore(irq_state);
			return result;
		}
		/* The empty check and WAITING transition are one IRQ-disabled step. */
		scheduler_block_input_current();
		irq_restore(irq_state);
		arch_wait_for_event();
	}
}

void input_set_hardware_available(int available)
{
	hardware_available = available != 0 ? 1 : 0;
}

int input_hardware_available(void) { return hardware_available; }
u64 input_dropped_events(void) { return dropped_events; }

int input_self_test(void)
{
	struct key_event result;
	input_init();
	if (input_push_event(event(KEY_CHAR, 'a', 0U)) == 0 ||
		input_try_get_event(&result) == 0 || result.code != KEY_CHAR ||
		result.ch != 'a' || result.ctrl != 0U) return 0;
	return 1;
}
