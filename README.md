# Nimera

Nimera is a from-scratch, hobby/community-oriented operating system project.
It is being developed from the bottom up to make the mechanics of computers
and operating systems understandable through small, testable steps.

The first public milestone targets bare-metal AArch64 on QEMU's `virt`
machine. The code is written in C with a minimal amount of AArch64 assembly.
It does not use libc, an existing kernel, or a third-party runtime.

This is an early single-developer project. The codebase is intentionally small
and is not presented as a complete operating system.

## What works today

The current milestone successfully:

- builds a freestanding AArch64 ELF;
- loads that ELF directly with QEMU's generic loader;
- establishes a private initial stack;
- enters `kernel_main()`;
- routes kernel I/O through a minimal platform-independent console API;
- writes boot and shell output through QEMU `virt`'s PL011 UART;
- accepts interrupt-driven UART input and echoes printable input while editing
  a line;
- provides a minimal kernel panic path that reports a reason and halts safely;
- reads the AArch64 Generic Timer to measure monotonic elapsed time; and
- discovers the physical RAM region from QEMU's Device Tree Blob;
- discovers QEMU `virtio,mmio` devices from the same Device Tree and exposes
  synchronous `disk0`, `disk1`, ... block devices when `virtio-blk-device`
  drives are attached;
- reads and writes 512-byte sectors through a small modern VirtIO split queue,
  with a persistent marker test on a reserved last sector;
- separates physical RAM from clipped/merged reserved ranges, computes usable
  ranges, and manages their full 4 KiB pages with a bitmap PMM;
- provides a small kernel heap layered on PMM-backed pages with aligned
  first-fit blocks, splitting, and local coalescing;
- enables a minimal EL1 AArch64 stage-1 MMU with identity-mapped RAM and the
  PL011 UART;
- applies initial W^X-style permissions: `.text` is RO+X, `.rodata` is RO+NX,
  and writable RAM, heap, stack, page tables, and UART are NX;
- installs a minimal AArch64 exception vector table for the current execution
  level; and
- discovers the QEMU `virt` GICv2 and handles the EL1 Generic Timer through a
  real hardware IRQ; and
- receives PL011 UART input through a GIC-delivered RX interrupt and fixed ring
  buffer while retaining polling TX; and
- runs a small built-in kernel shell; and
- preemptively switches between the shell thread and one background kernel
  worker on the Generic Timer IRQ using a small round-robin scheduler; and
- blocks the shell thread while the RX ring is empty, then wakes it from the
  PL011 receive IRQ without performing an immediate context switch; and
- mounts a small in-memory RAMFS at `/`, creates the initial Nimera directory
  tree, and exposes it through a minimal VFS and mutable shell file commands.
- decodes serial input into bounded logical key events and provides a small
  terminal screen-control API backed by host ANSI sequences.
- queries ANSI host terminal geometry once at boot, using detected dimensions
  when available and falling back to 80x25 when it is not.
- provides NimEdit 0.1, a small built-in kernel text editor with VFS-backed
  load/save, ASCII editing, cursor movement, vertical scrolling, dirty-state
  tracking, and Ctrl-S/Ctrl-Q controls.
- provides NimFS v0, a versioned native persistent filesystem over a whole
  VirtIO block device. Regular files, directories, overwrite, append, rename,
  unlink, and empty-directory removal use the same VFS as RAMFS; an additional
  NimFS volume can be mounted below `/volumes` with a persistent bounded label;
  volumes can be logically ejected and remounted without formatting; and
- loads a separate freestanding AArch64 ELF executable from `/apps/hello`
  through the VFS, maps its `PT_LOAD` segments with EL0 permissions, runs it,
  and reclaims its user pages after `SYS_exit`.
- provides a standalone EL0 NimEdit 0.2 at `/apps/edit`, using only the public
  terminal, file, and page-allocation syscalls; its editor buffer grows in
  user memory and is released automatically when the task exits.
- provides EL0 filesystem utilities at `/apps/ls`, `/apps/mkdir`, `/apps/touch`,
  `/apps/rm`, `/apps/rmdir`, `/apps/mv`, `/apps/pwd`, `/apps/write`, and
  `/apps/append`, backed by public directory and mutation syscalls.
- supports the first shell redirections `<`, `>`, and `>>` by binding VFS files
  to process standard handles, including redirection on the right side of the
  existing one-stage pipeline.

The isolated `run-user` milestone also provides the first embedded EL0 task and a small
syscall boundary. The user image is linked into the kernel ELF, but its
`.user.text`, `.user.rodata`, `.user.data`, and `.user.bss` sections receive
separate permissions: EL0 text is read-only/executable, while user data and
the 16 KiB user stack are read-write/non-executable. Kernel code and MMIO
remain EL1-only. The embedded task is still a regression fixture; file-backed
ELF applications now run as process objects with private address spaces.

There is currently no libc, dynamic linker, filesystem syscall,
UART TX interrupt path, or other larger OS subsystem. The current
RAMFS root is RAM-only and disappears on reboot; the separate NimFS boot mode
uses the persistent disk image described below. The MMU
The MMU
is enabled after early initialization, but this is not yet a general virtual
memory manager.

## First ELF executable

The first file-backed userspace program is a separately linked ELF64
`ET_EXEC` for AArch64. It has no libc, CRT, kernel symbol references, or
dynamic linker. Its startup is `_start -> main -> SYS_exit`; output uses the
public `SYS_write_console` ABI. The development payload is installed as the
exact ELF bytes into NimFS `/apps/hello` by an explicitly selected test build.
The loader itself only resolves and reads that path through the VFS.

The loader reads program headers, not section headers. It currently accepts
only little-endian ELF64, AArch64, `ET_EXEC`, and `PT_LOAD` segments. It checks
file and virtual-address ranges, `p_filesz <= p_memsz`, executable entry,
segment alignment, and rejects writable+executable pages. A fixed user virtual
range `0x10000000..0x20000000` is backed by fresh PMM pages: RX segments become
EL0 read-only/executable, RW segments become EL0 read-write/NX, and read-only
segments become EL0 read-only/NX. The loader zeroes BSS tails and creates a
16 KiB EL0 stack with one unmapped guard page below it.

Each dynamically loaded user task is a process with a PID (PID 0 is invalid),
its own cwd, handles, dynamic allocations, ELF pages, stack, and AArch64
TTBR0 address space. Kernel EL1 mappings are cloned into every process, while
user mappings are private. Processes have no ASIDs and switches conservatively
flush the EL1 TLB. Exit and EL0 faults first make a process a zombie; cleanup
and page-table destruction happen only after the scheduler has switched away.
There is no `fork`, `exec`, relocations, PIE, shared libraries, or background
shell job control. The current loader image limit is 64 KiB and the current test
program demonstrates both console output and zero-initialized BSS.

## Boot flow

`make run` starts QEMU with the `virt` machine and loads
`build/baremetal-aarch64.elf` using:

```text
-device loader,file=build/baremetal-aarch64.elf,cpu-num=0
```

The generic loader reads the ELF program headers, copies the loadable segments
into guest RAM, and starts CPU 0 at the ELF entry point. The linker places the
image at `0x40100000`. QEMU's `virt` machine starts RAM at `0x40000000` and
places its default device tree blob in the first `0x100000` bytes, so the image
starts just after that area.

The entry point is `_start` in `arch/aarch64/boot.S`:

1. `_start` computes the linker-defined `__stack_top` address.
2. It moves that address into the AArch64 stack pointer, `sp`.
3. It calls `exception_init()`, which reads `CurrentEL` and installs the
   matching `VBAR_ELx` vector base.
4. It calls `kernel_main()` in `kernel/main.c`.
5. `kernel_main()` initializes the timer, discovers the memory map, initializes
   the PMM, builds identity page tables, enables the MMU, and initializes the
   heap.
6. The console delegates output to PL011 TX polling; its input is filled by
   the PL011 RX IRQ into a fixed software ring buffer.
7. `irq_init()` discovers the GICv2 distributor, CPU interface, PL011 range,
   UART SPI, and architected timer PPI from the DTB, maps the MMIO pages as
   Device memory, programs a 10 Hz absolute-deadline timer, and enables IRQs
   only after setup.
8. The fixed shell and worker thread table is initialized, then IRQs are
   enabled.
