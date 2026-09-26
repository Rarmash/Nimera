#include <nimera/console.h>
#include <nimera/format.h>
#include <nimera/irq.h>
#include <nimera/memory.h>
#include <nimera/mmu.h>
#include <nimera/pmm.h>
#include <nimera/scheduler.h>
#include <nimera/shell.h>
#include <nimera/terminal.h>
#include <nimera/timer.h>
#include <nimera/version.h>
#include <nimera/vfs.h>

#define SHELL_LINE_CAPACITY 128U

static unsigned int text_equals(const char *left, const char *right)
{
	unsigned int index = 0U;

	while (left[index] != '\0' && right[index] != '\0') {
		if (left[index] != right[index]) {
			return 0U;
		}
		++index;
	}

	return left[index] == '\0' && right[index] == '\0';
}

static unsigned int starts_echo(const char *line, unsigned int length)
{
	return length >= 4U && line[0] == 'e' && line[1] == 'c' &&
	       line[2] == 'h' && line[3] == 'o' &&
	       (length == 4U || line[4] == ' ' || line[4] == '\t');
}

static struct vfs_node *shell_cwd;

static void shell_fs_error(enum vfs_error error)
{
	console_write(vfs_error_string(error));
	console_write("\r\n");
}

static void shell_help(void)
{
	console_write("Available commands:\r\n");
	console_write("  help\r\n");
	console_write("  echo [text]\r\n");
	console_write("  uptime\r\n");
	console_write("  ticks\r\n");
	console_write("  irqs\r\n");
	console_write("  mem\r\n");
	console_write("  threads\r\n");
	console_write("  counter\r\n");
	console_write("  version\r\n");
	console_write("  ls [path]\r\n");
	console_write("  pwd\r\n");
	console_write("  cd <path>\r\n");
	console_write("  mkdir <path>\r\n");
	console_write("  cat <path>\r\n");
	console_write("  touch <path>\r\n");
	console_write("  write <path> <text>\r\n");
	console_write("  append <path> <text>\r\n");
	console_write("  rm <file>\r\n");
	console_write("  rmdir <directory>\r\n");
	console_write("  mv <source> <destination>\r\n");
}

static void shell_ls(const char *path)
{
	struct vfs_node *directory;
	enum vfs_error error;

	error = vfs_resolve(shell_cwd, path == (const char *)0 ? "." : path,
				    &directory);
	if (error != VFS_OK) {
		shell_fs_error(error);
		return;
	}
	if (vfs_node_type(directory) != VFS_NODE_DIRECTORY) {
		shell_fs_error(VFS_NOT_DIRECTORY);
		return;
	}
	for (unsigned int index = 0U;; ++index) {
		struct vfs_node *entry;

		error = vfs_readdir(directory, index, &entry);
		if (error == VFS_NOT_FOUND) {
			break;
		}
		if (error != VFS_OK) {
			shell_fs_error(error);
			return;
		}
		console_write(vfs_node_name(entry));
		console_write("\r\n");
	}
}

static void shell_pwd(void)
{
	char path[VFS_PATH_MAX];

	if (vfs_format_path(shell_cwd, path, sizeof(path)) != VFS_OK) {
		shell_fs_error(VFS_TOO_LARGE);
		return;
	}
	console_write(path);
	console_write("\r\n");
}

static void shell_cd(const char *path)
{
	struct vfs_node *node;
	enum vfs_error error;

	if (path == (const char *)0) {
		shell_fs_error(VFS_INVALID_PATH);
		return;
	}
	error = vfs_resolve(shell_cwd, path, &node);
	if (error != VFS_OK) {
		shell_fs_error(error);
		return;
	}
	if (vfs_node_type(node) != VFS_NODE_DIRECTORY) {
		shell_fs_error(VFS_NOT_DIRECTORY);
		return;
	}
	shell_cwd = node;
}

static void shell_mkdir(const char *path)
{
	enum vfs_error error;

	if (path == (const char *)0) {
		shell_fs_error(VFS_INVALID_PATH);
		return;
	}
	error = vfs_mkdir(shell_cwd, path, (struct vfs_node **)0);
	if (error != VFS_OK) {
		shell_fs_error(error);
	}
}

static void shell_cat(const char *path)
{
	char buffer[128];
	struct vfs_node *file;
	u64 size;
	enum vfs_error error;

	if (path == (const char *)0) {
		shell_fs_error(VFS_INVALID_PATH);
		return;
	}
	error = vfs_resolve(shell_cwd, path, &file);
	if (error == VFS_OK) {
		error = vfs_read(file, buffer, sizeof(buffer), &size);
	}
	if (error != VFS_OK) {
		shell_fs_error(error);
		return;
	}
	for (u64 index = 0ULL; index < size; ++index) {
		console_putc(buffer[index]);
	}
	console_write("\r\n");
}

