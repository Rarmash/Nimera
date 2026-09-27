#include <nimera/abi/syscall.h>
#include <nimera/console.h>
#include <nimera/elf.h>
#include <nimera/mmu.h>
#include <nimera/scheduler.h>
#include <nimera/syscall.h>

#define NIMERA_SYSCALL_UNSUPPORTED (-38LL)
#define NIMERA_SYSCALL_FAULT (-14LL)

static long long syscall_write_console(struct irq_frame *frame)
{
	u64 address = frame->x[0];
	u64 length = frame->x[1];

	if (mmu_user_readable_range(address, length) == 0) {
		return NIMERA_SYSCALL_FAULT;
	}
	for (u64 index = 0ULL; index < length; ++index) {
		console_putc(*(const char *)(unsigned long)(address + index));
	}
	return (long long)length;
}

struct irq_frame *syscall_handle(struct irq_frame *frame)
{
	switch (frame->x[8]) {
	case NIMERA_SYS_WRITE_CONSOLE:
		frame->x[0] = (u64)syscall_write_console(frame);
		return frame;
	case NIMERA_SYS_EXIT:
		scheduler_set_user_exit_status((long long)frame->x[0]);
		{
			struct irq_frame *next = scheduler_terminate_current(frame);
			if (elf_user_task_active() != 0) {
				elf_user_task_finished();
				scheduler_release_user_task();
			}
			return next;
		}
	default:
		frame->x[0] = (u64)NIMERA_SYSCALL_UNSUPPORTED;
		return frame;
	}
}