9. `terminal_init()` sends the ANSI `CSI 18 t` query once. A valid
   `CSI 8;<rows>;<columns>t` response updates the terminal API; timeout,
   malformed input, or unreasonable dimensions keep the 80x25 fallback.
   Unrelated bytes remain in a bounded terminal pending queue.
10. An IRQ vector stub saves all general-purpose registers plus `ELR_EL1` and
   `SPSR_EL1`, dispatches the GIC timer or UART interrupt, and returns with
   `eret`. A timer IRQ may replace the saved frame with another kernel thread's
   frame before that return.
11. The shell reads input through the Console API, collects one fixed-size line,
   parses one of its built-in commands, and prints the next prompt.

With the current QEMU `virt` plus generic-loader invocation, `CurrentEL` was
verified at runtime as `EL1`. Nimera therefore installs `VBAR_EL1`; it does not
perform an EL2-to-EL1 transition or assume one is needed.

The vector table is a fixed, 2048-byte-aligned AArch64 table. Its synchronous,
IRQ, FIQ, and SError slots enter small assembly stubs. Synchronous exceptions
are reported diagnostically. The IRQ slot saves all general-purpose registers,
calls the minimal GICv2/timer/UART handler, restores them, and returns with
`eret`;
FIQ and SError still use the fatal path.

For a synchronous exception, `ESR_EL1` identifies the reason, `ELR_EL1` is the
instruction address to which execution would return, and `FAR_EL1` is the
faulting address when the exception supplies one. The report also shows the
exception class (`EC`) and then halts the CPU. The analogous EL2 registers are
used if the kernel is later started there.

The shell is started directly by the kernel. It is not a user process and does
not depend on a filesystem, current working directory, userspace, or external
programs.

In NimFS boot mode, `disk0` is mounted as `/` first. Additional discovered
block devices are probed without formatting them. A valid secondary NimFS is
mounted at `/volumes/<label>` when it has a label, or at a deterministic device
name such as `/volumes/disk1` when it does not. Duplicate labels receive names
such as `Data-2`; existing non-empty user directories are never overwritten.
An unformatted or corrupt device is reported and skipped. VFS lookup follows
this mount boundary transparently. Removing a mountpoint or renaming across
filesystem boundaries is rejected.

`eject /volumes/Data` performs a logical unmount. It refuses a busy current
working directory and never removes the underlying block device from the
registry; `mount disk1` can mount a valid existing NimFS again and never formats
it. `mounts` shows the path, filesystem, device, and label. This is lifecycle
management for the current development registry, not hardware hotplug.

The input path is interrupt-driven at the UART receive boundary. The UART IRQ
handler drains the PL011 FIFO into the fixed ring and wakes a shell thread that
is in `WAITING` state. The scheduler does not switch directly from the UART
IRQ; the next timer IRQ performs the normal scheduling decision. An empty RX
ring is checked and changed to `WAITING` with IRQs disabled, so an input byte
cannot create a lost wakeup. `wfe` is only the parking instruction used after
the thread has blocked, not a runnable polling loop. There is no history,
autocomplete, cursor movement, shell scripting, or command registry. TX still
polls PL011 readiness.

The terminal layer sits above that path:

```text
PL011 IRQ -> RX ring -> Console API -> logical key events -> shell/future editor
```

`terminal_read_key()` recognizes printable ASCII, Enter, Backspace, Escape,
cursor keys, Home, End, Delete, and Ctrl+S/Ctrl+Q. Common ANSI sequences such
as `ESC [ A`, `ESC [ D`, `ESC [ 3 ~`, and `ESC O H/F` are parsed by a bounded
state machine. An isolated Escape has finite lookahead, so it cannot leave the
parser waiting forever; unknown sequences reset the parser state.

The shell command `terminal` reports the current ANSI size and whether the
one-shot query was detected or fell back.

The terminal screen API provides clear-screen, cursor movement, line clearing,
cursor visibility, and runtime rows/columns. The QEMU serial backend implements
these operations with ANSI/VT escape sequences from the host terminal. At
initialization it sends `CSI 18 t` and accepts only the bounded response form
`CSI 8;<rows>;<columns>t`; valid dimensions are 20..500 columns and 10..200
rows. The fallback is 80x25, and failure to detect geometry never panics or
blocks boot indefinitely. Unrelated bytes read while recognizing the response
are retained in a small terminal-level FIFO before normal key decoding.
The ordinary interactive `make run`, `make run-editor`, and `make run-nimfs`
targets select this 80x25 fallback directly because some host terminals do
not implement `CSI 18 t` cleanly; the negotiation remains available through
the dedicated terminal-size targets.
NimEdit uses this logical API rather than knowing PL011 registers or the ANSI
protocol. Geometry is detected once per boot; live resize is not implemented.
The framebuffer backend is available through the dedicated graphics targets;
there is still no Unicode input or general VT100 emulator.

## First EL0 userspace and syscalls

The kernel remains in AArch64 EL1. The test task runs in EL0t with a separate
`SP_EL0`; exception entry continues to use the EL1 stack. A synchronous EL0
exception recognizes `SVC` (`ESR_EL1.EC = 0x15`) and dispatches the syscall
without entering the fatal kernel-exception path. The minimal Nimera ABI is:

```text
x8       syscall number
x0..x5   arguments
x0       return value
```

The ABI currently provides `SYS_write_console = 1`, `SYS_exit = 2`, file calls
`SYS_open`, `SYS_read`, `SYS_write`, `SYS_close`, and the directory/filesystem
utility calls described below. Paths
are passed as pointer-plus-length pairs; the kernel validates and copies them.
Each dynamic user task has a fixed 16-entry handle table containing its VFS
node, current offset, and access flags. Handles are released on close and on
task exit. User code calls small assembly stubs and does not link against
kernel symbols or libc. Bad user pointers return an error instead of panicking.

The loader now constructs an argv array and strings on the user stack. `run`
splits at spaces/tabs, supports at most eight arguments and 256 argument bytes,
and passes `argc`, `argv`, and a terminating null pointer in the usual AArch64
registers. Quoting, environment variables, and redirection are not implemented.

The same saved exception frame is used for EL1 kernel threads and EL0 tasks.
It contains the general registers, `ELR_EL1`, `SPSR_EL1`, `SP_EL0`, `ESR_EL1`,
and `FAR_EL1`. A timer IRQ arriving while EL0 executes saves that frame and
the scheduler can resume another thread before later returning with `eret` to
EL0. `SYS_exit` marks the current process a zombie and deferred reaping
releases its resources.

Run the isolated tests with:

```sh
make run-user
make run-user-protection
```

The first target demonstrates SVC output, timer preemption, register/local
integrity, and exit. The second attempts to write kernel `.data`; the expected
result is a `Data Abort from EL0`, termination of that task, and a surviving
kernel. Invalid EL1 faults still use the existing fatal panic/halt policy.
The embedded user image is still only a regression fixture; it is not loaded
from a file.

Build the separate test executable with:

```sh
make user-app
```

This produces `build-user-app/hello.elf`; it is not linked into the ordinary
kernel ELF. To create a fresh isolated NimFS image and install its exact bytes
at `/apps/hello`, run:

```sh
make run-elf-format
```

This explicitly reformats `build-storage/nimfs-elf.img` and is destructive to
that development image. Afterward, `make run-elf` mounts the same image
without formatting. In its shell, run:

```text
nimera:/ $ run /apps/hello
hello from ELF userspace!
BSS zero: OK
nimera:/ $
```

For a non-interactive loader regression, `make run-elf-test` boots the same
image and waits for `/apps/hello` to exit automatically. The normal shell
path remains `run /apps/hello`; the loader only reads the file through VFS.
`make user-app` also builds `/apps/cat` and `/apps/filetest`. The isolated
`make run-user-files` target runs `filetest`, which exercises open/create,
truncate, multi-call writes, read-back, and close through the VFS syscall
boundary. Its image must first be formatted with `make run-elf-format`.

For the standalone editor path, `make run-user-editor-format` creates a fresh
development image containing `/apps/edit`; stop it after the format report,
then `make run-user-editor` boots a direct EL0 editor test. The ordinary shell
path can launch the same ELF with `edit /path` after an image containing the
payload has been formatted. These targets use isolated kernel build
directories, so the editor test define cannot leak into `make run`.
`make run-user-editor-test` runs the same ELF in a non-interactive model test;
it checks insertion, the expected `hello from nimedit!` buffer, navigation, and
allocation cleanup.