static unsigned int shell_string_length(const char *text)
{
	unsigned int length = 0U;

	while (text[length] != '\0') {
		++length;
	}
	return length;
}

static int shell_path_and_text(char *argument, char **path, char **text)
{
	unsigned int index;

	if (argument == (char *)0 || argument[0] == '\0') {
		return 0;
	}
	for (index = 0U; argument[index] != '\0'; ++index) {
		if (argument[index] == ' ' || argument[index] == '\t') {
			argument[index++] = '\0';
			while (argument[index] == ' ' || argument[index] == '\t') {
				++index;
			}
			*path = argument;
			*text = &argument[index];
			return 1;
		}
	}
	*path = argument;
	*text = &argument[index];
	return 1;
}

static int shell_two_paths(char *argument, char **source, char **destination)
{
	unsigned int index;

	if (argument == (char *)0 || argument[0] == '\0') {
		return 0;
	}
	*source = argument;
	for (index = 0U; argument[index] != '\0'; ++index) {
		if (argument[index] == ' ' || argument[index] == '\t') {
			argument[index++] = '\0';
			while (argument[index] == ' ' || argument[index] == '\t') {
				++index;
			}
			if (argument[index] == '\0') {
				return 0;
			}
			*destination = &argument[index];
			return 1;
		}
	}
	return 0;
}

static void shell_touch(const char *path)
{
	enum vfs_error error;

	if (path == (const char *)0) {
		shell_fs_error(VFS_INVALID_PATH);
		return;
	}
	error = vfs_touch(shell_cwd, path, (struct vfs_node **)0);
	if (error != VFS_OK) {
		shell_fs_error(error);
	}
}

static void shell_write(char *argument, int append)
{
	char *path;
	char *text;
	enum vfs_error error;

	if (shell_path_and_text(argument, &path, &text) == 0) {
		shell_fs_error(VFS_INVALID_PATH);
		return;
	}
	error = append != 0 ?
		vfs_append(shell_cwd, path, text, shell_string_length(text),
			  (struct vfs_node **)0) :
		vfs_write(shell_cwd, path, text, shell_string_length(text),
			 (struct vfs_node **)0);
	if (error != VFS_OK) {
		shell_fs_error(error);
	}
}

static void shell_remove(const char *path, int directory)
{
	enum vfs_error error;

	if (path == (const char *)0) {
		shell_fs_error(VFS_INVALID_PATH);
		return;
	}
	error = directory != 0 ? vfs_rmdir(shell_cwd, path) :
		vfs_remove(shell_cwd, path);
	if (error == VFS_IS_DIRECTORY && directory == 0) {
		console_write("Is a directory; use rmdir\r\n");
	} else if (error != VFS_OK) {
		shell_fs_error(error);
	}
}

static void shell_mv(char *argument)
{
	char *source;
	char *destination;
	enum vfs_error error;

	if (shell_two_paths(argument, &source, &destination) == 0) {
		shell_fs_error(VFS_INVALID_PATH);
		return;
	}
	error = vfs_rename(shell_cwd, source, destination);
	if (error != VFS_OK) {
		shell_fs_error(error);
	}
}

static void shell_threads(void)
{
	unsigned int index;

	console_write("Threads:\r\n");
	for (index = 0U; index < scheduler_thread_count(); ++index) {
		const struct thread *thread = scheduler_thread(index);

		console_write("  ");
		format_u64_decimal(thread->id);
		console_write(" ");
		console_write(thread->name);
		console_write(" ");
		console_write(thread->state == THREAD_RUNNING ? "RUNNING" :
			thread->state == THREAD_WAITING ? "WAITING" : "READY");
		console_write(" switches=");
		format_u64_decimal(thread->switch_count);
		console_write("\r\n");
	}
}

static void shell_counter(void)
{
	console_write("Worker counter: ");
	format_u64_decimal(scheduler_worker_counter());
	console_write("\r\n");
}

static void shell_uptime(void)
{
	u64 uptime = timer_uptime_ms();
	u64 milliseconds = uptime % 1000ULL;

	console_write("Uptime: ");
	format_u64_decimal(uptime / 1000ULL);
	console_putc('.');
	console_putc((char)('0' + (milliseconds / 100ULL)));
	console_putc((char)('0' + ((milliseconds / 10ULL) % 10ULL)));
	console_putc((char)('0' + (milliseconds % 10ULL)));
	console_write(" s\r\n");
}

static void shell_ticks(void)
{
	console_write("Timer IRQ ticks: ");
	format_u64_decimal(irq_timer_ticks());
	console_write("\r\n");
}

static void shell_irqs(void)
{
	console_write("Timer IRQs: ");
	format_u64_decimal(irq_timer_ticks());
	console_write("\r\nUART RX IRQs: ");
	format_u64_decimal(irq_uart_count());
	console_write("\r\nUART dropped bytes: ");
	format_u64_decimal(irq_uart_dropped_bytes());
	console_write("\r\n");
}

