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
- accepts polling UART input and echoes printable input while editing a line;
- provides a minimal kernel panic path that reports a reason and halts safely;
- reads the AArch64 Generic Timer to measure monotonic elapsed time; and
- discovers the physical RAM region from QEMU's Device Tree Blob;
- separates physical RAM from clipped/merged reserved ranges and computes
  usable ranges without an allocator;
- installs a minimal AArch64 exception vector table for the current execution
  level; and
- runs a small built-in kernel shell.

There is currently no libc, allocator, MMU/page-table management, hardware
IRQ/GIC subsystem, scheduler, filesystem, userspace, or other larger OS
subsystem.

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
5. `kernel_main()` initializes the timer, prints the boot messages, and starts
   the built-in shell.
6. The current console implementation delegates to the QEMU `virt` PL011
   driver, which writes output to the data register at `0x09000000` and polls
   input status.
7. The shell reads input through the Console API, collects one fixed-size line,
   parses one of its built-in commands, and prints the next prompt.

With the current QEMU `virt` plus generic-loader invocation, `CurrentEL` was
verified at runtime as `EL1`. Nimera therefore installs `VBAR_EL1`; it does not
perform an EL2-to-EL1 transition or assume one is needed.

The vector table is a fixed, 2048-byte-aligned AArch64 table. Its synchronous,
IRQ, FIQ, and SError slots enter small assembly stubs. Synchronous exceptions
are reported diagnostically; the other categories currently use the same fatal
path and are not an IRQ implementation.

For a synchronous exception, `ESR_EL1` identifies the reason, `ELR_EL1` is the
instruction address to which execution would return, and `FAR_EL1` is the
faulting address when the exception supplies one. The report also shows the
exception class (`EC`) and then halts the CPU. The analogous EL2 registers are
used if the kernel is later started there.

The shell is started directly by the kernel. It is not a user process and does
not depend on a filesystem, current working directory, userspace, or external
programs.

The input path is intentionally polling-based. It has no interrupts, history,
autocomplete, cursor movement, shell scripting, or command registry.

The panic and exception paths share a small architecture-specific `cpu_halt()`
primitive. `panic()` writes a fatal message and reason through the common
console API, prints `System halted.`, and then remains in a CPU-local `wfe`
loop. It does not use UART or PL011 symbols directly.

The timer path is also separate from the normal echo flow. The common timer API
provides the counter frequency, the current counter value, and monotonic uptime
in milliseconds. Only `arch/aarch64/timer.c` reads the AArch64 timer system
registers; kernel code does not contain `mrs` instructions.

The counter is a continuously increasing hardware tick value. Its frequency is
the number of counter ticks per second. `timer_init()` records the current
counter value as Nimera's boot/reference tick, and `timer_uptime_ms()` converts
the difference from that reference into elapsed milliseconds. `timer_ticks()`
still exposes the raw hardware counter.

This is monotonic uptime, not a clock showing the current time of day: Nimera
does not yet have an RTC, calendar date, wall clock, or timezone handling.
Calling `timer_uptime_ms()` before `timer_init()` triggers `panic()` rather than
returning an uninitialized value.

## Physical memory discovery

QEMU's Device Tree Blob (DTB) is a small structured description of the virtual
machine: it tells software which devices exist and which physical memory
regions are present. With the current `-machine virt` and generic-loader boot
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
- reserved memory is RAM Nimera must not hand to a future allocator;
- usable memory is physical RAM after those reservations are subtracted;
- free memory is not known yet, because no allocator tracks allocations.

Usable memory is represented as multiple ranges when reservations split the
physical range. This is still a description of memory, not page allocation,
heap management, or MMU setup.

The normal terminal starts with:

```text
Nimera booting...
kernel: starting shell
nimera $
```

The built-in commands are `help`, `echo`, `uptime`, `mem`, and `version`.
They are compiled into the kernel; there is no filesystem, current working
directory, userspace, or external program execution.

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

To exercise physical memory discovery, use an isolated test image and choose
QEMU's RAM size:

```sh
make run-memory QEMU_MEMORY=128M
```

The test prints physical, reserved, and usable totals, the kernel and DTB
ranges, and the merged reserved ranges. `build-memory/` is kept separate from
the ordinary build.

To exercise the exception vector with a deliberate undefined instruction, use:

```sh
make run-exception
```

This builds an isolated image in `build-exception/` with
`EXCEPTION_TEST=1`. It prints the verified execution level and minimal
synchronous-exception diagnostics, then halts without returning to the shell.

The ordinary `make run` starts the built-in kernel shell. Its line buffer is a
fixed 128-byte array: printable ASCII is echoed into it, Enter executes the
line, and Backspace removes the previous character. Input beyond the buffer is
ignored safely. Parsing only recognizes the five commands shown above; there
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
│       ├── memory.h
│       ├── panic.h
│       ├── shell.h
│       ├── timer.h
│       ├── types.h
│       └── version.h
├── arch/
│   └── aarch64/
│       ├── boot.S
│       ├── exception.S
│       ├── exception.c
│       ├── halt.S
│       └── timer.c
├── kernel/
│   ├── console.c
│   ├── exception.c
│   ├── format.c
│   ├── main.c
│   ├── memory.c
│   ├── panic.c
│   ├── shell.c
│   └── timer.c
└── platform/
    └── qemu-virt/
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
- `include/nimera/console.h` — the small platform-independent console API.
- `include/nimera/exception.h` — exception initialization and fatal-report API.
- `include/nimera/format.h` — minimal unsigned decimal and hexadecimal output
  helpers used where a number must be displayed.
- `include/nimera/halt.h` — the architecture-neutral CPU halt declaration.
- `include/nimera/shell.h` — the non-returning built-in shell entry point.
- `include/nimera/memory.h` — the common physical memory information API.
- `include/nimera/panic.h` — the non-returning `panic()` API.
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
- `kernel/shell.c` — fixed-buffer command shell using only the common Console
  API; it provides the five built-in commands and basic line editing.
- `kernel/panic.c` — prints the panic report through Console API and halts in
  a simple `wfe` loop.
- `kernel/timer.c` — validates timer frequency and exposes frequency, ticks,
  and monotonic milliseconds without architecture instructions.
- `arch/aarch64/timer.c` — reads `CNTFRQ_EL0` and `CNTPCT_EL0` for the common
  timer layer.
- `platform/qemu-virt/memory.c` — minimal QEMU `virt` FDT parser for the
  physical memory node, DTB reservations, kernel range, and usable gaps; it
  does not implement an allocator.
- `platform/qemu-virt/uart.c` — minimal PL011 MMIO input and output for QEMU
  `virt`.
- `linker.ld` — defines `_start`, the fixed image address, ELF sections,
  `__kernel_start`/`__kernel_end`, and a 16 KiB private stack in `NOLOAD`
  `.bss`.
- `Makefile` — builds the freestanding objects and links them directly with
  LLD; provides `build`, `run`, `run-panic`, `run-timer`, `run-memory`,
  `run-exception`, and `clean`.
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
  `-DNIMERA_MEMORY_TEST=0`, and `-DNIMERA_EXCEPTION_TEST=0` keep the normal
  build path free of test flows; the dedicated Make targets enable their
  respective switch in isolated build directories.
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