Basic filesystem utilities are also standalone EL0 applications: `/apps/ls`,
`/apps/mkdir`, `/apps/touch`, `/apps/rm`, `/apps/rmdir`, `/apps/mv`,
`/apps/pwd`, `/apps/write`, and `/apps/append`.
Their public ABI uses explicit directory handles and fixed-size
`nimera_dir_entry` records; it does not expose `struct vfs_node` or backend
internals. `make run-user-utils` boots a development NimFS image containing
these payloads and runs a short automated create/write/append/read/directory/
rename/delete regression. The shell no longer
implements these commands, so `which ls` reports `/apps/ls`.

`cd` remains a shell built-in because it changes the shell's working directory.
`pwd`, `write`, and `append` are now external EL0 programs. Each new user task
inherits the shell's current directory as a canonical path; `SYS_getcwd` reads
that task-local value, so an application does not consult shell state directly.
The directory syscalls are Nimera-specific and intentionally much smaller than
a POSIX syscall surface.

## External command resolution

The shell separates built-ins from ELF applications. Built-ins such as `cd`,
`kedit`, and `run` are handled in the kernel. `edit` resolves to the
standalone userspace ELF at `/apps/edit`; `cat` is another userspace ELF at
`/apps/cat`, not a kernel-side command.

A command containing `/` is executed as that explicit path; a relative path is
resolved from the current directory. A command without `/` is searched only as
`/apps/<name>`; there is no environment or configurable `PATH`. `run <path>
[args...]` remains an explicit/debug form but uses the same launcher. `which`
reports either a built-in or the `/apps` path. Missing commands, directories,
and invalid ELF files have separate diagnostics.

After formatting the ELF development image with `make run-elf-format`, run the
isolated resolution test with `make run-commands`. It covers built-in lookup,
`/apps` lookup, userspace `cat`, direct `hello` execution with arguments,
explicit `run`, invalid executables, directories, and missing commands. This
is still a small shell path: quoting, environment, and search outside `/apps`
are not implemented. One pipe and the limited `<`, `>`, `>>` redirections are
available as described above.

## Userspace terminal ABI

EL0 applications can use a small logical terminal ABI without knowing about
PL011 registers or ANSI escape sequences. Syscalls 7–12 provide logical key
input, terminal size, clear screen, cursor movement, line clearing, and cursor
visibility. Rows and columns are zero-based; invalid coordinates and invalid
user pointers are rejected by the kernel. `SYS_write_console` remains the
simple text-output syscall.

The userspace memory ABI adds `SYS_mem_alloc` and `SYS_mem_free`. Requests are
rounded to pages in a dedicated virtual range (`0x18000000..0x1f000000`),
separate from ELF segments and the user stack. The kernel tracks live mappings
per process, rejects invalid or double frees, and releases all remaining
mappings when that process exits. This is a small allocation ABI, not a heap
or a general virtual-memory manager.

The filesystem ABI adds `SYS_open_directory`, `SYS_read_directory`,
`SYS_mkdir`, `SYS_unlink`, `SYS_rmdir`, and `SYS_rename`. Directory handles
share the task's bounded handle table with file handles but carry a directory
type and enumeration offset. `SYS_read_directory` returns one fixed-width
entry or zero at end-of-directory. The kernel closes all remaining handles on
EL0 task exit.

`SYS_getcwd` is syscall 21 and `SYS_getpid` is syscall 22. `getpid` returns
the current process PID. `SYS_getcwd` copies the bounded, NUL-terminated inherited
working-directory path into a validated user-writable buffer. The utility
`write` uses truncate/create plus the existing file-write ABI; `append` uses
create/append. The kernel's file-write syscall already loops over bounded
chunks, so these small applications do not need a libc or a formatting layer.

`/apps/keytest` is a standalone freestanding ELF that draws a small full-screen
test UI, waits for decoded key events, and exits on Ctrl-Q. Only one foreground
EL0 application may own terminal input. If it blocks waiting for a key, the
shell and application use the same IRQ-safe single input-waiter path while the
kernel worker continues to receive timer time. On normal exit or an EL0 fault,
the kernel restores the cursor before returning to the shell. `/apps/faulttest`
is a development payload for checking that cleanup path.

The terminal application payloads are installed only by an explicit test build.
To create a fresh development image containing them, run
`make run-terminal-app-format` and stop QEMU after the format report. Then
`make run-terminal-app` boots the direct keytest path, or use the normal shell
and enter `keytest`. `make run-terminal-fault` runs the cursor-cleanup fault
test against the same image. These targets use separate build directories and
do not change the normal RAMFS shell build. `make run-user-terminal` runs the
non-interactive bad-pointer and coordinate-validation checks.

## Processes and address spaces

`make run-processes` uses an isolated build and a freshly formatted development
NimFS image. It starts two `/apps/proctest` processes at the same time and a
fault-isolation child. The test prints different physical backing pages for
the same user virtual address, lets the timer preempt both processes, confirms
that an EL0 Data Abort terminates only the faulting child, and then reaps all
three processes. `/apps/pidtest` demonstrates the `SYS_getpid` ABI.

There is one user thread per process in this milestone. Kernel threads have no
process and use the kernel address space. TTBR1, ASIDs, SMP, `fork`, `exec`,
`waitpid`, and a process-creation syscall are intentionally not implemented.

Range I/O is the small common VFS extension used by these syscalls. RAMFS and
NimFS support reads and writes at a file offset, including partial sectors and
file extension without sparse holes. It is not a cache or a general file API.

## Standard streams and pipes

Every EL0 process has the fixed standard handles 0 (`stdin`), 1 (`stdout`),
and 2 (`stderr`). Standard output and error initially target the console;
standard input is initially unavailable. Ordinary dynamically opened handles
start at 3. Handles are typed by the kernel, so a VFS file, console output,
pipe reader, and pipe writer cannot be confused by an application.

The pipe implementation is a bounded in-kernel byte ring (4000 bytes). A
reader waiting for data and a writer waiting for space block through the
scheduler instead of spinning. Closing the last writer produces EOF; writing
without readers reports a broken pipe. Endpoint references are released at
process exit as well as explicit close, so a reader can observe EOF before the
shell reaps a zombie.

The shell supports one simple pipeline operator, for example:

```text
cat /system/version | upper
```

`/apps/cat` reads either a path or standard input, and `/apps/upper` converts
ASCII lowercase input to uppercase. `make run-pipes` runs an isolated
concurrent-process test and prints the transformed stream. This is a small
Nimera stream API, not a claim of POSIX compatibility.

The shell also supports the deliberately small redirection grammar `<`, `>`,
and `>>` when operators are separate whitespace-delimited tokens:

```text
upper < /system/version
cat /system/version > /tmp/version.txt
cat /system/version >> /tmp/version.txt
cat /system/version | upper > /tmp/upper.txt
```

`>` creates or truncates a file; `>>` creates or appends to it. In a one-stage
pipeline, input redirection is allowed on the left command and output
redirection on the right command. Redirection prepares the initial process
handles 0 and 1, so applications continue to use ordinary `SYS_READ` and
`SYS_WRITE` calls. Handle ownership is transferred on successful spawn and
rolled back on failure. Handle 2 remains connected to the console.
`make run-redirection` runs the isolated acceptance test, including a
persistent NimFS output proof.

This is intentionally not a POSIX shell: there is no quoting, escaping, `2>` or
`>&`, arbitrary file descriptors, heredoc, command substitution, or multiple
pipelines. Operators must be separate tokens.

## Background jobs

The shell has a small fixed job table layered over the existing process and
pipe code. A job is shell metadata, while a process is the actual schedulable
EL0 task; jobs retain PIDs and exit statuses rather than pointers to process
objects. A final `&` starts one application or one two-process pipeline in the
background, returns the prompt immediately, and leaves its stdout/stderr on the
console. `jobs` reports running, done, or failed entries, and `fg <job-id>`
gives a running job the terminal and waits for it. Only the rightmost process
of a foreground pipeline owns terminal-control operations. There are no
signals, process groups, stopping, or background terminal input yet.

`make run-jobs` runs the isolated job acceptance path with `/apps/jobtest`.

## First graphics output

