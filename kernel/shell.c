#include <nimera/console.h>
#include <nimera/block.h>
#include <nimera/editor.h>
#include <nimera/elf.h>
#include <nimera/format.h>
#include <nimera/irq.h>
#include <nimera/memory.h>
#include <nimera/mmu.h>
#include <nimera/nimfs.h>
#include <nimera/pmm.h>
#include <nimera/scheduler.h>
#include <nimera/shell.h>
#include <nimera/terminal.h>
#include <nimera/timer.h>
#include <nimera/version.h>
#include <nimera/vfs.h>

#define SHELL_LINE_CAPACITY 128U

static const char *application_search_paths[] = { "/apps" };
static unsigned int shell_string_length(const char *text);

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
	console_write("Built-in commands:\r\n");
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
	console_write("  touch <path>\r\n");
	console_write("  write <path> <text>\r\n");
	console_write("  append <path> <text>\r\n");
	console_write("  rm <file>\r\n");
	console_write("  rmdir <directory>\r\n");
	console_write("  mv <source> <destination>\r\n");
	console_write("  edit <path>\r\n");
	console_write("  run <path>\r\n");
	console_write("  terminal\r\n");
	console_write("  disks\r\n");
	console_write("  mounts\r\n");
	console_write("  mount <disk>\r\n");
	console_write("  eject <path>\r\n");
	console_write("  fsinfo\r\n");
	console_write("Applications:\r\n");
	console_write("  cat <path>\r\n");
	console_write("  hello [args...]\r\n");
	console_write("Other commands are searched in /apps.\r\n");
}

static void shell_disks(void)
{
	console_write("Block devices:\r\n");
	if (block_count() == 0U) {
		console_write("  none\r\n");
		return;
	}
	for (unsigned int index = 0U; index < block_count(); ++index) {
		const struct block_device *device = block_get(index);
		console_write("  "); console_write(device->name); console_write(" ");
		format_u64_decimal(device->block_size); console_write(" bytes/block, ");
		format_u64_decimal(device->block_count); console_write(" blocks\r\n");
	}
}

static void shell_mounts(void)
{
	char path[VFS_PATH_MAX];
	for (unsigned int index = 0U; index < vfs_mount_count(); ++index) {
		if (vfs_mount_path(index, path, sizeof(path)) != VFS_OK) continue;
		console_write(path); console_write("  ");
		console_write(vfs_mount_filesystem_at(index) == (const char *)0 ?
			"unknown" : vfs_mount_filesystem_at(index));
		if (vfs_mount_device_at(index) != (const char *)0) {
			console_write("  "); console_write(vfs_mount_device_at(index));
		}
		if (vfs_mount_label_at(index) != (const char *)0) {
			console_write("  "); console_write(vfs_mount_label_at(index));
		}
		console_write("\r\n");
	}
}

static unsigned int shell_volume_name_length(const char *text)
{
	unsigned int n = 0U;
	while (text[n] != '\0' && n < VFS_NAME_MAX) ++n;
	return n;
}

static void shell_volume_name_copy(char *destination, const char *source,
					   unsigned int suffix)
{
	unsigned int n = shell_volume_name_length(source);
	if (suffix == 1U) {
		for (unsigned int i = 0U; i <= n; ++i) destination[i] = source[i];
		return;
	}
	if (n > VFS_NAME_MAX - 2U) n = VFS_NAME_MAX - 2U;
	for (unsigned int i = 0U; i < n; ++i) destination[i] = source[i];
	destination[n++] = '-'; destination[n++] = (char)('0' + suffix);
	destination[n] = '\0';
}

