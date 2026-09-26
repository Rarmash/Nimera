// No headers, libc, allocator, or runtime are needed for this milestone.

extern void uart_puts(const char *text);

static void cpu_idle(void)
{
	for (;;) {
		// Wait For Event avoids a tight spinning loop while keeping the CPU in a
		// defined state. With no interrupt subsystem yet, this simply waits for
		// an event and then safely repeats.
		__asm__ volatile("wfe" ::: "memory");
	}
}

void kernel_main(void)
{
	uart_puts("Hello from kernel\r\n");
	cpu_idle();
}
