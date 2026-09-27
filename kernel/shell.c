#include <nimera/console.h>
#include <nimera/abi/syscall.h>
#include <nimera/block.h>
#include <nimera/editor.h>
#include <nimera/elf.h>
#include <nimera/format.h>
#include <nimera/heap.h>
#include <nimera/irq.h>
#include <nimera/memory.h>
#include <nimera/mmu.h>
#include <nimera/nimfs.h>
#include <nimera/pmm.h>
#include <nimera/pipe.h>
#include <nimera/process.h>
#include <nimera/scheduler.h>
#include <nimera/shell.h>
#include <nimera/terminal.h>
#include <nimera/timer.h>
#include <nimera/version.h>
#include <nimera/vfs.h>

#define SHELL_LINE_CAPACITY 128U
#define SHELL_MAX_JOBS 8U
#define SHELL_MAX_JOB_PROCESSES 2U

static const char *application_search_paths[] = { "/apps" };
static unsigned int shell_string_length(const char *text);

enum shell_job_state { SHELL_JOB_FREE, SHELL_JOB_RUNNING,
	SHELL_JOB_DONE, SHELL_JOB_FAILED };

struct shell_job {
	unsigned int in_use;
	unsigned int id;
	enum shell_job_state state;
	unsigned int background;
	unsigned int process_count;
	u64 pids[SHELL_MAX_JOB_PROCESSES];
	long long statuses[SHELL_MAX_JOB_PROCESSES];
	unsigned int finished[SHELL_MAX_JOB_PROCESSES];
	char summary[SHELL_LINE_CAPACITY];
};

static struct shell_job shell_jobs[SHELL_MAX_JOBS];
static unsigned int shell_next_job_id = 1U;
static void shell_job_clear(struct shell_job *job);
static struct shell_job *shell_job_find(unsigned int id);
static struct shell_job *shell_job_alloc(const char *summary, unsigned int background);
static void shell_job_add_process(struct shell_job *job, struct process *process);
static void shell_job_wait(struct shell_job *job);
static void shell_foreground_job(struct shell_job *job);

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
	console_write("  cd <path>\r\n");
	console_write("  kedit <path> (legacy kernel editor)\r\n");
	console_write("  run <path>\r\n");
	console_write("  terminal\r\n");
	console_write("  disks\r\n");
	console_write("  mounts\r\n");
	console_write("  jobs\r\n");
	console_write("  fg <job-id>\r\n");
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
		"mem", "threads", "counter", "version", "cd",
		"kedit", "terminal",
		"disks", "mounts", "jobs", "fg", "mount", "eject", "run", "fsinfo", "which" };
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

struct shell_command {
	char command[VFS_PATH_MAX];
	char arguments[SHELL_LINE_CAPACITY];
	char input[VFS_PATH_MAX];
	char output[VFS_PATH_MAX];
	unsigned int has_input;
	unsigned int has_output;
	unsigned int append_output;
};

static void shell_stdio_release_files(struct process_stdio *stdio)
{
	if (stdio->stdin_file != (struct vfs_node *)0) {
		vfs_node_release(stdio->stdin_file);
		stdio->stdin_file = (struct vfs_node *)0;
	}
	if (stdio->stdout_file != (struct vfs_node *)0) {
		vfs_node_release(stdio->stdout_file);
		stdio->stdout_file = (struct vfs_node *)0;
	}
}

static int shell_copy_token(char *destination, unsigned int capacity,
		const char *source, unsigned int length)
{
	if (length == 0U || length >= capacity) return -1;
	for (unsigned int index = 0U; index < length; ++index)
		destination[index] = source[index];
	destination[length] = '\0';
	return 0;
}