static void shell_mount(const char *argument)
{
	const struct block_device *device;
	struct vfs_node *volumes;
	struct vfs_node *mountpoint = (struct vfs_node *)0;
	struct vfs_node *existing;
	char label[NIMFS_LABEL_MAX + 1U];
	char name[VFS_NAME_MAX + 1U];
	int label_result;
	int owns = 0;

	if (argument == (const char *)0 || argument[0] == '\0') {
		shell_fs_error(VFS_INVALID_PATH); return;
	}
	device = block_find(argument);
	if (device == (const struct block_device *)0) {
		console_write("No such block device\r\n"); return;
	}
	for (unsigned int i = 1U; i < vfs_mount_count(); ++i)
		if (vfs_mount_device_at(i) != (const char *)0 &&
			text_equals(vfs_mount_device_at(i), device->name)) {
			console_write("Volume is already mounted\r\n"); return;
		}
	if (vfs_resolve(vfs_root(), "/volumes", &volumes) != VFS_OK) {
		shell_fs_error(VFS_NOT_FOUND); return;
	}
	label_result = nimfs_volume_label((struct block_device *)device, label,
						 sizeof(label));
	if (label_result != NIMFS_OK) {
		console_write(label_result == NIMFS_UNFORMATTED ?
			"No supported filesystem on " : "Corrupt NimFS on ");
		console_write(device->name); console_write("\r\n"); return;
	}
	for (unsigned int suffix = 1U; suffix < 10U; ++suffix) {
		enum vfs_error error;
		const char *base = label[0] == '\0' ? device->name : label;
		shell_volume_name_copy(name, base, suffix);
		error = vfs_resolve(volumes, name, &existing);
		if (error == VFS_OK) {
			if (vfs_node_type(existing) != VFS_NODE_DIRECTORY) continue;
			if (vfs_node_is_mountpoint(existing)) continue;
			mountpoint = existing;
			owns = vfs_readdir(existing, 0U, &existing) != VFS_OK;
			break;
		}
		if (error != VFS_NOT_FOUND) continue;
		if (vfs_mkdir(volumes, name, &mountpoint) == VFS_OK) {
			owns = 1; break;
		}
	}
	if (mountpoint == (struct vfs_node *)0) {
		console_write("No free volume name\r\n"); return;
	}
	if (nimfs_mount_at_owned((struct block_device *)device, mountpoint, owns) !=
	    NIMFS_OK) {
		if (owns != 0) (void)vfs_rmdir(volumes, name);
		console_write("Unable to mount NimFS on "); console_write(device->name);
		console_write("\r\n"); return;
	}
	console_write("Mounted "); console_write(vfs_mount_label_at(vfs_mount_count() - 1U) ==
		(const char *)0 ? name : vfs_mount_label_at(vfs_mount_count() - 1U));
	console_write(" on /volumes/"); console_write(name); console_write("\r\n");
}

static void shell_eject(const char *argument)
{
	char name[VFS_NAME_MAX + 1U];
	unsigned int length = 0U;
	if (argument == (const char *)0 || argument[0] == '\0') {
		shell_fs_error(VFS_INVALID_PATH); return;
	}
	while (argument[length] != '\0') ++length;
	while (length != 0U && argument[length - 1U] != '/') --length;
	{
		unsigned int n = 0U;
		while (argument[length + n] != '\0' && n < VFS_NAME_MAX)
			{ name[n] = argument[length + n]; ++n; }
		name[n] = '\0';
	}
	{
		enum vfs_error error = vfs_unmount_path(shell_cwd, argument);
		if (error != VFS_OK) { shell_fs_error(error); return; }
	}
	console_write("Ejected "); console_write(name); console_write("\r\n");
}

static unsigned int shell_contains_slash(const char *text)
{
	for (unsigned int index = 0U; text[index] != '\0'; ++index)
		if (text[index] == '/') return 1U;
	return 0U;
}

static unsigned int shell_is_builtin(const char *command)
{
	static const char *builtins[] = { "help", "echo", "uptime", "ticks", "irqs",
		"mem", "threads", "counter", "version", "ls", "pwd", "cd", "mkdir",
		"touch", "write", "append", "rm", "rmdir", "mv", "edit", "terminal",
		"disks", "mounts", "mount", "eject", "run", "fsinfo", "which" };
	for (unsigned int index = 0U; index < sizeof(builtins) / sizeof(builtins[0]); ++index)
		if (text_equals(command, builtins[index]) != 0U) return 1U;
	return 0U;
}

