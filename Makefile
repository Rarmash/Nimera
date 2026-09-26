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
EXCEPTION_TEST ?= 0
PMM_TEST ?= 0
HEAP_TEST ?= 0
MMU_TEST ?= 0
MMU_FAULT_TEST ?= 0
PROTECTION_TEST ?= 0
PROTECTION_WRITE_TEST ?= 0
PROTECTION_EXEC_TEST ?= 0
IRQ_TEST ?= 0
UART_IRQ_TEST ?= 0
UART_OVERFLOW_TEST ?= 0
SCHED_TEST ?= 0
QEMU_MEMORY ?= 128M
QEMU_MACHINE ?= virt,gic-version=2

CFLAGS := \
	--target=aarch64-none-elf \
	-std=c11 \
	-O2 \
	-Wall -Wextra -Werror \
	-ffreestanding \
	-fno-builtin \
	-mgeneral-regs-only \
	-fno-stack-protector \
	-fno-pic \
	-fno-pie \
	-fno-asynchronous-unwind-tables \
	-fno-unwind-tables \
	-Iinclude \
	-DNIMERA_PANIC_TEST=$(PANIC_TEST) \
	-DNIMERA_TIMER_TEST=$(TIMER_TEST) \
	-DNIMERA_MEMORY_TEST=$(MEMORY_TEST) \
	-DNIMERA_EXCEPTION_TEST=$(EXCEPTION_TEST) \
	-DNIMERA_PMM_TEST=$(PMM_TEST) \
	-DNIMERA_HEAP_TEST=$(HEAP_TEST) \
	-DNIMERA_MMU_TEST=$(MMU_TEST) \
	-DNIMERA_MMU_FAULT_TEST=$(MMU_FAULT_TEST) \
	-DNIMERA_PROTECTION_TEST=$(PROTECTION_TEST) \
	-DNIMERA_PROTECTION_WRITE_TEST=$(PROTECTION_WRITE_TEST) \
	-DNIMERA_PROTECTION_EXEC_TEST=$(PROTECTION_EXEC_TEST) \
	-DNIMERA_IRQ_TEST=$(IRQ_TEST) \
	-DNIMERA_UART_IRQ_TEST=$(UART_IRQ_TEST) \
	-DNIMERA_UART_OVERFLOW_TEST=$(UART_OVERFLOW_TEST) \
	-DNIMERA_SCHED_TEST=$(SCHED_TEST)

LDFLAGS := \
	-T linker.ld \
	-m aarch64elf \
	-e _start \
	-z max-page-size=0x1000 \
	-Map=$(MAP)

OBJECTS := $(BUILD_DIR)/boot.o $(BUILD_DIR)/exception-vector.o $(BUILD_DIR)/halt.o $(BUILD_DIR)/main.o $(BUILD_DIR)/console.o $(BUILD_DIR)/panic.o $(BUILD_DIR)/exception.o $(BUILD_DIR)/irq.o $(BUILD_DIR)/scheduler.o $(BUILD_DIR)/timer.o $(BUILD_DIR)/arch-timer.o $(BUILD_DIR)/arch-irq.o $(BUILD_DIR)/mmu.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/qemu-memory.o $(BUILD_DIR)/qemu-irq.o $(BUILD_DIR)/gic.o $(BUILD_DIR)/pmm.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/format.o $(BUILD_DIR)/shell.o $(BUILD_DIR)/uart.o $(BUILD_DIR)/arch-exception.o

.PHONY: build run run-panic run-timer run-memory run-exception run-pmm run-heap run-mmu run-mmu-fault run-protection run-protection-write run-protection-exec run-irq run-uart-irq run-uart-overflow run-sched clean

build: $(ELF)

