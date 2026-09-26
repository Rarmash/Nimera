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

There is currently no libc, `malloc/free`, persistent filesystem, userspace,
processes, UART TX interrupt path, or other larger OS subsystem. The current
filesystem is RAM-only and disappears on reboot.
The MMU
is enabled after early initialization, but this is not yet a general virtual
memory manager.

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
9. An IRQ vector stub saves all general-purpose registers plus `ELR_EL1` and
   `SPSR_EL1`, dispatches the GIC timer or UART interrupt, and returns with
   `eret`. A timer IRQ may replace the saved frame with another kernel thread's
   frame before that return.
10. The shell reads input through the Console API, collects one fixed-size line,
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

The input path is interrupt-driven at the UART receive boundary. The UART IRQ
handler drains the PL011 FIFO into the fixed ring and wakes a shell thread that
is in `WAITING` state. The scheduler does not switch directly from the UART
IRQ; the next timer IRQ performs the normal scheduling decision. An empty RX
ring is checked and changed to `WAITING` with IRQs disabled, so an input byte
cannot create a lost wakeup. `wfe` is only the parking instruction used after
the thread has blocked, not a runnable polling loop. There is no history,
autocomplete, cursor movement, shell scripting, or command registry. TX still
polls PL011 readiness.

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
directory operations; the current backend is RAMFS. The root filesystem is
mounted during boot and creates this real namespace:

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
version string. `ls`, `pwd`, `cd`, `mkdir`, `cat`, `touch`, `write`, `append`,
`rm`, `rmdir`, and `mv` use the VFS resolver, so
relative paths, `.`, `..`, repeated slashes, and root clamping are real path
operations rather than shell-only output. `ls` reports `Not a directory` when
given a regular file.

RAMFS metadata and file contents use the existing kernel heap. They are not
persistent: all entries and contents disappear when QEMU stops. `write`
replaces exact bytes and `append` adds exact bytes without an implicit newline.
`rm` removes regular files; `rmdir` only removes empty directories; and `mv`
requires a new, non-existing destination and rejects directory cycles. There
are no permissions, ownership, timestamps, or recursive removal yet. `NimFS` is
reserved for a future persistent native filesystem. `/volumes` is currently an
ordinary empty directory reserved for future automounts, and `/devices` is an
ordinary directory, not yet a devfs.

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
They are compiled into the kernel; there is no persistent disk filesystem,
userspace, or external program execution.

The common console API is intentionally only three operations:
`console_putc()`, `console_write()`, and `console_getc()`. It keeps kernel code
independent of the physical console device; the current implementation is a
thin delegation layer, not a driver framework or HAL. PL011 registers and
platform-specific details remain in `platform/qemu-virt/uart.c`.

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
ignored safely. Parsing recognizes the fourteen commands shown above; there
is no quoting, escaping, piping, redirection, history, or external command
execution.

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
│       ├── exception.h
│       ├── format.h
│       ├── halt.h
│       ├── heap.h
│       ├── irq.h
│       ├── memory.h
│       ├── mmu.h
│       ├── panic.h
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
└── platform/
    └── qemu-virt/
        ├── gic.c
        ├── irq.c
        ├── memory.c
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
- `include/nimera/scheduler.h` — the fixed kernel-thread and saved IRQ-frame
  API used by the small preemptive scheduler.
- `include/nimera/vfs.h` — the small filesystem node, path, directory, read,
  and error API used by the kernel and shell.
- `include/nimera/ramfs.h` — the current RAMFS root creation interface; it is
  not the future persistent NimFS interface.
- `include/nimera/timer.h` — the platform-independent timer API.
- `include/nimera/version.h` — the source-controlled `Nimera 0.0-dev` version.
- `include/nimera/types.h` — the minimal freestanding `u64` type definition.
- `kernel/console.c` — delegates the common console API to the current UART
  implementation.
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
  editing.
- `kernel/panic.c` — prints the panic report through Console API and halts in
  a simple `wfe` loop.
- `kernel/pmm.c` — bitmap physical page manager initialized from the memory
  map; it has no heap or virtual-memory responsibilities.
- `kernel/scheduler.c` — the two-thread round-robin scheduler, synthetic worker
  context, `WAITING`/wakeup transitions, stack checks, and isolated scheduler
  tests.
- `kernel/vfs.c` — the root mount, shared parent/basename path helper, VFS
  dispatch, mutation policy, boot-created directories, and `/system/version`
  creation.
- `kernel/ramfs.c` — the heap-backed in-memory directory/file nodes, geometric
  file-buffer growth, child unlinking, renaming, and minimal VFS operations.
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
  wakes the blocked console consumer.
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
  `run-vfs-write`, and `clean`.
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
- `-DNIMERA_VFS_TEST=0` keeps the normal shell path out of the VFS test;
  `make run-vfs` enables it in `build-vfs/`.
- `-DNIMERA_VFS_WRITE_TEST=0` keeps the mutable VFS test out of the normal
  shell path; `make run-vfs-write` enables it in `build-vfs-write/`.
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
