// QEMU's "virt" machine exposes a PL011-compatible UART at this address.
// This is a direct MMIO driver: no libc, OS API, or UART runtime is used.

typedef unsigned int u32;

#define PL011_BASE 0x09000000UL
#define UART_DR (*(volatile u32 *)(PL011_BASE + 0x00))
#define UART_FR (*(volatile u32 *)(PL011_BASE + 0x18))

#define UART_FR_RXFE (1U << 4)
#define UART_FR_TXFF (1U << 5)

void uart_putc(char c)
{
	// TXFF means the transmit FIFO is full. Waiting here prevents a new byte
	// from being written while PL011 has no room to accept it.
	while ((UART_FR & UART_FR_TXFF) != 0U) {
	}

	// DR is the receive/transmit data register. A volatile store makes this
	// write reach the UART instead of being optimized away.
	UART_DR = (u32)(unsigned char)c;
}

char uart_getc(void)
{
	// RXFE means the receive FIFO is empty. Polling this hardware state avoids
	// guessing with a delay and waits until QEMU has supplied an input byte.
	while ((UART_FR & UART_FR_RXFE) != 0U) {
	}

	// Only the low eight bits of DR contain the received character here; the
	// upper bits carry PL011 status information on reads.
	return (char)(UART_DR & 0xffU);
}

void uart_puts(const char *text)
{
	while (*text != '\0') {
		uart_putc(*text++);
	}
}
