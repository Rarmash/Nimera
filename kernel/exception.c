#include <nimera/console.h>
#include <nimera/exception.h>
#include <nimera/format.h>
#include <nimera/halt.h>

static void write_exception_type(u64 type)
{
	if (type == 0ULL) {
		console_write("synchronous");
	} else if (type == 1ULL) {
		console_write("irq");
	} else if (type == 2ULL) {
		console_write("fiq");
	} else {
		console_write("serror");
	}
}

__attribute__((noreturn))
void exception_fatal(u64 type, u64 esr, u64 elr, u64 far, u64 current_el)
{
	u64 exception_class = (esr >> 26) & 0x3fULL;

	console_write("Nimera exception\r\nType: ");
	write_exception_type(type);
	console_write("\r\nCurrentEL: ");
	format_u64_decimal(current_el);
	console_write("\r\nESR: ");
	format_u64_hex(esr);
	console_write("\r\nELR: ");
	format_u64_hex(elr);
	console_write("\r\nFAR: ");
	format_u64_hex(far);
	console_write("\r\nEC: ");
	format_u64_hex(exception_class);
	console_write("\r\nSystem halted.\r\n");

	cpu_halt();
}
