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
- writes `Hello from kernel` through QEMU `virt`'s PL011 UART; and
- enables polling UART input and echoes each received character;
- provides a minimal kernel panic path that reports a reason and halts safely.

There is currently no libc, allocator, interrupt subsystem, scheduler,
filesystem, userspace, or other larger OS subsystem.

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
3. It calls `kernel_main()` in `kernel/main.c`.
4. `kernel_main()` uses `console_write()` and `console_getc()` from the common
   console layer.
5. The current console implementation delegates to the QEMU `virt` PL011
   driver, which writes output to the data register at `0x09000000` and polls
   input status.
6. `kernel_main()` prints the echo-mode message, then waits for input by
   polling the PL011 receive FIFO state.
7. Each received character is sent back through the same UART. Enter is
   normalized to `\r\n` for a clean terminal line.

The input path is intentionally polling-based. It has no interrupts, ring
buffer, line editor, shell, or command handling.

The panic path is separate from the normal echo flow. `panic()` writes a fatal
message and reason through the common console API, prints `System halted.`, and
then remains in a CPU-local `wfe` loop. It does not use UART or PL011 symbols
directly.

The terminal output is:

```text
Hello from kernel
Echo mode enabled. Type characters:
```

After the second line, characters typed into the terminal are echoed one at a
time. Enter is emitted as `\r\n`.

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
│       └── panic.h
├── arch/
│   └── aarch64/
│       └── boot.S
├── kernel/
│   ├── console.c
│   ├── main.c
│   └── panic.c
└── platform/
    └── qemu-virt/
        └── uart.c
```

- `arch/aarch64/boot.S` — the only assembly file; installs the initial stack,
  calls C, and provides the fallback loop if C returns.
- `include/nimera/console.h` — the small platform-independent console API.
- `include/nimera/panic.h` — the non-returning `panic()` API.
- `kernel/console.c` — delegates the common console API to the current UART
  implementation.
- `kernel/main.c` — defines `kernel_main()` and the minimal polling echo loop;
  it does not call UART functions directly.
- `kernel/panic.c` — prints the panic report through Console API and halts in
  a simple `wfe` loop.
- `platform/qemu-virt/uart.c` — minimal PL011 MMIO input and output for QEMU
  `virt`.
- `linker.ld` — defines `_start`, the fixed image address, ELF sections, and a
  16 KiB private stack in `NOLOAD` `.bss`.
- `Makefile` — builds five object files and links them directly with LLD;
  provides `build`, `run`, `run-panic`, and `clean`.
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
- `-fno-stack-protector` avoids compiler-generated calls to stack-canary
  runtime support.
- `-fno-pic -fno-pie` avoids position-independent code and dynamic linking; the
  linker script supplies the fixed guest address.
- `-fno-asynchronous-unwind-tables -fno-unwind-tables` avoids unwind metadata
  that would require additional runtime support.
- `-Iinclude` makes the project's freestanding headers available without
  depending on host or libc headers.
- `-DNIMERA_PANIC_TEST=0` keeps the normal build path free of the panic test;
  `make run-panic` changes it to `1` for the separate runtime check.
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

The near-term roadmap is deliberately short:

1. Timer support
2. Memory discovery and reporting
3. Minimal interactive shell