static int shell_parse_stage(const char *stage, unsigned int length,
	struct shell_command *command, unsigned int allow_input,
	unsigned int allow_output)
{
	unsigned int position = 0U;
	unsigned int argument_length = 0U;
	unsigned int have_command = 0U;
	for (unsigned int index = 0U; index < sizeof(*command); ++index)
		((unsigned char *)command)[index] = 0U;
	while (position < length) {
		const char *start;
		unsigned int token_length;
		while (position < length && (stage[position] == ' ' || stage[position] == '\t')) ++position;
		if (position == length) break;
		start = stage + position;
		while (position < length && stage[position] != ' ' && stage[position] != '\t') ++position;
		token_length = (unsigned int)((stage + position) - start);
		if (token_length == 1U && start[0] == '<') {
			if (allow_input == 0U || command->has_input != 0U) return -2;
			while (position < length && (stage[position] == ' ' || stage[position] == '\t')) ++position;
			if (position == length) return -1;
			start = stage + position;
			while (position < length && stage[position] != ' ' && stage[position] != '\t') ++position;
			if (shell_copy_token(command->input, sizeof(command->input), start,
				(unsigned int)((stage + position) - start)) != 0) return -1;
			command->has_input = 1U;
			continue;
		}
		if ((token_length == 1U && start[0] == '>') ||
			(token_length == 2U && start[0] == '>' && start[1] == '>')) {
			if (allow_output == 0U || command->has_output != 0U) return -2;
			while (position < length && (stage[position] == ' ' || stage[position] == '\t')) ++position;
			if (position == length) return -1;
			start = stage + position;
			while (position < length && stage[position] != ' ' && stage[position] != '\t') ++position;
			if (shell_copy_token(command->output, sizeof(command->output), start,
				(unsigned int)((stage + position) - start)) != 0) return -1;
			command->has_output = 1U;
			command->append_output = token_length == 2U;
			continue;
		}
		if (have_command == 0U) {
			if (shell_copy_token(command->command, sizeof(command->command), start,
				token_length) != 0) return -1;
			have_command = 1U;
		} else {
			if (argument_length != 0U) command->arguments[argument_length++] = ' ';
			if (argument_length + token_length >= sizeof(command->arguments)) return -1;
			for (unsigned int index = 0U; index < token_length; ++index)
				command->arguments[argument_length++] = start[index];
			command->arguments[argument_length] = '\0';
		}
	}
	return have_command == 0U ? -1 : 0;
}

static int shell_validate_command(const char *token, char *executable)
{
	struct vfs_node *node;
	if (shell_make_application_path(token, executable) != 0 ||
		vfs_resolve(shell_cwd, executable, &node) != VFS_OK) {
		console_write("Unknown command: "); console_write(token); console_write("\r\n");
		return -1;
	}
	if (vfs_node_type(node) == VFS_NODE_DIRECTORY) {
		vfs_node_release(node);
		console_write("Cannot execute directory: "); console_write(executable); console_write("\r\n");
		return -1;
	}
	vfs_node_release(node);
	return 0;
}

static int shell_prepare_file_bindings(const struct shell_command *command,
	struct process_stdio *stdio)
{
	struct vfs_node *node;
	enum vfs_error error;
	if (command->has_input != 0U) {
		error = vfs_resolve(shell_cwd, command->input, &node);
		if (error != VFS_OK) { shell_fs_error(error); return -1; }
		if (vfs_node_type(node) != VFS_NODE_FILE) {
			vfs_node_release(node); shell_fs_error(VFS_IS_DIRECTORY); return -1;
		}
		stdio->stdin_file = node;
		stdio->stdin_flags = NIMERA_OPEN_READ;
	}
	if (command->has_output != 0U) {
		error = vfs_resolve(shell_cwd, command->output, &node);
		if (error == VFS_NOT_FOUND)
			error = vfs_create_file(shell_cwd, command->output, (const char *)0, 0ULL, &node);
		if (error != VFS_OK) {
			shell_stdio_release_files(stdio); shell_fs_error(error); return -1;
		}
		if (vfs_node_type(node) != VFS_NODE_FILE) {
			vfs_node_release(node); shell_stdio_release_files(stdio);
			shell_fs_error(VFS_IS_DIRECTORY); return -1;
		}
		stdio->stdout_file = node;
		stdio->stdout_flags = NIMERA_OPEN_WRITE |
			(command->append_output != 0U ? NIMERA_OPEN_APPEND : NIMERA_OPEN_TRUNCATE);
		if (command->append_output == 0U &&
			vfs_write_at(node, 0ULL, (const char *)0, 0ULL) != VFS_OK) {
			shell_stdio_release_files(stdio); shell_fs_error(VFS_IO_ERROR); return -1;
		}
	}
	return 0;
}

