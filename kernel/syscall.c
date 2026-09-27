#include <nimera/abi/syscall.h>
#include <nimera/console.h>
#include <nimera/elf.h>
#include <nimera/irq.h>
#include <nimera/mmu.h>
#include <nimera/scheduler.h>
#include <nimera/syscall.h>
#include <nimera/terminal.h>
#include <nimera/vfs.h>
#include <nimera/abi/terminal.h>

#define USER_IO_CHUNK 512ULL

static int copy_from_user(char *destination, u64 source, u64 length)
{
	if (mmu_user_readable_range(source, length) == 0) return -1;
	for (u64 index = 0ULL; index < length; ++index)
		destination[index] = *(const char *)(unsigned long)(source + index);
	return 0;
}

static int copy_to_user(u64 destination, const char *source, u64 length)
{
	if (mmu_user_writable_range(destination, length) == 0) return -1;
	for (u64 index = 0ULL; index < length; ++index)
		*(char *)(unsigned long)(destination + index) = source[index];
	return 0;
}

static long long syscall_write_console(struct irq_frame *frame)
{
	char buffer[USER_IO_CHUNK];
	u64 address = frame->x[0], length = frame->x[1];
	if (mmu_user_readable_range(address, length) == 0) return NIMERA_NERR_INVALID;
	for (u64 offset = 0ULL; offset < length;) {
		u64 count = length - offset;
		if (count > USER_IO_CHUNK) count = USER_IO_CHUNK;
		if (copy_from_user(buffer, address + offset, count) != 0) return NIMERA_NERR_INVALID;
		for (u64 index = 0ULL; index < count; ++index) console_putc(buffer[index]);
		offset += count;
	}
	return (long long)length;
}

static long long syscall_open(struct irq_frame *frame)
{
	char path[VFS_PATH_MAX];
	u64 length = frame->x[1];
	if (length == 0ULL || length >= VFS_PATH_MAX ||
		copy_from_user(path, frame->x[0], length) != 0) return NIMERA_NERR_INVALID;
	path[length] = '\0';
	return elf_user_open(path, frame->x[2]);
}

static long long syscall_read(struct irq_frame *frame)
{
	char buffer[USER_IO_CHUNK];
	u64 destination = frame->x[1], length = frame->x[2], total = 0ULL;
	if (mmu_user_writable_range(destination, length) == 0) return NIMERA_NERR_INVALID;
	while (total < length) {
		u64 count = length - total;
		long long result;
		if (count > USER_IO_CHUNK) count = USER_IO_CHUNK;
		result = elf_user_read((unsigned int)frame->x[0], buffer, count);
		if (result < 0LL) return total != 0ULL ? (long long)total : result;
		if (result == 0LL) break;
		if (copy_to_user(destination + total, buffer, (u64)result) != 0) return NIMERA_NERR_INVALID;
		total += (u64)result;
		if ((u64)result < count) break;
	}
	return (long long)total;
}

static long long syscall_write_file(struct irq_frame *frame)
{
	char buffer[USER_IO_CHUNK];
	u64 source = frame->x[1], length = frame->x[2], total = 0ULL;
	if (mmu_user_readable_range(source, length) == 0) return NIMERA_NERR_INVALID;
	while (total < length) {
		u64 count = length - total;
		long long result;
		if (count > USER_IO_CHUNK) count = USER_IO_CHUNK;
		if (copy_from_user(buffer, source + total, count) != 0) return NIMERA_NERR_INVALID;
		result = elf_user_write((unsigned int)frame->x[0], buffer, count);
		if (result < 0LL) return total != 0ULL ? (long long)total : result;
		total += (u64)result;
		if ((u64)result < count) break;
	}
	return (long long)total;
}

static long long syscall_terminal_read_key(struct irq_frame *frame)
{
	struct key_event event;
	struct nimera_key_event user_event;

	if (mmu_user_writable_range(frame->x[0], sizeof(user_event)) == 0)
		return NIMERA_NERR_INVALID;
	/* SVC entry masks IRQs. This syscall may sleep until UART RX wakes it. */
	irq_enable();
	event = terminal_read_key();
	user_event.code = (u32)event.code;
	user_event.ch = (u32)(unsigned char)event.ch;
	user_event.modifiers = event.ctrl != 0U ? NIMERA_KEY_MOD_CTRL : 0U;
	user_event.reserved = 0U;
	if (copy_to_user(frame->x[0], (const char *)(const void *)&user_event,
			sizeof(user_event)) != 0) return NIMERA_NERR_INVALID;
	return 0LL;
}

static long long syscall_terminal_get_size(struct irq_frame *frame)
{
	struct nimera_terminal_size size;
	if (mmu_user_writable_range(frame->x[0], sizeof(size)) == 0)
		return NIMERA_NERR_INVALID;
	size.columns = terminal_columns();
	size.rows = terminal_rows();
	if (copy_to_user(frame->x[0], (const char *)(const void *)&size,
			sizeof(size)) != 0) return NIMERA_NERR_INVALID;
	return 0LL;
}

static long long syscall_terminal_move_cursor(struct irq_frame *frame)
{
	if (frame->x[0] >= (u64)terminal_rows() ||
		frame->x[1] >= (u64)terminal_columns()) return NIMERA_NERR_INVALID;
	terminal_move_cursor((unsigned int)frame->x[0], (unsigned int)frame->x[1]);
	return 0LL;
}

struct irq_frame *syscall_handle(struct irq_frame *frame)
{
	switch (frame->x[8]) {
	case NIMERA_SYS_WRITE_CONSOLE:
		frame->x[0] = (u64)syscall_write_console(frame); return frame;
	case NIMERA_SYS_EXIT:
		scheduler_set_user_exit_status((long long)frame->x[0]);
		{
			struct irq_frame *next = scheduler_terminate_current(frame);
			if (elf_user_task_active() != 0) {
				elf_user_task_finished(); scheduler_release_user_task();
			}
			return next;
		}
	case NIMERA_SYS_OPEN:
		frame->x[0] = (u64)syscall_open(frame); return frame;
	case NIMERA_SYS_READ:
		frame->x[0] = (u64)syscall_read(frame); return frame;
	case NIMERA_SYS_WRITE:
		frame->x[0] = (u64)syscall_write_file(frame); return frame;
	case NIMERA_SYS_CLOSE:
		frame->x[0] = (u64)elf_user_close((unsigned int)frame->x[0]); return frame;
	case NIMERA_SYS_TERM_READ_KEY:
		frame->x[0] = (u64)syscall_terminal_read_key(frame); return frame;
	case NIMERA_SYS_TERM_GET_SIZE:
		frame->x[0] = (u64)syscall_terminal_get_size(frame); return frame;
	case NIMERA_SYS_TERM_CLEAR:
		terminal_clear(); frame->x[0] = 0ULL; return frame;
	case NIMERA_SYS_TERM_MOVE_CURSOR:
		frame->x[0] = (u64)syscall_terminal_move_cursor(frame); return frame;
	case NIMERA_SYS_TERM_CLEAR_LINE:
		terminal_clear_line(); frame->x[0] = 0ULL; return frame;
	case NIMERA_SYS_TERM_SET_CURSOR_VISIBLE:
		if (frame->x[0] > 1ULL) frame->x[0] = (u64)NIMERA_NERR_INVALID;
		else { if (frame->x[0] != 0ULL) terminal_show_cursor(); else terminal_hide_cursor(); frame->x[0] = 0ULL; }
		return frame;
	default:
		frame->x[0] = (u64)NIMERA_NERR_INVALID; return frame;
	}
}
