// QEMU's virt machine exposes a PL011-compatible UART. TX remains polling,
// while RX is delivered by the UART IRQ into this fixed software queue.

#include <nimera/irq.h>
#include <nimera/panic.h>
#include <nimera/scheduler.h>
#include <nimera/types.h>

typedef unsigned int u32;

#define PL011_DEFAULT_BASE 0x09000000ULL
#define UART_DR 0x00U
#define UART_FR 0x18U
#define UART_IMSC 0x38U
#define UART_MIS 0x40U
#define UART_ICR 0x44U

#define UART_FR_RXFE (1U << 4)
#define UART_FR_TXFF (1U << 5)
#define UART_INT_RX (1U << 4)
#define UART_INT_RT (1U << 6)

#define UART_RX_BUFFER_CAPACITY 256U

static u64 uart_base = PL011_DEFAULT_BASE;
static volatile unsigned char rx_storage[UART_RX_BUFFER_CAPACITY];
static volatile unsigned int rx_head;
static volatile unsigned int rx_tail;
static volatile u64 rx_dropped;
static volatile u64 rx_irq_count;

static volatile u32 *uart_register(u32 offset)
{
	return (volatile u32 *)(unsigned long)(uart_base + (u64)offset);
}

static void compiler_memory_barrier(void)
{
	__asm__ volatile("" ::: "memory");
}

static void rx_push(unsigned char value)
{
	unsigned int next = (rx_head + 1U) % UART_RX_BUFFER_CAPACITY;

	if (next == rx_tail) {
		/* Keep unread data intact; drop only the newest byte. */
		++rx_dropped;
		return;
	}
	rx_storage[rx_head] = value;
	compiler_memory_barrier();
	rx_head = next;
}

void uart_init(u64 base)
{
	uart_base = base;
	rx_head = 0U;
	rx_tail = 0U;
	rx_dropped = 0ULL;
	rx_irq_count = 0ULL;
}

void uart_putc(char c)
{
	volatile u32 *flags = uart_register(UART_FR);

	while ((*flags & UART_FR_TXFF) != 0U) {
	}
	*uart_register(UART_DR) = (u32)(unsigned char)c;
}

char uart_getc(void)
{
	for (;;) {
		u64 irq_state = irq_save_disable();
		unsigned int tail = rx_tail;

		if (tail != rx_head) {
			char value = (char)rx_storage[tail];

			compiler_memory_barrier();
			rx_tail = (tail + 1U) % UART_RX_BUFFER_CAPACITY;
			irq_restore(irq_state);
			return value;
		}

		/* The check and WAITING transition are atomic against UART IRQs. */
		scheduler_block_current();
		irq_restore(irq_state);
		/* WFE only parks an already WAITING thread; it is not the polling
		 * mechanism. Timer IRQs switch away, and UART IRQs wake this thread. */
		arch_wait_for_event();
	}
}

void uart_puts(const char *text)
{
	while (*text != '\0') {
		uart_putc(*text++);
	}
}

void uart_enable_rx_interrupt(void)
{
	/* RX and receive-timeout interrupts cover both immediate and paused input. */
	*uart_register(UART_IMSC) |= UART_INT_RX | UART_INT_RT;
}

void uart_handle_irq(void)
{
	u32 masked_status = *uart_register(UART_MIS);
	unsigned int received = 0U;

	if ((masked_status & (UART_INT_RX | UART_INT_RT)) == 0U) {
		return;
	}
	++rx_irq_count;
	while ((*uart_register(UART_FR) & UART_FR_RXFE) == 0U) {
		rx_push((unsigned char)(*uart_register(UART_DR) & 0xffU));
		++received;
	}
	/* ICR acknowledges both sources after the FIFO has been drained. */
	*uart_register(UART_ICR) = UART_INT_RX | UART_INT_RT;
	if (received != 0U) {
		scheduler_wake_console_input();
		if (scheduler_console_waiting()) {
			/* A received byte must not leave the sole console consumer
			 * WAITING after the wakeup transition. */
			panic("console wakeup invariant failed");
		}
	}
	arch_signal_event();
}

u64 uart_rx_irq_count(void)
{
	return rx_irq_count;
}

u64 uart_dropped_bytes(void)
{
	return rx_dropped;
}

int uart_overflow_test(void)
{
	unsigned int index;

	rx_head = 0U;
	rx_tail = 0U;
	rx_dropped = 0ULL;
	for (index = 0U; index < UART_RX_BUFFER_CAPACITY + 10U; ++index) {
		rx_push((unsigned char)index);
	}
	if (rx_head != UART_RX_BUFFER_CAPACITY - 1U || rx_tail != 0U ||
	    rx_storage[0] != 0U || rx_storage[UART_RX_BUFFER_CAPACITY - 2U] != 254U ||
	    rx_dropped != 11ULL) {
		return -1;
	}
	return 0;
}
