PROJECT := baremetal-aarch64
BUILD_DIR := build
ELF := $(BUILD_DIR)/$(PROJECT).elf
MAP := $(BUILD_DIR)/$(PROJECT).map

# Homebrew's native Apple Silicon prefix. Override these variables if needed.
LLVM_PREFIX ?= /opt/homebrew/opt/llvm
LLD_PREFIX ?= /opt/homebrew/opt/lld
CC := $(LLVM_PREFIX)/bin/clang
LD := $(LLD_PREFIX)/bin/ld.lld
QEMU ?= qemu-system-aarch64
PANIC_TEST ?= 0
TIMER_TEST ?= 0
MEMORY_TEST ?= 0
QEMU_MEMORY ?= 128M

CFLAGS := \
	--target=aarch64-none-elf \
	-std=c11 \
	-O2 \
	-Wall -Wextra -Werror \
	-ffreestanding \
	-fno-builtin \
	-fno-stack-protector \
	-fno-pic \
	-fno-pie \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables \
	-Iinclude \
	-DNIMERA_PANIC_TEST=$(PANIC_TEST) \
	-DNIMERA_TIMER_TEST=$(TIMER_TEST) \
	-DNIMERA_MEMORY_TEST=$(MEMORY_TEST)

LDFLAGS := \
	-T linker.ld \
	-m aarch64elf \
	-e _start \
	-z max-page-size=0x1000 \
	-Map=$(MAP)

OBJECTS := $(BUILD_DIR)/boot.o $(BUILD_DIR)/main.o $(BUILD_DIR)/console.o $(BUILD_DIR)/panic.o $(BUILD_DIR)/timer.o $(BUILD_DIR)/arch-timer.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/qemu-memory.o $(BUILD_DIR)/uart.o

.PHONY: build run run-panic run-timer run-memory clean

build: $(ELF)

$(ELF): $(OBJECTS) linker.ld | $(BUILD_DIR)/.dir
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD_DIR)/boot.o: arch/aarch64/boot.S | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: kernel/main.c | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/console.o: kernel/console.c include/nimera/console.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/panic.o: kernel/panic.c include/nimera/console.h include/nimera/panic.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/timer.o: kernel/timer.c include/nimera/panic.h include/nimera/timer.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/arch-timer.o: arch/aarch64/timer.c include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/memory.o: kernel/memory.c include/nimera/memory.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/qemu-memory.o: platform/qemu-virt/memory.c include/nimera/memory.h include/nimera/panic.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/uart.o: platform/qemu-virt/uart.c | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/.dir:
	mkdir -p $@

run:
	$(MAKE) BUILD_DIR=build PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 build
	$(QEMU) \
		-machine virt \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build/baremetal-aarch64.elf,cpu-num=0

run-panic:
	$(MAKE) BUILD_DIR=build-panic PANIC_TEST=1 TIMER_TEST=0 MEMORY_TEST=0 build
	$(QEMU) \
		-machine virt \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-panic/baremetal-aarch64.elf,cpu-num=0

run-timer:
	$(MAKE) BUILD_DIR=build-timer PANIC_TEST=0 TIMER_TEST=1 MEMORY_TEST=0 build
	$(QEMU) \
		-machine virt \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-timer/baremetal-aarch64.elf,cpu-num=0

run-memory:
	$(MAKE) BUILD_DIR=build-memory PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=1 build
	$(QEMU) \
		-machine virt \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-memory/baremetal-aarch64.elf,cpu-num=0

clean:
	rm -rf build build-panic build-timer build-memory
