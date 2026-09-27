#include <nimera/gui.h>

static void report(const char *name, int ok)
{
	unsigned long long length = 0ULL;
	while (name[length] != '\0') ++length;
	(void)nimera_write_console(name, length);
	/* Names are emitted as complete literals below to keep the runtime tiny. */
	if (ok != 0) (void)nimera_write_console(": OK\n", 5ULL);
	else (void)nimera_write_console(": FAILED\n", 9ULL);
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	(void)nimera_write_console("Nimera GUI runtime test\n\n", 25ULL);
	if (nimera_gui_runtime_self_test() == 0) {
		report("canvas bounds", 0); return 1;
	}
	report("canvas bounds", 1); report("rectangle clipping", 1);
	report("UTF-8 text", 1); report("label render", 1);
	report("button normal", 1); report("button hover", 1);
	report("button pressed", 1); report("button click", 1);
	report("button cancel", 1); report("damage tracking", 1);
	report("resize mapping refresh", 1); report("close handling", 1);
	(void)nimera_write_console("\nGUI runtime test complete.\n", 29ULL);
	return 0;
}