static void shell_memory(void)
{
	struct memory_map map = memory_discover();

	memory_print_map(&map);
	console_write("Page size: ");
	format_u64_decimal(NIMERA_PAGE_SIZE);
	console_write(" bytes\r\nManaged pages: ");
	format_u64_decimal(pmm_total_pages());
	console_write("\r\nPMM metadata: ");
	format_u64_decimal(pmm_metadata_pages());
	console_write(" page(s), ");
	format_u64_decimal(pmm_bitmap_bytes());
	console_write(" bytes\r\nFree pages: ");
	format_u64_decimal(pmm_free_pages());
	console_write("\r\nAllocated pages: ");
	format_u64_decimal(pmm_used_pages());
	console_write("\r\nFree memory: ");
	format_u64_decimal(pmm_free_pages() * NIMERA_PAGE_SIZE / 1024ULL);
	console_write(" KiB\r\nMMU: ");
	console_write(mmu_enabled() != 0ULL ? "enabled\r\n" : "disabled\r\n");
	console_write("Page-table pages: ");
	format_u64_decimal(mmu_page_table_pages());
	console_write("\r\nMMU L3 tables: ");
	format_u64_decimal(mmu_l3_table_pages());
	console_write("\r\nKernel text: RO+X\r\nKernel rodata: RO+NX\r\n");
	console_write("Kernel data: RW+NX\r\n");
}

static void shell_execute(char *line, unsigned int length)
{
	unsigned int index = 0U;
	char *argument = (char *)0;
	unsigned int echo_command;

	while (index < length && (line[index] == ' ' || line[index] == '\t')) {
		++index;
	}
	line += index;
	length -= index;

	while (length != 0U && (line[length - 1U] == ' ' ||
					line[length - 1U] == '\t')) {
		line[--length] = '\0';
	}
	echo_command = starts_echo(line, length);
	for (index = 0U; index < length; ++index) {
		if (line[index] == ' ' || line[index] == '\t') {
			line[index] = '\0';
			argument = &line[index + 1U];
			while (*argument == ' ' || *argument == '\t') {
				++argument;
			}
			break;
		}
	}

	if (text_equals(line, "help")) {
		shell_help();
	} else if (echo_command != 0U) {
		console_write(argument == (char *)0 ? "" : argument);
		console_write("\r\n");
	} else if (text_equals(line, "uptime")) {
		shell_uptime();
	} else if (text_equals(line, "ticks")) {
		shell_ticks();
	} else if (text_equals(line, "irqs")) {
		shell_irqs();
	} else if (text_equals(line, "mem")) {
		shell_memory();
	} else if (text_equals(line, "threads")) {
		shell_threads();
	} else if (text_equals(line, "counter")) {
		shell_counter();
	} else if (text_equals(line, "version")) {
		console_write(NIMERA_VERSION "\r\n");
	} else if (text_equals(line, "ls")) {
		shell_ls(argument);
	} else if (text_equals(line, "pwd")) {
		shell_pwd();
	} else if (text_equals(line, "cd")) {
		shell_cd(argument);
	} else if (text_equals(line, "mkdir")) {
		shell_mkdir(argument);
	} else if (text_equals(line, "cat")) {
		shell_cat(argument);
	} else if (text_equals(line, "touch")) {
		shell_touch(argument);
	} else if (text_equals(line, "write")) {
		shell_write(argument, 0);
	} else if (text_equals(line, "append")) {
		shell_write(argument, 1);
	} else if (text_equals(line, "rm")) {
		shell_remove(argument, 0);
	} else if (text_equals(line, "rmdir")) {
		shell_remove(argument, 1);
	} else if (text_equals(line, "mv")) {
		shell_mv(argument);
	} else if (length != 0U) {
		console_write("Unknown command: ");
		console_write(line);
		console_write("\r\n");
	}
}

__attribute__((noreturn))
void shell_run(void)
{
	char line[SHELL_LINE_CAPACITY];
	shell_cwd = vfs_root();

	for (;;) {
		unsigned int length = 0U;

		char path[VFS_PATH_MAX];

		console_write("nimera:");
		if (vfs_format_path(shell_cwd, path, sizeof(path)) == VFS_OK) {
			console_write(path);
		} else {
			console_write("?");
		}
		console_write(" $ ");
		for (;;) {
			struct key_event event = terminal_read_key();

			if (event.code == KEY_ENTER) {
				console_write("\r\n");
				line[length] = '\0';
				shell_execute(line, length);
				break;
			}
			if (event.code == KEY_BACKSPACE) {
				if (length != 0U) {
					--length;
					console_write("\b \b");
				}
				continue;
			}
			if (event.code == KEY_CHAR && event.ctrl == 0U &&
				event.ch >= 32 && event.ch <= 126 &&
				length < SHELL_LINE_CAPACITY - 1U) {
				line[length++] = event.ch;
				console_putc(event.ch);
			}
		}
	}
}