$(ELF): $(OBJECTS) linker.ld | $(BUILD_DIR)/.dir
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD_DIR)/boot.o: arch/aarch64/boot.S | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/exception-vector.o: arch/aarch64/exception.S | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/halt.o: arch/aarch64/halt.S | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: kernel/main.c include/nimera/exception.h include/nimera/format.h include/nimera/heap.h include/nimera/irq.h include/nimera/memory.h include/nimera/mmu.h include/nimera/panic.h include/nimera/pmm.h include/nimera/scheduler.h include/nimera/shell.h include/nimera/timer.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/console.o: kernel/console.c include/nimera/console.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/panic.o: kernel/panic.c include/nimera/console.h include/nimera/panic.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/exception.o: kernel/exception.c include/nimera/console.h include/nimera/exception.h include/nimera/format.h include/nimera/halt.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/irq.o: kernel/irq.c include/nimera/irq.h include/nimera/panic.h include/nimera/scheduler.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/scheduler.o: kernel/scheduler.c include/nimera/console.h include/nimera/format.h include/nimera/irq.h include/nimera/panic.h include/nimera/scheduler.h include/nimera/timer.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/timer.o: kernel/timer.c include/nimera/panic.h include/nimera/timer.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/arch-timer.o: arch/aarch64/timer.c include/nimera/irq.h include/nimera/panic.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/arch-irq.o: arch/aarch64/irq.c include/nimera/irq.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mmu.o: arch/aarch64/mmu.c include/nimera/irq.h include/nimera/memory.h include/nimera/mmu.h include/nimera/panic.h include/nimera/pmm.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/arch-exception.o: arch/aarch64/exception.c arch/aarch64/exception.S include/nimera/exception.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/memory.o: kernel/memory.c include/nimera/console.h include/nimera/format.h include/nimera/memory.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/qemu-memory.o: platform/qemu-virt/memory.c include/nimera/memory.h include/nimera/panic.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/qemu-irq.o: platform/qemu-virt/irq.c include/nimera/irq.h include/nimera/panic.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gic.o: platform/qemu-virt/gic.c include/nimera/irq.h include/nimera/panic.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pmm.o: kernel/pmm.c include/nimera/memory.h include/nimera/panic.h include/nimera/pmm.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/heap.o: kernel/heap.c include/nimera/heap.h include/nimera/panic.h include/nimera/pmm.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/format.o: kernel/format.c include/nimera/console.h include/nimera/format.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shell.o: kernel/shell.c include/nimera/console.h include/nimera/format.h include/nimera/heap.h include/nimera/irq.h include/nimera/memory.h include/nimera/mmu.h include/nimera/pmm.h include/nimera/scheduler.h include/nimera/shell.h include/nimera/timer.h include/nimera/version.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/uart.o: platform/qemu-virt/uart.c include/nimera/irq.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/.dir:
	mkdir -p $@

run:
	$(MAKE) BUILD_DIR=build PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build/baremetal-aarch64.elf,cpu-num=0

run-panic:
	$(MAKE) BUILD_DIR=build-panic PANIC_TEST=1 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-panic/baremetal-aarch64.elf,cpu-num=0

run-timer:
	$(MAKE) BUILD_DIR=build-timer PANIC_TEST=0 TIMER_TEST=1 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-timer/baremetal-aarch64.elf,cpu-num=0

run-memory:
	$(MAKE) BUILD_DIR=build-memory PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=1 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-memory/baremetal-aarch64.elf,cpu-num=0

run-exception:
	$(MAKE) BUILD_DIR=build-exception PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=1 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-exception/baremetal-aarch64.elf,cpu-num=0

run-pmm:
	$(MAKE) BUILD_DIR=build-pmm PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=1 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-pmm/baremetal-aarch64.elf,cpu-num=0

run-mmu:
	$(MAKE) BUILD_DIR=build-mmu PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=1 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-mmu/baremetal-aarch64.elf,cpu-num=0

run-mmu-fault:
	$(MAKE) BUILD_DIR=build-mmu-fault PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=1 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-mmu-fault/baremetal-aarch64.elf,cpu-num=0

run-heap:
	$(MAKE) BUILD_DIR=build-heap PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=1 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-heap/baremetal-aarch64.elf,cpu-num=0

run-protection:
	$(MAKE) BUILD_DIR=build-protection PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=1 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-protection/baremetal-aarch64.elf,cpu-num=0

run-protection-write:
	$(MAKE) BUILD_DIR=build-protection-write PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=1 PROTECTION_EXEC_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-protection-write/baremetal-aarch64.elf,cpu-num=0

run-protection-exec:
	$(MAKE) BUILD_DIR=build-protection-exec PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=1 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-protection-exec/baremetal-aarch64.elf,cpu-num=0

run-irq:
	$(MAKE) BUILD_DIR=build-irq PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=1 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-irq/baremetal-aarch64.elf,cpu-num=0

run-uart-irq:
	$(MAKE) BUILD_DIR=build-uart-irq PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=0 UART_IRQ_TEST=1 UART_OVERFLOW_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-uart-irq/baremetal-aarch64.elf,cpu-num=0

run-uart-overflow:
	$(MAKE) BUILD_DIR=build-uart-overflow PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=0 UART_IRQ_TEST=0 UART_OVERFLOW_TEST=1 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-uart-overflow/baremetal-aarch64.elf,cpu-num=0

run-sched:
	rm -rf build-sched
	$(MAKE) BUILD_DIR=build-sched PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=0 UART_IRQ_TEST=0 UART_OVERFLOW_TEST=0 SCHED_TEST=1 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-sched/baremetal-aarch64.elf,cpu-num=0

clean:
	rm -rf build build-panic build-timer build-memory build-exception build-pmm build-heap build-mmu build-mmu-fault build-protection build-protection-write build-protection-exec build-irq build-uart-irq build-uart-overflow build-sched
