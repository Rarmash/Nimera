# Minimal bare-metal AArch64 kernel

This is the first milestone of a tiny freestanding AArch64 kernel for QEMU's
`virt` machine on Apple Silicon macOS. It builds an ELF, loads that ELF with
QEMU's generic loader, creates a private stack, calls `kernel_main()`, prints
`Hello from kernel` through the `virt` machine's PL011 UART, and then remains in
a safe `wfe` idle loop.

There is deliberately no libc, compiler runtime, allocator, interrupt setup,
scheduler, filesystem, userspace, or other future subsystem.

## Prerequisites

Install the native Homebrew packages:

```sh
brew install llvm lld qemu
```

The Makefile defaults to Homebrew LLVM at `/opt/homebrew/opt/llvm` and
Homebrew LLD at `/opt/homebrew/opt/lld`, which are the usual Apple Silicon
prefixes. Homebrew distributes `ld.lld` separately from the `llvm` formula.
The build therefore uses Homebrew `clang` and Homebrew `ld.lld`, not the Apple
system compiler or linker. Override `LLVM_PREFIX`, `LLD_PREFIX`, or `QEMU` if
your installation is elsewhere.

## Build and run

```sh
make build
make run
make clean
```

`make run` starts QEMU without a graphical device, connects the emulated
serial port to the terminal, and loads `build/baremetal-aarch64.elf` with:

```text
-device loader,file=build/baremetal-aarch64.elf,cpu-num=0
```

The loader reads the ELF program headers, places its loadable segments in
guest RAM, and starts CPU 0 at the ELF entry point. The linker places the image
at `0x40100000`, just after QEMU's default DTB area at the RAM base
(`0x40000000`). Stop QEMU with `Ctrl-A`, then `X`.

Expected output:

```text
Hello from kernel
```

## Source layout

- `arch/aarch64/boot.S` is the only assembly. `_start` computes `__stack_top`, moves it
  into `sp`, calls `kernel_main`, and uses `wfe` in a fallback loop if C ever
  returns.
- `kernel/main.c` is the kernel entry point and owns the safe idle loop.
- `platform/qemu-virt/uart.c` writes bytes to PL011's data register at
  `0x09000000`.
- `linker.ld` defines the ELF entry, the `virt` load address, normal code/data
  sections, and a 16 KiB private stack in `NOLOAD` `.bss`.
- `Makefile` builds only two object files and links them directly with LLD.

## Compiler and linker flags

The flags are intentionally explicit because this is not a hosted program:

- `--target=aarch64-none-elf` selects 64-bit ARM ELF code with no assumed OS.
- `-std=c11` selects the small C language subset used here.
- `-O2` enables normal optimization while keeping the output small.
- `-Wall -Wextra -Werror` turns common mistakes into build failures.
- `-ffreestanding` tells Clang that the standard library and `main` are not
  available and that this is a freestanding environment.
- `-fno-builtin` prevents implicit assumptions about libc functions.
- `-fno-stack-protector` removes compiler-inserted canary calls to a runtime.
- `-fno-pic -fno-pie` avoids position-independent code and dynamic linking; the
  linker script supplies the fixed guest address.
- `-fno-asynchronous-unwind-tables -fno-unwind-tables` avoids metadata that
  would require runtime unwind support.
- `-T linker.ld` supplies the complete memory layout and entry point.
- `-m aarch64elf` selects LLD's AArch64 ELF emulation.
- `-e _start` makes the assembly entry explicit.
- `-z max-page-size=0x1000` keeps load segment alignment small and predictable
  for this tiny image.
- `-Map=...` writes a useful link map for inspecting symbols and layout.

The link is performed by `ld.lld` directly, so no startup objects, libc,
libgcc/compiler-rt, dynamic linker, or other third-party runtime is pulled in.