`make run-graphics` adds QEMU's `virtio-gpu-device` alongside the existing
VirtIO block device. The platform driver discovers the GPU through the
VirtIO-MMIO nodes described by the Device Tree, asks it for display information,
and uses scanout 0. Nimera allocates a guest-owned, writable and non-executable
XRGB-like 32-bit framebuffer, draws a small software test pattern, and sends it
to the device with the 2D commands `TRANSFER_TO_HOST_2D` and `RESOURCE_FLUSH`.
The current pixel layout is B8G8R8X8 in little-endian memory; the alpha byte is
unused. The UART shell remains the only interactive console.

This is a display output path, not a GUI, window system, compositor, graphical
terminal, or userspace graphics API. The GPU is optional: ordinary `make run`
continues to work without it, while `make run-graphics` opens a QEMU graphics
window and also checks that block and GPU VirtIO devices coexist.

## Framebuffer terminal

`make run-fb-terminal` selects a framebuffer terminal backend for the existing
logical terminal API. The backend keeps a character-cell grid, renders a small
bitmap font into the VirtIO-GPU framebuffer, flushes changed cells, and draws a
software cursor. At the current `1280x800` mode the grid is `80x50` using
`16x16` cells; the dimensions are derived from the display and font metrics,
not hardcoded terminal geometry.

The ordinary `make run` path remains the ANSI/PL011 backend. In framebuffer
mode, shell and EL0 application output uses the same terminal API and ABI as
before. `make run-fb-terminal` also adds QEMU `virtio-keyboard-device` and
`virtio-tablet-device`: keyboard IRQs are translated into the existing
hardware-independent logical key events, while the tablet has its own bounded
pointer-event queue. Its advertised ABS_X/ABS_Y ranges are scaled and clamped
to framebuffer pixels, and the framebuffer terminal draws a small software
cursor over its cell grid. Pointer button events are decoded but are not yet
used by the shell or terminal. The current keyboard translation uses a fixed
US layout and supports the shell/editor keys, Shift, and Ctrl-S/Ctrl-Q. UART
remains the debug and panic/rescue console; if the optional keyboard is absent,
the framebuffer path falls back to UART input. This is pointer input and
software cursor support, not a GUI, compositor, window manager, or userspace
input-device API.

`make run-native-input-test` runs the isolated keyboard model/discovery test.
It checks the VirtIO-MMIO keyboard, fixed event queue, printable/Shift/Ctrl
translation, navigation events, and the IRQ wakeup path.

`make run-fb-terminal-test` runs the existing logical terminal test against the
framebuffer backend. It exercises printable characters, control operations,
cursor visibility, clearing, and scrolling in an isolated build.

For the shell plus installed EL0 applications, prepare the development NimFS
image once with `make run-elf-format` (stop QEMU after the format report), then
use `make run-fb-terminal`. The image is not reformatted by the framebuffer
terminal target.

`edit <path>` launches the standalone EL0 NimEdit 0.2 from `/apps/edit`. It
loads or creates a VFS file, uses a growable flat ASCII byte buffer in
userspace memory, and supports Enter, Backspace, Delete, arrows, Home, End,
preferred-column vertical movement, clipping, scrolling, dirty tracking,
Ctrl-S, and the double-Ctrl-Q dirty exit guard. Its title is
`NimEdit 0.2 - <path>`. The editor knows only the public userspace ABI: it has
no kernel headers, kernel editor calls, or direct ANSI escape strings.
`kedit <path>` remains the legacy kernel editor regression entry point. RAMFS
files remain ephemeral; use a NimFS boot target for persistence.

## NimFS v0

NimFS is Nimera's first native persistent filesystem. Each image uses its
entire block device as one volume; there are no partitions. The format is explicitly
versioned as format version 1 and uses little-endian field serialization rather
than relying on host C struct padding.

For a 64 MiB image the layout is:

```text
block 0       superblock (magic NMFS, version 1)
blocks 1..4   allocation bitmap (one bit per 512-byte block)
blocks 5..132 fixed inode table (256 inodes, 256 bytes each)
block 133..   data blocks
```

Each inode has 60 direct data-block references, so the current maximum regular
file size is 30 KiB. Directory entries are fixed 64-byte records with a
bounded 58-byte on-disk component name. The bitmap and inode table are cached
in RAM for the running kernel, but every allocation and inode change is written
back through the block API to the owning disk; they are not a RAM mirror used as the
authoritative store.

The original single-volume development image is `build-storage/nimfs.img`.
The multi-volume targets use `build-storage/nimfs-root.img` and
`build-storage/nimfs-data.img`, each 64 MiB. The volume lifecycle test also
uses `build-storage/nimfs-data2.img` to verify duplicate-label naming.
Formatting is always explicit and destructive:

```sh
make nimfs-disk-reset
make run-nimfs-format
make run-nimfs
```

`run-nimfs-format` creates the superblock, bitmap, inode table, root inode, and
the initial `/system`, `/apps`, `/users`, `/volumes`, `/devices`, `/config`,
`/var`, and `/tmp` tree. `run-nimfs` only mounts an existing valid image; it
does not reformat it. The NimFS shell also provides `mounts` and `fsinfo`.
For the multi-volume development path use `make nimfs-root-create`,
`make nimfs-data-create`, `make run-nimfs-multi-format`, then
`make run-nimfs-multi`. `make run-mounts` resets both images, formats them in
an isolated test build, and checks transparent traversal, mountpoint
protection, and cross-filesystem rename rejection. `make run-volume` uses three
images and checks labels, deterministic `Data-2` naming, busy-cwd protection,
unmount cleanup, remount persistence, and the root-unmount guard. The explicit
multi-volume format path is `make run-nimfs-data-format`.

NimFS format version 1 stores a maximum 31-byte ASCII volume label in the
previously reserved part of the superblock, so older v1 images remain valid
and simply appear unlabeled. Labels cannot contain control characters, `/`,
`.` or `..`. `/tmp`, `/devices`, and `/volumes` are ordinary NimFS directories
for now; tmpfs, devfs, and hardware hotplug are future work.

NimFS v0 has no journal or crash recovery. A power loss or QEMU termination
during metadata writes may corrupt the image. It also does not implement
permissions, timestamps, free-page management, or partitions.

The isolated editor self-test can be run with:

```sh
make run-editor
```

It exercises buffer growth and reallocation, editing and preferred-column
navigation, load/save/reopen, dirty tracking, and heap cleanup without a fake
keyboard input stream.

The panic and exception paths share a small architecture-specific `cpu_halt()`
primitive. `panic()` writes a fatal message and reason through the common
console API, prints `System halted.`, and then remains in a CPU-local `wfe`
loop. It does not use UART or PL011 symbols directly.

The timer path is also separate from the normal echo flow. The common timer API
provides the counter frequency, the current counter value, and monotonic uptime
in milliseconds. Only `arch/aarch64/timer.c` reads the AArch64 timer system
registers; kernel code does not contain `mrs` instructions. The same
architectural timer is also armed as an EL1 physical-timer IRQ, but the normal
shell does not print every tick.

The counter is a continuously increasing hardware tick value. Its frequency is
the number of counter ticks per second. `timer_init()` records the current
counter value as Nimera's boot/reference tick, and `timer_uptime_ms()` converts
the difference from that reference into elapsed milliseconds. `timer_ticks()`
still exposes the raw hardware counter.

This is monotonic uptime, not a clock showing the current time of day: Nimera
does not yet have an RTC, calendar date, wall clock, or timezone handling.
Calling `timer_uptime_ms()` before `timer_init()` triggers `panic()` rather than
returning an uninitialized value.

## First hardware IRQ

The current QEMU `virt,gic-version=2` machine exposes a GICv2. The platform
DTB parser discovers the GIC distributor and CPU-interface MMIO ranges and the
non-secure EL1 physical timer PPI; these values are not guessed in the driver.
`arch/aarch64/mmu.c` maps the discovered UART and both GIC ranges as
Device-nGnRnE, read/write, non-executable memory before enabling the MMU.

After setup, the kernel clears the AArch64 `DAIF.I` IRQ mask, programs
`CNTP_CVAL_EL0` for an absolute 10 Hz deadline, enables the timer PPI, and
handles each interrupt by acknowledging it, incrementing a `volatile`
single-core counter, rearming the next absolute deadline, and writing
end-of-interrupt. `volatile` makes each C access observe the object rather than
being optimized into a cached value; it is not a general SMP synchronization
primitive, and Nimera has no SMP or locking support yet.

`make run-irq` waits for five actual timer IRQs with `wfe`, rather than polling
the counter, and then reports the observed count. The normal shell's `ticks`
command reads the same count. This is an interrupt demonstration, not a
scheduler or a general interrupt framework.