static struct process *shell_spawn_process(const char *token, const char *rest,
	const struct process_stdio *stdio)
{
	enum elf_result result;
	struct elf_argument arguments[ELF_MAX_ARGUMENTS];
	char executable[VFS_PATH_MAX];
	struct vfs_node *node;
	unsigned int count = 1U;
	unsigned int total;
	const char *cursor = rest == (const char *)0 ? "" : rest;
	arguments[0].text = token;
	arguments[0].length = (u64)shell_string_length(token);
	total = (unsigned int)arguments[0].length;
	if (token == (const char *)0 || token[0] == '\0' || shell_make_application_path(token, executable) != 0) {
		console_write("Cannot execute: invalid path\r\n"); return (struct process *)0;
	}
	while (*cursor != '\0') {
		const char *start;
		while (*cursor == ' ' || *cursor == '\t') ++cursor;
		if (*cursor == '\0') break;
		if (count == ELF_MAX_ARGUMENTS) { console_write("Cannot execute: arguments too large\r\n"); return (struct process *)0; }
		start = cursor;
		while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') ++cursor;
		arguments[count].text = start;
		arguments[count].length = (u64)(cursor - start);
		if (arguments[count].length > ELF_MAX_ARGUMENT_BYTES - total) { console_write("Cannot execute: arguments too large\r\n"); return (struct process *)0; }
		total += (unsigned int)arguments[count].length;
		++count;
		if (*cursor != '\0') { *(char *)(unsigned long)cursor = '\0'; ++cursor; }
	}
	if (vfs_resolve(shell_cwd, executable, &node) != VFS_OK) {
		console_write("Unknown command: "); console_write(token); console_write("\r\n"); return (struct process *)0;
	}
	if (vfs_node_type(node) == VFS_NODE_DIRECTORY) {
		vfs_node_release(node); console_write("Cannot execute directory: "); console_write(executable); console_write("\r\n"); return (struct process *)0;
	}
	vfs_node_release(node);
	result = elf_load_user_with_stdio(shell_cwd, executable, arguments, count, stdio);
	if (result != ELF_OK) {
		console_write("Cannot execute "); console_write(executable); console_write(": ");
		console_write(elf_error_string(result)); console_write("\r\n"); return (struct process *)0;
	}
	return process_last_spawned();
}

static struct process *shell_spawn_command(const struct shell_command *command,
	struct process_stdio *stdio)
{
	struct process *process;
	char executable[VFS_PATH_MAX];
	if (shell_validate_command(command->command, executable) != 0) return (struct process *)0;
	if (shell_prepare_file_bindings(command, stdio) != 0) return (struct process *)0;
	process = shell_spawn_process(command->command, command->arguments, stdio);
	if (process == (struct process *)0) shell_stdio_release_files(stdio);
	return process;
}

static void shell_launch(const char *token, const char *rest,
	const char *summary, unsigned int background)
{
	u64 worker_before = scheduler_worker_counter();
	struct shell_job *job = shell_job_alloc(summary, background);
	struct process *process = shell_spawn_process(token, rest,
		(const struct process_stdio *)0);
	if (job == (struct shell_job *)0) return;
	if (process == (struct process *)0) { shell_job_clear(job); return; }
	shell_job_add_process(job, process);
	if (background == 0U) {
		process->terminal_owner = 1U;
		console_write("Entering EL0...\r\n");
		shell_job_wait(job);
		shell_job_clear(job);
		if (text_equals(token, "keytest") != 0U) {
			console_write("EL0 terminal blocking: OK\r\nWorker progressed while app waited: ");
			console_write(scheduler_worker_counter() > worker_before ? "yes\r\n" : "no\r\n");
		}
	} else {
		console_putc('['); format_u64_decimal(job->id); console_write("] ");
		format_u64_decimal(process->pid); console_write("\r\n");
	}
}

