#include <nimera/console.h>
#include <nimera/halt.h>
#include <nimera/panic.h>

__attribute__((noreturn))
void panic(const char *message)
{
	console_write("Nimera kernel panic\r\n");
	console_write("Reason: ");
	console_write(message);
	console_write("\r\nSystem halted.\r\n");

	cpu_halt();
}