## Kernel threads and preemption

The scheduler currently has exactly two fixed kernel threads: the shell thread
and a background worker. Both run in EL1 on one CPU. The worker only increments
a counter; it does not print, allocate memory, or handle input. Each timer IRQ
round-robins to the other READY thread by saving the full general-purpose IRQ
frame and returning through `eret` with the selected frame. UART IRQs do not
cause scheduling.

The worker has a static 16 KiB stack in writable, non-executable `.bss`. A
synthetic initial frame enters a small assembly trampoline, which calls the
worker entry function and panics if it ever returns. The scheduler test checks
that the worker counter increases, context switches occur, local test values
survive preemption, and the worker stack canary remains intact:

```sh
make run-sched
```

The scheduler distinguishes `RUNNING`, `READY`, and `WAITING`. A blocked shell
thread is skipped while the worker remains runnable. If no thread is READY, a
minimal fallback returns to the current parked frame so an interrupt can make
work runnable again; this is not a separate power-management subsystem.
These are kernel threads, not processes: there are no address spaces, userspace
stacks, SMP, generic wait queues, locks, or other synchronization primitives.

The blocking path can be exercised with one input character:

```sh
make run-blocking
```

It reports that the worker progressed while the shell was waiting and then
checks that the shell resumed after the UART wakeup.

## VFS and RAMFS

Nimera now has a small virtual filesystem layer. The shell calls VFS path and
directory operations. Normal `make run` uses RAMFS; `make run-nimfs` mounts
NimFS from `disk0`. Both backends expose this root namespace:

```text
/
├── system/
├── apps/
├── users/
├── volumes/
├── devices/
├── config/
├── var/
└── tmp/
```

`/system/version` is a regular RAMFS file containing the canonical Nimera
version string. `ls`, `pwd`, `cd`, `mkdir`, `touch`, `write`, `append`,
`rm`, `rmdir`, and `mv` use the VFS resolver, so
relative paths, `.`, `..`, repeated slashes, and root clamping are real path
operations rather than shell-only output. `ls` reports `Not a directory` when
given a regular file.

RAMFS metadata and file contents use the existing kernel heap and disappear
when QEMU stops. NimFS stores corresponding metadata and contents on its raw
image. `write`
replaces exact bytes and `append` adds exact bytes without an implicit newline.
`rm` removes regular files; `rmdir` only removes empty directories; and `mv`
requires a new, non-existing destination and rejects directory cycles. There
are no permissions, ownership, timestamps, or recursive removal yet.
In normal RAMFS mode `/volumes` remains an ordinary directory. In NimFS boot
mode it is the location for the current boot-time secondary-volume probe;
`/devices` remains an ordinary directory, not yet a devfs.

The isolated test is:

```sh
make run-vfs
make run-vfs-write
```

## PL011 receive IRQ

The current QEMU DTB describes PL011 as `compatible = "arm,pl011"` with
`reg = <0 0x9000000 0 0x1000>` and `interrupts = <0 1 4>`. In the GIC binding,
type `0` means SPI, raw interrupt number `1` becomes GIC INTID `32 + 1 = 33`,
and flag `4` means level-high. Nimera discovers these fields; it does not
hardcode UART INTID 33.

The driver enables PL011 `IMSC` bits RX (`bit 4`) and receive-timeout RT
(`bit 6`). RT is useful when a short input burst does not reach the FIFO
threshold. The handler checks `MIS` (`0x40`), drains `DR` (`0x00`) until
`FR.RXFE` (`bit 4`) says the FIFO is empty, and acknowledges RX/RT through
`ICR` (`0x44`). It never prints characters from interrupt context.

Received bytes enter a 256-byte storage array with volatile `head` and `tail`
indices. One slot distinguishes full from empty, so 255 bytes can be pending.
On overflow the newest byte is dropped and a counter is incremented; unread
bytes are never overwritten. The producer publishes the byte before `head`,
and the consumer advances `tail` after reading it, with compiler barriers for
the current single-core interrupt model. `volatile` controls compiler memory
accesses; it is not a universal SMP synchronization primitive.

`console_getc()` no longer reads PL011 registers or busy-spins. It checks the
software queue with IRQs disabled; when empty, it marks the current scheduler
thread `WAITING`, restores the previous IRQ state, and parks with `wfe`. The
IRQ handler executes `sev` after publishing data and changes the waiting shell
thread to `READY`. If the IRQ arrives between the empty check and the waiting
transition, it is held pending until the transition is complete, so the
wakeup cannot be lost. The `sev` event also wakes a parked CPU; the actual
context switch back to a shell that was already switched out remains
timer-driven.

## Physical memory discovery

QEMU's Device Tree Blob (DTB) is a small structured description of the virtual
machine: it tells software which devices exist and which physical memory
regions are present. With the current `-machine virt,gic-version=2` and
generic-loader boot
command, QEMU places the DTB at `0x40000000`; this was verified directly when
QEMU reported the DTB occupying `0x40000000..0x40100000` while diagnosing the
ELF load address. The kernel image starts at `0x40100000`, after that DTB area.

The QEMU `virt` parser checks the DTB magic and bounds, reads its big-endian
fields, follows the structure block, and uses the root `#address-cells` and
`#size-cells` values to decode the memory node's `reg` property. The common API
also reads the FDT memory reservation block. QEMU's current DTB has an empty
reservation block, which is handled as a valid case; the DTB itself is still
reserved using its actual header `totalsize`.

This is physical memory discovery, not memory management. Physical memory is
what the machine reports. The memory map now reserves the exact kernel linker
range and the DTB range from its FDT `totalsize`, plus entries from the FDT
memory reservation block. Overlapping or adjacent reservations are merged, and
ranges outside RAM are clipped or ignored conservatively.

The terms have deliberately narrow meanings here:

- physical memory is the RAM range reported by the machine;
- reserved memory is RAM Nimera must not hand to the PMM;
- usable memory is physical RAM after those reservations are subtracted;
- managed memory is the page-aligned part of usable memory after PMM bitmap
  metadata pages are removed;
- allocated memory is managed pages currently marked in use by the PMM; and
- free memory is managed pages currently available from the PMM.

Usable memory is represented as multiple ranges when reservations split the
physical range. The PMM manages only complete 4 KiB pages in those ranges. It
does not provide a heap or general virtual-memory manager.

The kernel heap is a separate layer above PMM. PMM manages fixed 4 KiB physical
pages, while the heap returns smaller aligned blocks such as 1, 32, or 1000
bytes. When the heap has no suitable block, it obtains another page from PMM.
The current heap uses identity-mapped pointers: the virtual address numerically
equals the physical address, even though the MMU is now enabled. `kfree()` makes
a block reusable inside the heap, but completely unused heap pages are not yet
returned to PMM.

## Minimal MMU bring-up

After PMM initialization, `arch/aarch64/mmu.c` allocates its own 4 KiB page
tables from PMM, not from the heap. It maps the discovered physical RAM using
2 MiB blocks where possible, uses 4 KiB mappings at edges, and maps the PL011
UART page at `0x09000000` as Device-nGnRnE memory. RAM is described as Normal
write-back memory because RAM and device registers have different ordering and
caching rules.

The mapping is identity-based: an access to `0x40000000` still reaches
physical `0x40000000`, so this milestone does not need address relocation. The
MMU uses TTBR0 only; there is no high-half mapping, TTBR1 address space,
userspace address space, demand paging, or general virtual-memory allocator.
`MAIR_EL1` assigns index 0 to Normal write-back RAM and index 1 to Device
nGnRnE. `TCR_EL1` selects 4 KiB granules and a 48-bit TTBR0 address space;
`TTBR1` walks are disabled. The setup uses barriers and invalidates stale
translation state before changing `SCTLR_EL1`. The original `SCTLR_EL1` value
is preserved and only its M bit is changed to enable translation. The MMU test verifies RAM, UART, and timer access, while
`make run-mmu-fault` deliberately accesses an unmapped address to exercise the
existing exception path.

The protection build creates final permissions before enabling the MMU. The
linker aligns `.text`, `.rodata`, `.data`, `.bss`, and the stack to 4 KiB
boundaries and exports their start/end symbols. Only the 2 MiB block containing
the kernel is split into L3 pages where section permissions differ; the rest
of RAM remains block-mapped.

