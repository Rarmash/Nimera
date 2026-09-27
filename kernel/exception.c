#include <nimera/console.h>
#include <nimera/elf.h>
#include <nimera/exception.h>
#include <nimera/format.h>
#include <nimera/halt.h>
#include <nimera/process.h>
#include <nimera/scheduler.h>
#include <nimera/syscall.h>

static void write_exception_type(u64 type)
{
	if (type == 0ULL) {
		debug_console_write("synchronous");
	} else if (type == 1ULL) {
		debug_console_write("irq");
	} else if (type == 2ULL) {
		debug_console_write("fiq");
	} else {
		debug_console_write("serror");
	}
}

__attribute__((noreturn))
void exception_fatal(u64 type, u64 esr, u64 elr, u64 far, u64 current_el)
{
	u64 exception_class = (esr >> 26) & 0x3fULL;

	debug_console_write("Nimera exception\r\nType: ");
	write_exception_type(type);
	debug_console_write("\r\nCurrentEL: ");
	debug_format_u64_decimal(current_el);
	debug_console_write("\r\nESR: ");
	debug_format_u64_hex(esr);
	debug_console_write("\r\nELR: ");
	debug_format_u64_hex(elr);
	debug_console_write("\r\nFAR: ");
	debug_format_u64_hex(far);
	debug_console_write("\r\nEC: ");
	debug_format_u64_hex(exception_class);
	debug_console_write("\r\nSystem halted.\r\n");

	cpu_halt();
}

static void user_fault_report(u64 esr, u64 elr, u64 far)
{
	u64 exception_class = (esr >> 26) & 0x3fULL;

	console_write("User task terminated\r\nType: ");
	if (exception_class == 0x20ULL || exception_class == 0x21ULL) {
		console_write("Instruction Abort from EL0");
	} else if (exception_class == 0x24ULL || exception_class == 0x25ULL) {
		console_write("Data Abort from EL0");
	} else {
		console_write("synchronous exception from EL0");
	}
	console_write("\r\nELR: "); format_u64_hex(elr);
	console_write("\r\nFAR: "); format_u64_hex(far);
	console_write("\r\nESR: "); format_u64_hex(esr);
	console_write("\r\n");
}

struct irq_frame *exception_sync_handle(struct irq_frame *frame)
{
	u64 exception_class = (frame->esr >> 26) & 0x3fULL;

	/* EL0t is the only user state created by this milestone. */
	if ((frame->spsr & 0x0fULL) != 0ULL) {
		exception_fatal(0ULL, frame->esr, frame->elr, frame->far, 1ULL);
	}
	if (exception_class == 0x15ULL) {
		return syscall_handle(frame);
	}
	user_fault_report(frame->esr, frame->elr, frame->far);
	process_mark_exit(process_current(), -1LL);
	{
		struct irq_frame *next = scheduler_terminate_current(frame);
		return next;
	}
}
