#include <nimera/console.h>
#include <nimera/halt.h>
#include <nimera/panic.h>

__attribute__((noreturn))
void panic(const char *message)
{
	debug_console_write("Nimera kernel panic\r\n");
	debug_console_write("Reason: ");
	debug_console_write(message);
	debug_console_write("\r\nSystem halted.\r\n");

	cpu_halt();
}