R/W/X permissions describe whether a page may be read, written, or executed.
NX means non-executable and prevents data from being used as code. This small
W^X-style policy makes `.text` non-writable and writable memory non-executable;
it is protection for the EL1 kernel, not userspace isolation.

The normal terminal starts with:

```text
Nimera booting...
MMU: enabled
irq: enabled
kernel: starting shell
nimera $
```

The built-in commands include `help`, `echo`, `uptime`, `ticks`, `irqs`, `mem`,
`threads`, `counter`, `version`, and the VFS commands listed above. `ticks` reports the number of handled EL1 timer IRQs, while `irqs`
also reports UART RX IRQ and dropped-byte counters.
Built-ins are compiled into the kernel; applications are separate ELF files
loaded through VFS and the EL0 loader. Persistent storage is available only
through the explicit NimFS development boot target.

The common console API is intentionally only three operations:
`console_putc()`, `console_write()`, and `console_getc()`. It keeps kernel code
independent of the physical console device; the current implementation is a
thin delegation layer, not a driver framework or HAL. PL011 registers and
platform-specific details remain in `platform/qemu-virt/uart.c`.

The block layer is deliberately below the filesystem boundary. Its common API
knows only named devices, sector size/capacity, and one synchronous read/write
request. The QEMU backend discovers `virtio,mmio` nodes from the DTB and drives
modern VirtIO MMIO directly. It does not provide partitions, a block cache, a
filesystem, or VFS integration.

`make run-block` attaches `build-storage/nimera-test.img`, a persistent raw
64 MiB image. `make disk-create` creates it only when absent; `make disk-reset`
intentionally removes and recreates it. The isolated test uses the image's
last sector, never block 0: the first run writes a marker and a second run
reports `Persistent marker: present`. Normal `make run` does not attach or
require this image. The shell's `disks` command only reports discovered
devices; RAMFS remains the root filesystem for normal `make run`, while
`make run-nimfs` mounts the separate persistent NimFS image.

Device Tree is the machine's hardware inventory: QEMU hands the kernel a
binary table saying which memory and MMIO devices exist and where they live.
Nimera currently parses only the fields needed for QEMU `virtio,mmio` and
`memory` nodes. Physical memory is the RAM region reported by that table; it
is not the same as usable memory after reservations, and neither is the same
as currently free memory. NimFS storage has its own whole-disk layout; this
memory-discovery section does not describe free storage or allocator state.

## Requirements on macOS Apple Silicon

Install Apple's Command Line Tools if they are not already present:

```sh
xcode-select --install
```

Install the Homebrew toolchain and QEMU:

```sh
brew install llvm lld qemu
```

Homebrew distributes `ld.lld` separately from the `llvm` formula. The Makefile
therefore uses these native Apple Silicon paths by default:

- Clang: `/opt/homebrew/opt/llvm/bin/clang`
- LLD: `/opt/homebrew/opt/lld/bin/ld.lld`
- QEMU: `qemu-system-aarch64` from `PATH`

If Homebrew is installed elsewhere, override the paths when invoking Make:

```sh
make build LLVM_PREFIX=/path/to/llvm LLD_PREFIX=/path/to/lld
```

No libc or host C++ libraries are needed by the kernel. The host tools are used
only to assemble, compile, link, and emulate the freestanding image.

## Build and run

From the repository root:

```sh
make build
make run
```

The normal `make run` path does not trigger a panic. To exercise the panic
runtime without editing source files, use:

```sh
make run-panic
```

This target builds an isolated panic-test image in `build-panic/` with
`PANIC_TEST=1`, leaving the ordinary `build/` artifacts untouched. It should
print:

```text
Nimera kernel panic
Reason: panic test
System halted.
```

The `noreturn` attribute on `panic()` tells Clang that the function cannot
return to its caller. This matches the permanent halt loop and lets the
compiler reason correctly about control flow.

To exercise the Generic Timer polling test, use:

```sh
make run-timer
```

This builds an isolated image in `build-timer/` with `TIMER_TEST=1`. It waits
for three approximately one-second intervals using the architectural counter:

```text
Nimera timer test
tick
tick
tick
Timer test complete.
```

To exercise the first real hardware IRQ path, use:

```sh
make run-irq QEMU_MEMORY=128M
```

It discovers the GIC and timer from the DTB, waits for five timer interrupts
without polling the counter, and prints output similar to:

```text
Nimera IRQ test
GIC: v2
Distributor: 0x8000000
CPU interface: 0x8010000
EL1 physical timer INTID: 30
IRQs enabled
Timer IRQ ticks: 5
IRQ test complete.
```

To exercise interrupt-driven PL011 input, use:

```sh
make run-uart-irq QEMU_MEMORY=128M
```

Type five characters, without requiring Enter. The isolated image reports the
DTB-derived UART base and INTID, then shows the bytes that travelled through
the IRQ handler and ring buffer:

```text
Nimera UART IRQ test
UART base: 0x9000000
UART INTID: 33
RX IRQ enabled
Type 5 characters:

Received via IRQ: abcde
UART RX IRQs: 5
Dropped bytes: 0
UART IRQ test complete.
```

The controlled overflow check is available with `make run-uart-overflow`; it
fills the ring without touching PL011 and verifies that unread data is kept and
new bytes are dropped and counted.

To exercise physical memory discovery, use an isolated test image and choose
QEMU's RAM size:

```sh
make run-memory QEMU_MEMORY=128M
```

The test prints physical, reserved, and usable totals, the kernel and DTB
ranges, and the merged reserved ranges. `build-memory/` is kept separate from
the ordinary build.

The physical page manager uses a 4096-byte page/frame size. Each managed page
has one bitmap bit. The bitmap is sized from the actual usable page count and
is placed at the beginning of the first sufficiently large page-aligned usable
range. The complete pages occupied by the bitmap are removed from the managed
set, so the PMM cannot return its own metadata.

To exercise allocation and release directly, use the isolated PMM test:

```sh
make run-pmm QEMU_MEMORY=128M
```

The test allocates three distinct aligned pages, frees one, allocates again,
and prints managed/free/allocated statistics. The PMM returns an explicit
failure when no page remains; invalid frees and double frees call `panic()`.
`build-pmm/` is separate from the ordinary build.

To exercise the kernel heap, use:

```sh
make run-heap QEMU_MEMORY=128M
```

This test allocates blocks of several sizes, writes and verifies byte
patterns, demonstrates split/reuse after `kfree()`, and reports heap-reserved,
allocated, and reusable bytes. Heap backing pages come only from PMM; this is
not a libc allocator and it does not configure virtual memory. `build-heap/`
is kept separate from all other test builds.

To exercise the exception vector with a deliberate undefined instruction, use:

```sh
make run-exception
```

This builds an isolated image in `build-exception/` with
`EXCEPTION_TEST=1`. It prints the verified execution level and minimal
synchronous-exception diagnostics, then halts without returning to the shell.

To exercise the identity-mapped MMU, use:

```sh
make run-mmu QEMU_MEMORY=128M
```

This builds an isolated image in `build-mmu/`, reports the initial and final
`SCTLR_EL1`, page-table count, and basic RAM/UART/timer accesses. To verify the
negative path, use `make run-mmu-fault`; the resulting translation fault is
reported by the existing exception path and the CPU halts.

Protection tests use isolated build directories:

```sh
make run-protection QEMU_MEMORY=128M
make run-protection-write QEMU_MEMORY=128M
make run-protection-exec QEMU_MEMORY=128M
```

The first validates representative descriptors. The second must report a
permission Data Abort when writing `.text`; the third must report an
Instruction Abort when branching to a `ret` instruction stored in writable
`.data`. Neither test is an invalid-opcode test.

The ordinary `make run` starts the built-in kernel shell. Its line buffer is a
fixed 128-byte array: printable ASCII is echoed into it, Enter executes the
line, and Backspace removes the previous character. Input beyond the buffer is
ignored safely. Parsing has no quoting, escaping, or history; the implemented
operators are the single pipeline, simple file redirections, and a final `&`
for background jobs.

`make clean` removes generated objects, the ELF, and the link map:

```sh
make clean
```

QEMU runs without a graphical device and connects the emulated serial port to
the terminal. Stop it with `Ctrl-A`, then `X`.

## Repository structure