static int shell_make_application_path(const char *token, char *path)
{
	unsigned int length = shell_string_length(token);
	const char *prefix = application_search_paths[0];
	unsigned int prefix_length = shell_string_length(prefix);
	if (shell_contains_slash(token) != 0U) {
		if (length >= VFS_PATH_MAX) return -1;
		for (unsigned int index = 0U; index <= length; ++index) path[index] = token[index];
		return 0;
	}
	if (prefix_length + 1U + length >= VFS_PATH_MAX) return -1;
	for (unsigned int index = 0U; index < prefix_length; ++index) path[index] = prefix[index];
	path[prefix_length] = '/';
	for (unsigned int index = 0U; index <= length; ++index)
		path[prefix_length + 1U + index] = token[index];
	return 0;
}

static void shell_launch(const char *token, const char *rest)
{
	enum elf_result result;
	struct elf_argument arguments[ELF_MAX_ARGUMENTS];
	char executable[VFS_PATH_MAX];
	struct vfs_node *node;
	unsigned int count = 1U;
	unsigned int total;
	u64 worker_before = scheduler_worker_counter();
	const char *cursor = rest == (const char *)0 ? "" : rest;
	arguments[0].text = token;
	arguments[0].length = (u64)shell_string_length(token);
	total = (unsigned int)arguments[0].length;
	if (token == (const char *)0 || token[0] == '\0' || shell_make_application_path(token, executable) != 0) {
		console_write("Cannot execute: invalid path\r\n"); return;
	}
	while (*cursor != '\0') {
		const char *start;
		while (*cursor == ' ' || *cursor == '\t') ++cursor;
		if (*cursor == '\0') break;
		if (count == ELF_MAX_ARGUMENTS) { console_write("Cannot execute: arguments too large\r\n"); return; }
		start = cursor;
		while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') ++cursor;
		arguments[count].text = start;
		arguments[count].length = (u64)(cursor - start);
		if (arguments[count].length > ELF_MAX_ARGUMENT_BYTES - total) { console_write("Cannot execute: arguments too large\r\n"); return; }
		total += (unsigned int)arguments[count].length;
		++count;
		if (*cursor != '\0') { *(char *)(unsigned long)cursor = '\0'; ++cursor; }
	}
	if (vfs_resolve(shell_cwd, executable, &node) != VFS_OK) {
		console_write("Unknown command: "); console_write(token); console_write("\r\n"); return;
	}
	if (vfs_node_type(node) == VFS_NODE_DIRECTORY) {
		vfs_node_release(node); console_write("Cannot execute directory: "); console_write(executable); console_write("\r\n"); return;
	}
	vfs_node_release(node);
	result = elf_load_user(shell_cwd, executable, arguments, count);
	if (result != ELF_OK) {
		console_write("Cannot execute "); console_write(executable); console_write(": ");
		console_write(elf_error_string(result)); console_write("\r\n"); return;
	}
	console_write("Entering EL0...\r\n");
	scheduler_block_current();
	while (elf_user_task_active() != 0) arch_wait_for_event();
	if (text_equals(token, "keytest") != 0U) {
		console_write("EL0 terminal blocking: OK\r\nWorker progressed while app waited: ");
		console_write(scheduler_worker_counter() > worker_before ? "yes\r\n" : "no\r\n");
	}
}

static void shell_run_program(const char *argument)
{
	char token[VFS_PATH_MAX];
	unsigned int length = 0U;
	const char *cursor = argument;
	if (cursor == (const char *)0) { console_write("Usage: run <path>\r\n"); return; }
	while (cursor[length] != '\0' && cursor[length] != ' ' && cursor[length] != '\t') ++length;
	if (length == 0U || length >= sizeof(token)) { console_write("Usage: run <path>\r\n"); return; }
	for (unsigned int index = 0U; index < length; ++index) token[index] = cursor[index];
	token[length] = '\0';
	while (cursor[length] == ' ' || cursor[length] == '\t') ++length;
	shell_launch(token, cursor + length);
}

