#include <nimera/console.h>
#include <nimera/panic.h>

__attribute__((noreturn))
void panic(const char *message)
{
	console_write("Nimera kernel panic\r\n");
	console_write("Reason: ");
	console_write(message);
	console_write("\r\nSystem halted.\r\n");

	for (;;) {
		// With no interrupt subsystem yet, WFE safely stops normal execution
		// while keeping this CPU-local halt loop minimal.
		__asm__ volatile("wfe" ::: "memory");
	}
}