```text
.
├── README.md
├── Makefile
├── linker.ld
├── .gitignore
├── include/
│   └── nimera/
│       ├── console.h
│       ├── display.h
│       ├── graphics.h
│       ├── terminal_fb.h
│       ├── exception.h
│       ├── format.h
│       ├── halt.h
│       ├── heap.h
│       ├── irq.h
│       ├── memory.h
│       ├── mmu.h
│       ├── panic.h
│       ├── elf.h
│       ├── pmm.h
│       ├── scheduler.h
│       ├── shell.h
│       ├── ramfs.h
│       ├── timer.h
│       ├── types.h
│       ├── version.h
│       └── vfs.h
├── arch/
│   └── aarch64/
│       ├── boot.S
│       ├── exception.S
│       ├── exception.c
│       ├── halt.S
│       ├── mmu.c
│       ├── irq.c
│       └── timer.c
├── kernel/
│   ├── console.c
│   ├── elf.c
│   ├── exception.c
│   ├── format.c
│   ├── heap.c
│   ├── irq.c
│   ├── main.c
│   ├── memory.c
│   ├── panic.c
│   ├── pmm.c
│   ├── ramfs.c
│   ├── scheduler.c
│   ├── shell.c
│   ├── timer.c
│   └── vfs.c
├── user/
│   ├── include/nimera/user.h
│   ├── runtime/
│   │   ├── start.S
│   │   └── syscall.S
│   └── apps/
│       ├── hello/
│       ├── cat/
│       └── filetest/
└── platform/
    └── qemu-virt/
        ├── gic.c
        ├── irq.c
        ├── memory.c
        ├── virtio.c
        └── uart.c
```

- `arch/aarch64/boot.S` — installs the initial stack, initializes exception
  vectors, calls C, and provides the fallback loop if C returns.
- `arch/aarch64/exception.S` — the 2048-byte-aligned vector table and minimal
  register-capture stubs.
- `arch/aarch64/exception.c` — reads `CurrentEL`, installs `VBAR_EL1` or
  `VBAR_EL2`, and contains the deliberate exception-test instruction.
- `arch/aarch64/halt.S` — the small `wfe`-based `cpu_halt()` primitive.
- `arch/aarch64/irq.c` — enables/disables the AArch64 `DAIF.I` IRQ mask and
  provides the architecture-specific wait-for-event operation.
- `arch/aarch64/mmu.c` — builds minimal identity page tables from PMM, maps
  Normal RAM and the Device-nGnRnE PL011 page, splits the kernel block into
  permissioned L3 pages, validates representative permissions, and enables
  EL1 MMU translation.
- `include/nimera/console.h` — the small platform-independent console API.
- `include/nimera/block.h` — the minimal common sector block-device API.
- `include/nimera/virtio.h` — the QEMU platform VirtIO discovery API.
- `include/nimera/terminal.h` — logical key events, screen controls, runtime
  geometry, and the 80x25 fallback dimensions.
- `include/nimera/abi/terminal.h` — fixed-width user-visible key-event and
  terminal-size ABI structures; it contains no PL011 details.
- `include/nimera/exception.h` — exception initialization and fatal-report API.
- `include/nimera/format.h` — minimal unsigned decimal and hexadecimal output
  helpers used where a number must be displayed.
- `include/nimera/halt.h` — the architecture-neutral CPU halt declaration.
- `include/nimera/heap.h` — the kernel heap API and heap statistics.
- `include/nimera/irq.h` — the small common timer-IRQ API and platform-discovery
  structure.
- `include/nimera/shell.h` — the non-returning built-in shell entry point.
- `include/nimera/memory.h` — the common physical memory information API.
- `include/nimera/mmu.h` — the small MMU state, initialization, and test API.
- `include/nimera/panic.h` — the non-returning `panic()` API.
- `include/nimera/pmm.h` — the minimal physical page manager API and 4 KiB
  page-size constant.
- `include/nimera/process.h` — the fixed process object, PID, address-space,
  and deferred-reap interfaces.
- `include/nimera/scheduler.h` — the fixed kernel-thread and saved IRQ-frame
  API used by the small preemptive scheduler.
- `include/nimera/vfs.h` — the small filesystem node, path, directory, read,
  and error API used by the kernel and shell.
- `include/nimera/ramfs.h` — the current RAMFS root creation interface; it is
- `include/nimera/nimfs.h` — the native persistent NimFS format, mount, format,
  and diagnostic API.
- `include/nimera/timer.h` — the platform-independent timer API.
- `include/nimera/version.h` — the source-controlled `Nimera 0.0-dev` version.
- `include/nimera/types.h` — the minimal freestanding `u64` type definition.
- `kernel/console.c` — delegates the common console API to the current UART
  implementation.
- `kernel/display.c` — minimal platform-independent framebuffer/display state
  and rectangular flush API.
- `kernel/graphics.c` — bounds-safe software drawing primitives, tiny bitmap
  text rendering, and the first graphics test pattern.
- `kernel/terminal_fb.c` — framebuffer terminal cell grid, cursor overlay,
  scrolling, and cell-sized display flushes.
- `kernel/input.c` — bounded hardware-independent logical key-event queue and
  blocking input handoff.
- `include/nimera/input.h` — common key-event types and input queue API.
- `kernel/elf.c` — validates supported ELF64 program headers, builds the
  bounded argv stack, creates process-owned mappings, and owns per-process
  cwd, file handles, and dynamic allocations.
- `kernel/block.c` — registers and dispatches the small generic block-device
  set; it contains no VirtIO register knowledge.
- `kernel/nimfs.c` — the small versioned whole-disk filesystem and VFS backend;
  it contains no VirtIO queue knowledge.
- `user/runtime/` — the tiny freestanding user entry point and syscall stubs.
- `user/apps/` — separately linked `hello`, `cat`, file-syscall, `keytest`,
  `termcheck`, userspace `edit`, and filesystem utility ELF programs; artifacts
  are kept outside the source tree in `build-user-app/`.
- `user/apps/edit/` — the standalone NimEdit model and renderer. It includes
  only the public userspace ABI and no kernel headers.
- `user/runtime/fsutil.c` — tiny shared output/error helpers for filesystem
  utility apps; it is not a libc.
- `kernel/terminal.c` — bounded ANSI key decoding, one-shot geometry
  detection, fallback, and pending input; it does not access PL011 directly.
- `kernel/exception.c` — prints synchronous-exception diagnostics through the
  Console API and halts the CPU.
- `kernel/main.c` — defines `kernel_main()`, initializes the timer, and starts
  the shell or one of the isolated runtime tests; it does not call UART
  functions directly.
- `kernel/memory.c` — exposes the common memory API through the platform
  discovery implementation and prints the physical/reserved/usable report.
- `kernel/format.c` — only the small unsigned decimal/hex output helpers used
  by shell and memory test; it is not a `printf` implementation.
- `kernel/heap.c` — the small PMM-backed first-fit heap with block splitting,
  coalescing, and validation of frees.
- `kernel/shell.c` — fixed-buffer command shell using only the common Console
  API; it provides the built-in diagnostic and VFS commands plus basic line
  editing and the legacy `kedit` launcher; external `edit` resolves through
  `/apps`.
- `kernel/editor.c` — the legacy built-in `kedit` buffer, editing operations,
  terminal renderer, VFS load/save path, and isolated self-test.
- `kernel/panic.c` — prints the panic report through Console API and halts in
  a simple `wfe` loop.
- `kernel/pmm.c` — bitmap physical page manager initialized from the memory
  map; it has no heap or virtual-memory responsibilities.
- `kernel/scheduler.c` — the two-thread round-robin scheduler, synthetic worker
  context, `WAITING`/wakeup transitions, stack checks, and isolated scheduler
  tests.
- `kernel/vfs.c` — root and secondary mount records, mount-aware path
  traversal, shared parent/basename path helper, VFS dispatch, mutation
  policy, boot-created directories, and `/system/version` creation.
- `kernel/ramfs.c` — the heap-backed in-memory directory/file nodes, geometric
  file-buffer growth, child unlinking, renaming, and minimal VFS operations.
- `include/nimera/editor.h` — the small shell-to-editor entry-point API.
- `kernel/timer.c` — validates timer frequency and exposes frequency, ticks,
  and monotonic milliseconds without architecture instructions.
- `arch/aarch64/timer.c` — reads `CNTFRQ_EL0` and `CNTPCT_EL0` for the common
  timer layer and programs `CNTP_CVAL_EL0`/`CNTP_CTL_EL0` for the EL1 physical
  timer IRQ.