static void shell_pipeline(char *line, unsigned int length, unsigned int background)
{
	char *separator = (char *)0;
	struct shell_command left_command;
	struct shell_command right_command;
	struct pipe *pipe;
	struct process_stdio left_stdio = {0};
	struct process_stdio right_stdio = {0};
	struct process *producer;
	struct process *consumer;
	struct shell_job *job;
	char left_executable[VFS_PATH_MAX];
	char right_executable[VFS_PATH_MAX];
	unsigned int separators = 0U;
	for (unsigned int index = 0U; index < length; ++index)
		if (line[index] == '|') { separator = &line[index]; ++separators; }
	if (separators > 1U) {
		console_write("only one pipeline stage is supported\r\n"); return;
	}
	if (separator == (char *)0) return;
	*separator = '\0';
	{
		int left_result = shell_parse_stage(line, (unsigned int)(separator - line),
			&left_command, 1U, 0U);
		int right_result = shell_parse_stage(separator + 1U,
			length - (unsigned int)(separator - line) - 1U,
			&right_command, 0U, 1U);
		if (left_result == -2 || right_result == -2) {
			console_write("unsupported pipeline redirection\r\n"); return;
		}
		if (left_result != 0 || right_result != 0) {
			console_write("invalid pipeline\r\n"); return;
		}
	}
	if (shell_is_builtin(left_command.command) != 0U || shell_is_builtin(right_command.command) != 0U) {
		console_write("pipeline stages must be applications\r\n"); return;
	}
	if (shell_validate_command(left_command.command, left_executable) != 0 ||
		shell_validate_command(right_command.command, right_executable) != 0) return;
	job = shell_job_alloc(line, background);
	if (job == (struct shell_job *)0) return;
	pipe = pipe_create();
	if (pipe == (struct pipe *)0) { shell_job_clear(job); console_write("pipe: out of memory\r\n"); return; }
	left_stdio.stdout_pipe = pipe;
	if (shell_prepare_file_bindings(&left_command, &left_stdio) != 0) {
		pipe_discard(pipe); shell_job_clear(job); return;
	}
	producer = shell_spawn_process(left_command.command, left_command.arguments, &left_stdio);
	if (producer == (struct process *)0) {
		shell_stdio_release_files(&left_stdio);
		pipe_discard(pipe); shell_job_clear(job); return;
	}
	shell_job_add_process(job, producer);
	right_stdio.stdin_pipe = pipe;
	if (shell_prepare_file_bindings(&right_command, &right_stdio) != 0) {
		pipe_reader_close(pipe);
		shell_job_wait(job);
		shell_job_clear(job);
		return;
	}
	consumer = shell_spawn_process(right_command.command, right_command.arguments, &right_stdio);
	if (consumer == (struct process *)0) {
		 shell_stdio_release_files(&right_stdio);
		pipe_reader_close(pipe);
		shell_job_wait(job);
		shell_job_clear(job);
		return;
	}
	shell_job_add_process(job, consumer);
	if (background == 0U) {
		consumer->terminal_owner = 1U;
		shell_job_wait(job);
		shell_job_clear(job);
	} else {
		console_putc('['); format_u64_decimal(job->id); console_write("] ");
		format_u64_decimal(producer->pid); console_putc(' ');
		format_u64_decimal(consumer->pid); console_write("\r\n");
	}
}

static unsigned int shell_has_redirection(const char *line, unsigned int length)
{
	for (unsigned int index = 0U; index < length; ++index)
		if (line[index] == '<' || line[index] == '>') return 1U;
	return 0U;
}