static void shell_which(const char *command)
{
	char path[VFS_PATH_MAX];
	struct vfs_node *node;
	if (command == (const char *)0 || command[0] == '\0') { console_write("Usage: which <command>\r\n"); return; }
	if (shell_is_builtin(command) != 0U) { console_write("builtin: "); console_write(command); console_write("\r\n"); return; }
	if (shell_make_application_path(command, path) == 0 && vfs_resolve(shell_cwd, path, &node) == VFS_OK) {
		vfs_node_release(node); console_write(path); console_write("\r\n"); return;
	}
	console_write("not found\r\n");
}

static void shell_fsinfo(void)
{
	console_write("Filesystem: ");
	console_write(vfs_mount_filesystem());
	console_write("\r\n");
	if (text_equals(vfs_mount_filesystem(), "NimFS")) {
		console_write("Format version: 1\r\nDevice: disk0\r\nBlock size: 512\r\nTotal blocks: ");
		format_u64_decimal(nimfs_total_blocks());
		console_write("\r\nFree blocks: ");
		format_u64_decimal(nimfs_free_blocks());
		console_write("\r\nFree inodes: ");
		format_u64_decimal(nimfs_free_inodes());
		console_write("\r\n");
	}
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

static void shell_edit(const char *path)
{
	if (path == (const char *)0) {
		shell_fs_error(VFS_INVALID_PATH);
		return;
	}
	(void)editor_run(shell_cwd, path);
}

static void shell_terminal(void)
{
	console_write("Terminal: ANSI\r\nSize: ");
	format_u64_decimal((u64)terminal_columns());
	console_putc('x');
	format_u64_decimal((u64)terminal_rows());
	console_write("\r\nGeometry: ");
	console_write(terminal_geometry_detected() != 0U ?
		"detected\r\n" : "fallback\r\n");
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
	} else if (text_equals(line, "edit")) {
		shell_edit(argument);
	} else if (text_equals(line, "terminal")) {
		shell_terminal();
	} else if (text_equals(line, "disks")) {
		shell_disks();
	} else if (text_equals(line, "mounts")) {
		shell_mounts();
	} else if (text_equals(line, "mount")) {
		shell_mount(argument);
	} else if (text_equals(line, "eject")) {
		shell_eject(argument);
	} else if (text_equals(line, "run")) {
		shell_run_program(argument);
	} else if (text_equals(line, "which")) {
		shell_which(argument);
	} else if (text_equals(line, "fsinfo")) {
		shell_fsinfo();
	} else if (length != 0U) {
		shell_launch(line, argument);
	}
}

void shell_command_test(void)
{
	struct vfs_node *node;
	char line_which_cat[] = "which cat";
	char line_which_cd[] = "which cd";
	char line_cat[] = "cat /system/version";
	char line_hello[] = "hello one two";
	char line_explicit[] = "run /apps/cat /system/version";
	char line_invalid[] = "not-an-elf";
	char line_directory[] = "command-directory";
	char line_missing[] = "does-not-exist";

	shell_cwd = vfs_root();
	(void)vfs_create_file(shell_cwd, "/apps/not-an-elf", "not ELF", 7ULL, &node);
	if (node != (struct vfs_node *)0) vfs_node_release(node);
	(void)vfs_mkdir(shell_cwd, "/apps/command-directory", &node);
	if (node != (struct vfs_node *)0) vfs_node_release(node);
	console_write("builtin resolution: "); shell_execute(line_which_cd, shell_string_length(line_which_cd));
	console_write("/apps search: "); shell_execute(line_which_cat, shell_string_length(line_which_cat));
	console_write("userspace cat: "); shell_execute(line_cat, shell_string_length(line_cat));
	console_write("hello direct: "); shell_execute(line_hello, shell_string_length(line_hello));
	console_write("explicit run: "); shell_execute(line_explicit, shell_string_length(line_explicit));
	console_write("invalid executable guard: "); shell_execute(line_invalid, shell_string_length(line_invalid));
	console_write("directory execution guard: "); shell_execute(line_directory, shell_string_length(line_directory));
	console_write("missing command: "); shell_execute(line_missing, shell_string_length(line_missing));
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
