// QEMU's "virt" machine exposes a PL011-compatible UART at this address.
// This is a direct MMIO driver: no libc, OS API, or UART runtime is used.

typedef unsigned int u32;

#define PL011_BASE 0x09000000UL
#define UART_DR (*(volatile u32 *)(PL011_BASE + 0x00))

static void uart_putc(char c)
{
	// A volatile store is required: the compiler must emit the MMIO write.
	// PL011 accepts writes to DR and QEMU forwards them to the serial device.
	UART_DR = (u32)(unsigned char)c;
}

void uart_puts(const char *text)
{
	while (*text != '\0') {
		uart_putc(*text++);
	}
}