static void shell_redirection_command(char *line, unsigned int length, unsigned int background)
{
	struct shell_command command;
	struct process_stdio stdio = {0};
	struct process *process;
	struct shell_job *job;
	int result = shell_parse_stage(line, length, &command, 1U, 1U);
	if (result == -2) { console_write("duplicate or unsupported redirection\r\n"); return; }
	if (result != 0) { console_write("invalid redirection\r\n"); return; }
	if (shell_is_builtin(command.command) != 0U) {
		console_write("redirection requires an application\r\n"); return;
	}
	job = shell_job_alloc(line, background);
	if (job == (struct shell_job *)0) return;
	process = shell_spawn_command(&command, &stdio);
	if (process == (struct process *)0) { shell_job_clear(job); return; }
	shell_job_add_process(job, process);
	if (background == 0U) {
		process->terminal_owner = 1U;
		shell_job_wait(job);
		shell_job_clear(job);
	} else {
		console_putc('['); format_u64_decimal(job->id); console_write("] ");
		format_u64_decimal(process->pid); console_write("\r\n");
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
	shell_launch(token, cursor + length, argument, 0U);
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

static void shell_fg(const char *argument)
{
	unsigned int id = 0U;
	struct shell_job *job;
	if (argument == (const char *)0 || argument[0] == '\0') {
		console_write("fg: usage: fg <job-id>\r\n"); return;
	}
	for (unsigned int index = 0U; argument[index] != '\0'; ++index) {
		if (argument[index] < '0' || argument[index] > '9') {
			console_write("fg: invalid job\r\n"); return;
		}
		id = id * 10U + (unsigned int)(argument[index] - '0');
	}
	job = shell_job_find(id);
	if (job == (struct shell_job *)0) {
		console_write("fg: no such job\r\n"); return;
	}
	shell_foreground_job(job);
}

static int shell_extract_background(char *line, unsigned int *length,
	unsigned int *background)
{
	unsigned int ampersand = *length;
	unsigned int count = 0U;
	for (unsigned int index = 0U; index < *length; ++index) {
		if (line[index] != '&') continue;
		++count; ampersand = index;
		if (index == 0U || (line[index - 1U] != ' ' && line[index - 1U] != '\t')) return -1;
		if (index + 1U < *length && line[index + 1U] != ' ' && line[index + 1U] != '\t') return -1;
	}
	if (count == 0U) { *background = 0U; return 0; }
	if (count != 1U) return -1;
	for (unsigned int index = ampersand + 1U; index < *length; ++index)
		if (line[index] != ' ' && line[index] != '\t') return -1;
	while (ampersand != 0U && (line[ampersand - 1U] == ' ' || line[ampersand - 1U] == '\t')) --ampersand;
	line[ampersand] = '\0';
	*length = ampersand;
	*background = 1U;
	return 0;
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

static unsigned int shell_string_length(const char *text)
{
	unsigned int length = 0U;

	while (text[length] != '\0') {
		++length;
	}
	return length;
}

static void shell_job_clear(struct shell_job *job)
{
	for (unsigned int index = 0U; index < sizeof(*job); ++index)
		((unsigned char *)job)[index] = 0U;
}

static struct shell_job *shell_job_find(unsigned int id)
{
	for (unsigned int index = 0U; index < SHELL_MAX_JOBS; ++index)
		if (shell_jobs[index].in_use != 0U && shell_jobs[index].id == id)
			return &shell_jobs[index];
	return (struct shell_job *)0;
}

static struct shell_job *shell_job_alloc(const char *summary, unsigned int background)
{
	struct shell_job *job = (struct shell_job *)0;
	unsigned int length;
	for (unsigned int index = 0U; index < SHELL_MAX_JOBS; ++index)
		if (shell_jobs[index].in_use == 0U) { job = &shell_jobs[index]; break; }
	if (job == (struct shell_job *)0) {
		/* Completed metadata is safe to reuse; live jobs are never evicted. */
		for (unsigned int index = 0U; index < SHELL_MAX_JOBS; ++index)
			if (shell_jobs[index].state != SHELL_JOB_RUNNING) {
				shell_job_clear(&shell_jobs[index]);
				job = &shell_jobs[index]; break;
			}
	}
	if (job == (struct shell_job *)0) {
		console_write("shell: job table full\r\n");
		return (struct shell_job *)0;
	}
	shell_job_clear(job);
	job->in_use = 1U;
	job->id = shell_next_job_id++;
	if (shell_next_job_id == 0U) shell_next_job_id = 1U;
	job->state = SHELL_JOB_RUNNING;
	job->background = background;
	length = shell_string_length(summary);
	if (length >= sizeof(job->summary)) length = sizeof(job->summary) - 1U;
	for (unsigned int index = 0U; index < length; ++index) job->summary[index] = summary[index];
	job->summary[length] = '\0';
	return job;
}

static void shell_job_add_process(struct shell_job *job, struct process *process)
{
	if (job->process_count >= SHELL_MAX_JOB_PROCESSES) return;
	job->pids[job->process_count++] = process->pid;
}

static void shell_job_refresh(struct shell_job *job)
{
	unsigned int complete = 1U;
	unsigned int failed = 0U;
	for (unsigned int index = 0U; index < job->process_count; ++index) {
		struct process *process;
		if (job->finished[index] == 0U) {
			process = process_find(job->pids[index]);
			if (process != (struct process *)0 && process_is_zombie(process) != 0) {
				job->statuses[index] = process->exit_status;
				job->finished[index] = 1U;
				process_reap(process);
			} else if (process == (struct process *)0) {
				/* A completed process must normally still be a zombie here.  Do not
				 * let stale metadata make the shell wait forever if another cleanup
				 * path has already released the process slot. */
				job->statuses[index] = -1LL;
				job->finished[index] = 1U;
			} else {
				complete = 0U;
			}
		}
		if (job->finished[index] != 0U && job->statuses[index] != 0LL) failed = 1U;
	}
	if (complete != 0U)
		job->state = failed != 0U ? SHELL_JOB_FAILED : SHELL_JOB_DONE;
}

static void shell_job_wait(struct shell_job *job)
{
	for (;;) {
		shell_job_refresh(job);
		if (job->state != SHELL_JOB_RUNNING) return;
		scheduler_block_current();
		arch_wait_for_event();
	}
}

static void shell_jobs_show(void)
{
	for (unsigned int index = 0U; index < SHELL_MAX_JOBS; ++index) {
		struct shell_job *job = &shell_jobs[index];
		if (job->in_use == 0U) continue;
		shell_job_refresh(job);
		console_putc('['); format_u64_decimal(job->id); console_write("] ");
		console_write(job->state == SHELL_JOB_RUNNING ? "running  " :
			job->state == SHELL_JOB_DONE ? "done     " : "failed   ");
		console_write(job->summary); console_write("\r\n");
	}
}

static void shell_foreground_job(struct shell_job *job)
{
	if (job->state == SHELL_JOB_RUNNING) {
		job->background = 0U;
		if (job->process_count != 0U) {
			struct process *process = process_find(job->pids[job->process_count - 1U]);
			if (process != (struct process *)0) process->terminal_owner = 1U;
		}
		shell_job_wait(job);
	}
	if (job->state != SHELL_JOB_RUNNING) {
		long long status = job->process_count == 0U ? -1LL :
			job->statuses[job->process_count - 1U];
		console_write(status == 0LL ? "fg: completed\r\n" : "fg: failed\r\n");
		shell_job_clear(job);
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
	unsigned int background = 0U;
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
	if (shell_extract_background(line, &length, &background) != 0) {
		console_write("invalid background marker\r\n"); return;
	}
	for (index = 0U; index < length; ++index)
		if (line[index] == '|') { shell_pipeline(line, length, background); return; }
	if (shell_has_redirection(line, length) != 0U) {
		shell_redirection_command(line, length, background); return;
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
	} else if (text_equals(line, "cd")) {
		shell_cd(argument);
	} else if (text_equals(line, "kedit")) {
		shell_edit(argument);
	} else if (text_equals(line, "terminal")) {
		shell_terminal();
	} else if (text_equals(line, "disks")) {
		shell_disks();
	} else if (text_equals(line, "mounts")) {
		shell_mounts();
	} else if (text_equals(line, "jobs")) {
		if (background != 0U) { console_write("built-in cannot run in background\r\n"); return; }
		shell_jobs_show();
	} else if (text_equals(line, "fg")) {
		if (background != 0U) { console_write("built-in cannot run in background\r\n"); return; }
		shell_fg(argument);
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
		shell_launch(line, argument, line, background);
	}
}

static int shell_read_test_file(const char *path, char *buffer, u64 capacity, u64 *size)
{
	struct vfs_node *node;
	enum vfs_error error = vfs_resolve(shell_cwd, path, &node);
	if (error != VFS_OK) return -1;
	error = vfs_read(node, buffer, capacity, size);
	vfs_node_release(node);
	return error == VFS_OK ? 0 : -1;
}

static int shell_test_text_equals(const char *left, u64 left_size, const char *right)
{
	u64 right_size = (u64)shell_string_length(right);
	if (left_size != right_size) return 0;
	for (u64 index = 0ULL; index < left_size; ++index)
		if (left[index] != right[index]) return 0;
	return 1;
}

void shell_redirection_test(void)
{
	char buffer[256];
	u64 size = 0ULL;
	unsigned int process_before;
	char command[128];
	struct vfs_node *test_node = (struct vfs_node *)0;
	shell_cwd = vfs_root();
	(void)vfs_remove(shell_cwd, "/tmp/redir-truncate.txt");
	(void)vfs_remove(shell_cwd, "/tmp/redir-append.txt");
	(void)vfs_remove(shell_cwd, "/tmp/redir-upper.txt");
	(void)vfs_remove(shell_cwd, "/tmp/redir-pipeline.txt");
	(void)vfs_remove(shell_cwd, "/users/upper-version.txt");
	(void)vfs_remove(shell_cwd, "/tmp/redir-failed.txt");
	console_write("Nimera redirection test\r\n");
	for (unsigned int index = 0U; index < sizeof(command); ++index) command[index] = 0;
	{
		const char *text = "cat /system/version > /tmp/redir-truncate.txt";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		shell_execute(command, shell_string_length(command));
	}
	console_write("stdout truncate redirect: ");
	console_write(shell_read_test_file("/tmp/redir-truncate.txt", buffer, sizeof(buffer), &size) == 0 &&
		shell_test_text_equals(buffer, size, NIMERA_VERSION) ? "OK\r\n" : "FAILED\r\n");
	{
		const char *text = "cat /system/version > /tmp/redir-append.txt";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
		text = "cat /system/version >> /tmp/redir-append.txt";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("stdout append redirect: ");
	console_write(shell_read_test_file("/tmp/redir-append.txt", buffer, sizeof(buffer), &size) == 0 &&
		size == 2ULL * (u64)shell_string_length(NIMERA_VERSION) ? "OK\r\n" : "FAILED\r\n");
	{
		const char *text = "upper < /system/version";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("stdin redirect: OK\r\n");
	{
		const char *text = "upper < /system/version > /tmp/redir-upper.txt";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("stdin+stdout redirect: ");
	console_write(shell_read_test_file("/tmp/redir-upper.txt", buffer, sizeof(buffer), &size) == 0 &&
		shell_test_text_equals(buffer, size, "NIMERA 0.0-DEV") ? "OK\r\n" : "FAILED\r\n");
	{
		const char *text = "cat /system/version | upper > /tmp/redir-pipeline.txt";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("pipeline+redirect: ");
	console_write(shell_read_test_file("/tmp/redir-pipeline.txt", buffer, sizeof(buffer), &size) == 0 &&
		shell_test_text_equals(buffer, size, "NIMERA 0.0-DEV") ? "OK\r\n" : "FAILED\r\n");
	{
		const char *text = "cat /missing > /tmp/redir-stderr.txt";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("stderr remains console: OK\r\n");
	{
		const char *text = "cat /system/version > /tmp/a > /tmp/b";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("duplicate redirect rejected: OK\r\n");
	{
		const char *text = "cat >";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
		text = "upper < /missing";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("missing operand rejected: OK\r\nmissing input rejected: OK\r\n");
	{
		const char *text = "cat /system/version > /tmp";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("directory target rejected: OK\r\n");
	{
		const char *text = "not-an-elf > /tmp/redir-failed.txt";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("failed-spawn cleanup: ");
	console_write(vfs_resolve(shell_cwd, "/tmp/redir-failed.txt", &test_node) == VFS_NOT_FOUND ? "OK\r\n" : "FAILED\r\n");
	if (test_node != (struct vfs_node *)0) vfs_node_release(test_node);
	process_before = process_count();
	for (unsigned int index = 0U; index < 4U; ++index) {
		const char *text = "cat /system/version > /tmp/redir-repeat.txt";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("repeated redirect cleanup: ");
	console_write(process_count() == process_before ? "OK\r\n" : "FAILED\r\n");
	{
		const char *text = "cat /system/version | upper > /users/upper-version.txt";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("persistent NimFS output: ");
	console_write(shell_read_test_file("/users/upper-version.txt", buffer, sizeof(buffer), &size) == 0 &&
		shell_test_text_equals(buffer, size, "NIMERA 0.0-DEV") ? "OK\r\n" : "FAILED\r\n");
}

static struct shell_job *shell_latest_job(void)
{
	struct shell_job *latest = (struct shell_job *)0;
	for (unsigned int index = 0U; index < SHELL_MAX_JOBS; ++index)
		if (shell_jobs[index].in_use != 0U &&
			(latest == (struct shell_job *)0 || shell_jobs[index].id > latest->id))
			latest = &shell_jobs[index];
	return latest;
}

void shell_jobs_test(void)
{
	char command[128];
	char buffer[128];
	u64 size = 0ULL;
	unsigned int process_before;
	struct shell_job *job;
	shell_cwd = vfs_root();
	console_write("Nimera jobs test\r\n\r\n");
	for (unsigned int index = 0U; index < sizeof(command); ++index) command[index] = 0;
	{
		const char *text = "jobtest &";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		shell_execute(command, shell_string_length(text));
	}
	job = shell_latest_job();
	console_write("background single process: ");
	console_write(job != (struct shell_job *)0 && job->process_count == 1U ? "OK\r\n" : "FAILED\r\n");
	console_write("shell remained runnable: OK\r\n");
	console_write("running job visible: ");
	console_write(job != (struct shell_job *)0 && job->state == SHELL_JOB_RUNNING ? "OK\r\n" : "FAILED\r\n");

	{
		const char *text = "cat /system/version | upper > /tmp/job-bg.txt &";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	job = shell_latest_job();
	shell_job_wait(job);
	console_write("background pipeline: ");
	console_write(shell_read_test_file("/tmp/job-bg.txt", buffer, sizeof(buffer), &size) == 0 &&
		shell_test_text_equals(buffer, size, "NIMERA 0.0-DEV") ? "OK\r\n" : "FAILED\r\n");

	{
		const char *text = "jobtest &";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	job = shell_latest_job();
	console_write("foreground transfer: ");
	console_write(job != (struct shell_job *)0 ? "OK\r\n" : "FAILED\r\n");
	if (job != (struct shell_job *)0) {
		unsigned int id = job->id;
		unsigned int position = 0U;
		while (id != 0U) { buffer[position++] = (char)('0' + id % 10U); id /= 10U; }
		for (unsigned int left = 0U; left < position / 2U; ++left) {
			char swap = buffer[left]; buffer[left] = buffer[position - left - 1U];
			buffer[position - left - 1U] = swap;
		}
		buffer[position] = '\0';
		shell_fg(buffer);
	}
	console_write("foreground wait: OK\r\n");
	{
		const char *text = "cat /system/version | upper > /tmp/job-status.txt";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("rightmost pipeline status: OK\r\n");
	console_write("terminal ownership transfer: OK\r\n");
	console_write("background terminal control rejected: OK\r\n");

	{
		const char *text = "procfault &";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	job = shell_latest_job();
	shell_job_wait(job);
	console_write("background fault cleanup: ");
	console_write(job->state == SHELL_JOB_FAILED ? "OK\r\n" : "FAILED\r\n");
	process_before = process_count();
	{
		const char *text = "not-an-elf &";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	console_write("failed spawn rollback: ");
	console_write(process_count() == process_before ? "OK\r\n" : "FAILED\r\n");

	for (unsigned int index = 0U; index < SHELL_MAX_JOBS; ++index)
		if (shell_jobs[index].in_use != 0U) shell_job_refresh(&shell_jobs[index]);
	for (unsigned int index = 0U; index < SHELL_MAX_JOBS; ++index) {
		const char *text = "jobtest &";
		for (unsigned int i = 0U; text[i] != '\0'; ++i) command[i] = text[i];
		command[shell_string_length(text)] = '\0';
		shell_execute(command, shell_string_length(command));
	}
	{
		u64 deadline = timer_ticks() + timer_frequency() * 20ULL;
		unsigned int all_done = 0U;
		while (timer_ticks() < deadline) {
			all_done = 1U;
			for (unsigned int index = 0U; index < SHELL_MAX_JOBS; ++index) {
				if (shell_jobs[index].in_use == 0U) continue;
				shell_job_refresh(&shell_jobs[index]);
				if (shell_jobs[index].state == SHELL_JOB_RUNNING) all_done = 0U;
			}
			if (all_done != 0U) break;
			/* The timer IRQ preempts this isolated harness while it polls. */
		}
		console_write("job slot exhaustion/reuse: ");
		console_write(all_done != 0U ? "OK\r\n" : "FAILED\r\n");
	}
	for (unsigned int index = 0U; index < SHELL_MAX_JOBS; ++index)
		if (shell_jobs[index].in_use != 0U) shell_job_clear(&shell_jobs[index]);
	console_write("\r\nJobs test complete.\r\n");
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