- `platform/qemu-virt/memory.c` — minimal QEMU `virt` FDT parser for the
  physical memory node, DTB reservations, kernel range, and usable gaps; it
  does not implement an allocator.
- `platform/qemu-virt/uart.c` — minimal PL011 MMIO input and output for QEMU
  `virt`; TX is polling, while RX drains into the fixed interrupt-side ring and
  wakes the blocked console consumer. It also exposes nonblocking access to
  already-buffered bytes for terminal sequence lookahead.
- `platform/qemu-virt/virtio.c` — bounded DTB discovery, shared modern VirtIO
  MMIO discovery, synchronous block I/O, and the minimal VirtIO-GPU 2D path.
- `platform/qemu-virt/irq.c` — minimal DTB discovery of the GICv2 MMIO ranges
  and the architected timer PPI.
- `platform/qemu-virt/gic.c` — minimal one-CPU GICv2 setup, acknowledge, and
  end-of-interrupt operations.
- `kernel/irq.c` — common timer IRQ counter and dispatch path, separate from
  GIC and PL011 details; it dispatches the timer and UART INTIDs explicitly,
  and asks the scheduler for a replacement frame on timer interrupts.
- `linker.ld` — defines `_start`, the fixed image address, ELF sections,
  `__kernel_start`/`__kernel_end`, page-aligned section boundaries, and a 16
  KiB private stack in `NOLOAD` `.bss`.
- `Makefile` — builds the freestanding objects and links them directly with
  LLD; provides `build`, `run`, `run-panic`, `run-timer`, `run-memory`,
  `run-pmm`, `run-heap`, `run-exception`, `run-mmu`, `run-mmu-fault`,
  `run-protection`, `run-protection-write`, `run-protection-exec`, `run-irq`,
  `run-uart-irq`, `run-uart-overflow`, `run-sched`, `run-blocking`, `run-vfs`,
  `run-vfs-write`, `run-terminal`, `run-terminal-size`,
  `run-terminal-size-fallback`, `run-editor`, `run-user-editor-format`,
  `run-user-editor`, `run-user-editor-test`, `run-user-utils`, `run-block`,
  `disk-create`,
  `disk-reset`, `run-terminal-app-format`, `run-terminal-app`,
  `run-terminal-fault`, `run-user-terminal`, `run-graphics`,
  `run-fb-terminal`, `run-fb-terminal-test`, and `clean`. Test builds use
  separate directories so their compile-time paths cannot contaminate `make
  run`; `run-jobs` uses `build-jobs/` for the background-job test.
- `README.md` — project status, workflow, and design notes.

## Why the build flags are explicit

This is a freestanding program rather than a hosted application:

- `--target=aarch64-none-elf` selects 64-bit ARM ELF output with no assumed OS.
- `-std=c11` selects the C language version used by the kernel.
- `-O2` enables normal optimization while keeping the image small.
- `-Wall -Wextra -Werror` makes common compiler warnings build failures.
- `-ffreestanding` tells Clang that there is no standard library and no
  hosted `main()` environment.
- `-fno-builtin` prevents implicit assumptions about libc functions.
- `-mgeneral-regs-only` prevents generated FP/SIMD instructions; this kernel
  has not enabled the corresponding AArch64 execution state yet.
- `-fno-stack-protector` avoids compiler-generated calls to stack-canary
  runtime support.
- `-fno-pic -fno-pie` avoids position-independent code and dynamic linking; the
  linker script supplies the fixed guest address.
- `-fno-asynchronous-unwind-tables -fno-unwind-tables` avoids unwind metadata
  that would require additional runtime support.
- `-Iinclude` makes the project's freestanding headers available without
  depending on host or libc headers.
- `-DNIMERA_PANIC_TEST=0`, `-DNIMERA_TIMER_TEST=0`,
  `-DNIMERA_MEMORY_TEST=0`, `-DNIMERA_EXCEPTION_TEST=0`,
  `-DNIMERA_PMM_TEST=0`, `-DNIMERA_HEAP_TEST=0`, `-DNIMERA_MMU_TEST=0`,
  `-DNIMERA_MMU_FAULT_TEST=0`, the three protection-test defines, and
  `-DNIMERA_IRQ_TEST=0` keep the normal build path free of test flows;
  dedicated Make targets enable their respective switch in isolated build
  directories. `NIMERA_UART_IRQ_TEST=0` and `NIMERA_UART_OVERFLOW_TEST=0`
  similarly keep UART-specific test paths out of the normal image.
- `-DNIMERA_SCHED_TEST=0` keeps the normal shell path out of the isolated
  scheduler test; `make run-sched` enables it in `build-sched/`.
- `-DNIMERA_BLOCKING_TEST=0` keeps the normal shell path out of the blocking
  test; `make run-blocking` enables it in `build-blocking/`.
- `-DNIMERA_PROCESS_TEST=0` keeps the multi-process harness out of the normal
  image; `make run-processes` enables it in the isolated `build-processes/`
  build and formats its explicit development image.
- `-DNIMERA_PIPE_TEST=0` keeps the concurrent pipe harness out of the normal
  image; `make run-pipes` enables it in the isolated `build-pipes/` build.
- `-DNIMERA_COMMAND_TEST=0` keeps external-command resolution out of the
  normal shell boot; `make run-commands` enables it in `build-commands/`.
- `-DNIMERA_JOBS_TEST=0` keeps the deterministic background-job acceptance
  flow out of the normal shell boot; `make run-jobs` enables it in the
  isolated `build-jobs/` build.
- `-DNIMERA_VFS_TEST=0` keeps the normal shell path out of the VFS test;
  `make run-vfs` enables it in `build-vfs/`.
- `-DNIMERA_VFS_WRITE_TEST=0` keeps the mutable VFS test out of the normal
  shell path; `make run-vfs-write` enables it in `build-vfs-write/`.
- `-DNIMERA_TERMINAL_TEST=0` keeps the interactive key/screen test out of the
  normal shell path; `make run-terminal` enables it in `build-terminal/`.
- `-DNIMERA_TERMINAL_SIZE_TEST=0` keeps the geometry diagnostic out of the
  normal shell path; `make run-terminal-size` enables it in
  `build-terminal-size/`, while `run-terminal-size-fallback` uses a separate
  build with the response disabled.
- `-DNIMERA_TERMINAL_CHECK_TEST=0` keeps non-interactive EL0 terminal ABI
  validation out of the normal image; `make run-user-terminal` enables it in
  `build-user-terminal/`.
- `-DNIMERA_USER_EDITOR_TEST=0` keeps the direct standalone editor runtime
  path out of the normal image; `make run-user-editor` enables it in
  `build-user-editor/`.
- `-DNIMERA_TERMINAL_APP_TEST=0` and `-DNIMERA_TERMINAL_FAULT_TEST=0` keep the
  EL0 terminal test flows out of the normal image; their explicit targets use
  isolated `build-terminal-*` directories and install the corresponding ELF
  payloads into the development NimFS image.
- `-DNIMERA_EDITOR_TEST=0` keeps the automated editor self-test out of the
  normal shell path; `make run-editor` enables it in `build-editor/`.
- `-DNIMERA_BLOCK_TEST=0` keeps the disk test out of the normal flow;
  `make run-block` rebuilds in `build-block/` and attaches the separate raw
  image.
- `-T linker.ld` supplies the complete memory layout and entry point.
- `-m aarch64elf` selects LLD's AArch64 ELF emulation.
- `-e _start` makes the assembly entry point explicit.
- `-z max-page-size=0x1000` keeps load-segment alignment small and predictable
  for this image.
- `-Map=...` writes a link map that makes the final layout and symbols easier
  to inspect.

The link is performed directly by `ld.lld`, so no startup objects, libc,
libgcc/compiler-rt, dynamic linker, or other third-party runtime is pulled in.

## License

Nimera is distributed under the Mozilla Public License 2.0 (MPL-2.0). See
[`LICENSE`](LICENSE) for the complete license text.

## Project direction

Nimera aims to:

- teach how computers and operating systems work from the bottom up;
- remain portable instead of becoming permanently tied to one board;
- eventually support systems ranging from constrained hardware to full
  computers;
- grow incrementally, without pretending to be a complete OS from day one; and
- become suitable for community contributions as the project matures.

The near-term roadmap will be updated as the next subsystem is selected. The
minimal built-in shell described above is implemented, but it is intentionally
not a userspace shell or a general command-execution environment.
