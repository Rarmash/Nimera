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
QEMU_IMG ?= qemu-img
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
BLOCKING_TEST ?= 0
VFS_TEST ?= 0
VFS_WRITE_TEST ?= 0
TERMINAL_TEST ?= 0
TERMINAL_SIZE_TEST ?= 0
TERMINAL_SIZE_NO_RESPONSE ?= 0
EDITOR_TEST ?= 0
USER_EDITOR_TEST ?= 0
USER_EDITOR_SELF_TEST ?= 0
ELF_INSTALL_TEST ?= 0
ELF_RUN_TEST ?= 0
BLOCK_TEST ?= 0
NIMFS_BOOT ?= 0
NIMFS_FORMAT_TEST ?= 0
NIMFS_MULTI_FORMAT_TEST ?= 0
NIMFS_MOUNT_TEST ?= 0
NIMFS_VOLUME_TEST ?= 0
USER_TEST ?= 0
USER_PROTECTION_TEST ?= 0
USER_FILES_TEST ?= 0
USER_UTILS_TEST ?= 0
PROCESS_TEST ?= 0
PIPE_TEST ?= 0
COMMAND_TEST ?= 0
REDIRECTION_TEST ?= 0
JOBS_TEST ?= 0
GRAPHICS_TEST ?= 0
FRAMEBUFFER_TERMINAL ?= 0
INPUT_TEST ?= 0
UTF8_TEST ?= 0
COMPOSITOR_TEST ?= 0
WINDOW_TEST ?= 0
RENDER_BATCHING ?= 1
RENDER_BATCHING_TEST ?= 0
TERMINAL_APP_TEST ?= 0
TERMINAL_FAULT_TEST ?= 0
TERMINAL_CHECK_TEST ?= 0
NIMFS_STORAGE_IMAGE ?= build-storage/nimfs.img
NIMFS_ROOT_IMAGE ?= build-storage/nimfs-root.img
NIMFS_DATA_IMAGE ?= build-storage/nimfs-data.img
NIMFS_DATA2_IMAGE ?= build-storage/nimfs-data2.img
NIMFS_ELF_IMAGE ?= build-storage/nimfs-elf.img
STORAGE_DIR ?= build-storage
STORAGE_IMAGE ?= $(STORAGE_DIR)/nimera-test.img
STORAGE_SIZE ?= 64M
QEMU_MEMORY ?= 128M
QEMU_MACHINE ?= virt,gic-version=2
USER_APP_BUILD_DIR ?= build-user-app
USER_APP_ELF := $(USER_APP_BUILD_DIR)/hello.elf
USER_CAT_ELF := $(USER_APP_BUILD_DIR)/cat.elf
USER_FILETEST_ELF := $(USER_APP_BUILD_DIR)/filetest.elf
USER_KEYTEST_ELF := $(USER_APP_BUILD_DIR)/keytest.elf
USER_FAULTTEST_ELF := $(USER_APP_BUILD_DIR)/faulttest.elf
USER_TERMCHECK_ELF := $(USER_APP_BUILD_DIR)/termcheck.elf
USER_EDIT_ELF := $(USER_APP_BUILD_DIR)/edit.elf
USER_LS_ELF := $(USER_APP_BUILD_DIR)/ls.elf
USER_MKDIR_ELF := $(USER_APP_BUILD_DIR)/mkdir.elf
USER_TOUCH_ELF := $(USER_APP_BUILD_DIR)/touch.elf
USER_RM_ELF := $(USER_APP_BUILD_DIR)/rm.elf
USER_RMDIR_ELF := $(USER_APP_BUILD_DIR)/rmdir.elf
USER_MV_ELF := $(USER_APP_BUILD_DIR)/mv.elf
USER_PWD_ELF := $(USER_APP_BUILD_DIR)/pwd.elf
USER_WRITE_ELF := $(USER_APP_BUILD_DIR)/write.elf
USER_APPEND_ELF := $(USER_APP_BUILD_DIR)/append.elf
USER_PIDTEST_ELF := $(USER_APP_BUILD_DIR)/pidtest.elf
USER_PROCTEST_ELF := $(USER_APP_BUILD_DIR)/proctest.elf
USER_PROCFAULT_ELF := $(USER_APP_BUILD_DIR)/procfault.elf
USER_UPPER_ELF := $(USER_APP_BUILD_DIR)/upper.elf
USER_PIPETEST_ELF := $(USER_APP_BUILD_DIR)/pipetest.elf
USER_JOBTEST_ELF := $(USER_APP_BUILD_DIR)/jobtest.elf
USER_CC := $(LLVM_PREFIX)/bin/clang
USER_LD := $(LLD_PREFIX)/bin/ld.lld
USER_OBJCOPY := $(LLVM_PREFIX)/bin/llvm-objcopy

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
	-DNIMERA_SCHED_TEST=$(SCHED_TEST) \
	-DNIMERA_BLOCKING_TEST=$(BLOCKING_TEST) \
	-DNIMERA_VFS_TEST=$(VFS_TEST) \
	-DNIMERA_VFS_WRITE_TEST=$(VFS_WRITE_TEST) \
	-DNIMERA_TERMINAL_TEST=$(TERMINAL_TEST) \
	-DNIMERA_TERMINAL_SIZE_TEST=$(TERMINAL_SIZE_TEST) \
	-DNIMERA_TERMINAL_SIZE_NO_RESPONSE=$(TERMINAL_SIZE_NO_RESPONSE) \
	-DNIMERA_EDITOR_TEST=$(EDITOR_TEST) \
	-DNIMERA_USER_EDITOR_TEST=$(USER_EDITOR_TEST) \
	-DNIMERA_BLOCK_TEST=$(BLOCK_TEST) \
	-DNIMERA_NIMFS_BOOT=$(NIMFS_BOOT) \
	-DNIMERA_NIMFS_FORMAT_TEST=$(NIMFS_FORMAT_TEST) \
	-DNIMERA_NIMFS_MULTI_FORMAT_TEST=$(NIMFS_MULTI_FORMAT_TEST) \
	-DNIMERA_NIMFS_MOUNT_TEST=$(NIMFS_MOUNT_TEST) \
	-DNIMERA_NIMFS_VOLUME_TEST=$(NIMFS_VOLUME_TEST) \
	-DNIMERA_USER_TEST=$(USER_TEST) \
	-DNIMERA_USER_PROTECTION_TEST=$(USER_PROTECTION_TEST) \
	-DNIMERA_USER_FILES_TEST=$(USER_FILES_TEST) \
	-DNIMERA_USER_UTILS_TEST=$(USER_UTILS_TEST) \
	-DNIMERA_PROCESS_TEST=$(PROCESS_TEST) \
	-DNIMERA_PIPE_TEST=$(PIPE_TEST) \
	-DNIMERA_COMMAND_TEST=$(COMMAND_TEST) \
	-DNIMERA_REDIRECTION_TEST=$(REDIRECTION_TEST) \
	-DNIMERA_JOBS_TEST=$(JOBS_TEST) \
	-DNIMERA_GRAPHICS_TEST=$(GRAPHICS_TEST) \
	-DNIMERA_FRAMEBUFFER_TERMINAL=$(FRAMEBUFFER_TERMINAL) \
	-DNIMERA_INPUT_TEST=$(INPUT_TEST) \
	-DNIMERA_UTF8_TEST=$(UTF8_TEST) \
	-DNIMERA_COMPOSITOR_TEST=$(COMPOSITOR_TEST) \
	-DNIMERA_WINDOW_TEST=$(WINDOW_TEST) \
	-DNIMERA_RENDER_BATCHING=$(RENDER_BATCHING) \
	-DNIMERA_RENDER_BATCHING_TEST=$(RENDER_BATCHING_TEST) \
	-DNIMERA_TERMINAL_APP_TEST=$(TERMINAL_APP_TEST) \
	-DNIMERA_TERMINAL_FAULT_TEST=$(TERMINAL_FAULT_TEST) \
	-DNIMERA_TERMINAL_CHECK_TEST=$(TERMINAL_CHECK_TEST) \
	-DNIMERA_ELF_INSTALL_TEST=$(ELF_INSTALL_TEST) \
	-DNIMERA_ELF_RUN_TEST=$(ELF_RUN_TEST)

LDFLAGS := \
	-T linker.ld \
	-m aarch64elf \
	-e _start \
	-z max-page-size=0x1000 \
	-Map=$(MAP)

OBJECTS := $(BUILD_DIR)/boot.o $(BUILD_DIR)/exception-vector.o $(BUILD_DIR)/halt.o $(BUILD_DIR)/main.o $(BUILD_DIR)/console.o $(BUILD_DIR)/display.o $(BUILD_DIR)/graphics.o $(BUILD_DIR)/surface.o $(BUILD_DIR)/compositor.o $(BUILD_DIR)/window.o $(BUILD_DIR)/render-stats.o $(BUILD_DIR)/render-test.o $(BUILD_DIR)/utf8.o $(BUILD_DIR)/terminal.o $(BUILD_DIR)/terminal-fb.o $(BUILD_DIR)/input.o $(BUILD_DIR)/editor.o $(BUILD_DIR)/elf.o $(BUILD_DIR)/panic.o $(BUILD_DIR)/exception.o $(BUILD_DIR)/syscall.o $(BUILD_DIR)/irq.o $(BUILD_DIR)/scheduler.o $(BUILD_DIR)/pipe.o $(BUILD_DIR)/vfs.o $(BUILD_DIR)/ramfs.o $(BUILD_DIR)/nimfs.o $(BUILD_DIR)/timer.o $(BUILD_DIR)/arch-timer.o $(BUILD_DIR)/arch-irq.o $(BUILD_DIR)/mmu.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/qemu-memory.o $(BUILD_DIR)/qemu-irq.o $(BUILD_DIR)/qemu-virtio.o $(BUILD_DIR)/gic.o $(BUILD_DIR)/pmm.o $(BUILD_DIR)/heap.o $(BUILD_DIR)/format.o $(BUILD_DIR)/shell.o $(BUILD_DIR)/block.o $(BUILD_DIR)/uart.o $(BUILD_DIR)/arch-exception.o $(BUILD_DIR)/user-test.o $(BUILD_DIR)/user-syscall.o

ifneq ($(ELF_INSTALL_TEST),0)
OBJECTS += $(BUILD_DIR)/hello-payload.o
OBJECTS += $(BUILD_DIR)/cat-payload.o $(BUILD_DIR)/filetest-payload.o
OBJECTS += $(BUILD_DIR)/edit-payload.o
OBJECTS += $(BUILD_DIR)/ls-payload.o $(BUILD_DIR)/mkdir-payload.o
OBJECTS += $(BUILD_DIR)/touch-payload.o $(BUILD_DIR)/rm-payload.o
OBJECTS += $(BUILD_DIR)/rmdir-payload.o $(BUILD_DIR)/mv-payload.o
OBJECTS += $(BUILD_DIR)/pwd-payload.o $(BUILD_DIR)/write-payload.o $(BUILD_DIR)/append-payload.o
OBJECTS += $(BUILD_DIR)/pidtest-payload.o $(BUILD_DIR)/proctest-payload.o $(BUILD_DIR)/procfault-payload.o
OBJECTS += $(BUILD_DIR)/upper-payload.o
OBJECTS += $(BUILD_DIR)/pipetest-payload.o
OBJECTS += $(BUILD_DIR)/jobtest-payload.o
ifneq ($(TERMINAL_APP_TEST)$(TERMINAL_FAULT_TEST)$(TERMINAL_CHECK_TEST),000)
OBJECTS += $(BUILD_DIR)/keytest-payload.o $(BUILD_DIR)/faulttest-payload.o
OBJECTS += $(BUILD_DIR)/termcheck-payload.o
endif
endif

.PHONY: build user-app run run-panic run-timer run-memory run-exception run-pmm run-heap run-mmu run-mmu-fault run-protection run-protection-write run-protection-exec run-irq run-uart-irq run-uart-overflow run-sched run-blocking run-vfs run-vfs-write run-terminal run-terminal-size run-terminal-size-fallback run-editor run-block run-user run-user-protection run-user-terminal run-user-editor-format run-user-editor run-user-editor-test run-user-utils run-elf-format run-elf run-elf-test run-user-files run-commands run-terminal-app run-terminal-app-format run-terminal-fault run-processes run-pipes run-redirection run-jobs run-fb-terminal run-fb-terminal-test run-native-input-test run-utf8-test run-compositor run-windows run-render-batching nimfs-elf-disk-create nimfs-elf-disk-reset disk-create disk-reset nimfs-disk-create nimfs-disk-reset nimfs-root-create nimfs-data-create nimfs-data-reset nimfs-data2-create nimfs-data2-reset run-nimfs-format run-nimfs run-nimfs-data-format run-nimfs-multi-format run-nimfs-multi run-nimfs-volume-format run-mounts run-volume clean

build: $(ELF)

.PHONY: run-graphics

$(ELF): $(OBJECTS) linker.ld | $(BUILD_DIR)/.dir
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD_DIR)/boot.o: arch/aarch64/boot.S | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/exception-vector.o: arch/aarch64/exception.S | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/halt.o: arch/aarch64/halt.S | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: kernel/main.c include/nimera/block.h include/nimera/compositor.h include/nimera/editor.h include/nimera/elf.h include/nimera/exception.h include/nimera/format.h include/nimera/graphics.h include/nimera/heap.h include/nimera/irq.h include/nimera/memory.h include/nimera/mmu.h include/nimera/nimfs.h include/nimera/panic.h include/nimera/pipe.h include/nimera/pmm.h include/nimera/process.h include/nimera/scheduler.h include/nimera/shell.h include/nimera/terminal.h include/nimera/timer.h include/nimera/user.h include/nimera/utf8.h include/nimera/vfs.h include/nimera/virtio.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/console.o: kernel/console.c include/nimera/console.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/display.o: kernel/display.c include/nimera/display.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/graphics.o: kernel/graphics.c include/nimera/console.h include/nimera/display.h include/nimera/graphics.h include/nimera/timer.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/surface.o: kernel/surface.c include/nimera/pmm.h include/nimera/surface.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/compositor.o: kernel/compositor.c include/nimera/compositor.h include/nimera/console.h include/nimera/display.h include/nimera/graphics.h include/nimera/timer.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/window.o: kernel/window.c include/nimera/compositor.h include/nimera/console.h include/nimera/display.h include/nimera/graphics.h include/nimera/window.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/utf8.o: kernel/utf8.c include/nimera/types.h include/nimera/utf8.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/terminal.o: kernel/terminal.c include/nimera/compositor.h include/nimera/console.h include/nimera/display.h include/nimera/format.h include/nimera/irq.h include/nimera/input.h include/nimera/terminal.h include/nimera/terminal_fb.h include/nimera/timer.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/terminal-fb.o: kernel/terminal_fb.c include/nimera/compositor.h include/nimera/graphics.h include/nimera/heap.h include/nimera/terminal_fb.h include/nimera/utf8.h include/nimera/window.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/render-stats.o: kernel/render_stats.c include/nimera/render_stats.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/render-test.o: kernel/render_test.c include/nimera/compositor.h include/nimera/console.h include/nimera/format.h include/nimera/render_stats.h include/nimera/render_test.h include/nimera/terminal.h include/nimera/timer.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/input.o: kernel/input.c include/nimera/input.h include/nimera/irq.h include/nimera/scheduler.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/editor.o: kernel/editor.c include/nimera/console.h include/nimera/editor.h include/nimera/format.h include/nimera/heap.h include/nimera/panic.h include/nimera/terminal.h include/nimera/vfs.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/elf.o: kernel/elf.c include/nimera/abi/syscall.h include/nimera/abi/terminal.h include/nimera/elf.h include/nimera/heap.h include/nimera/mmu.h include/nimera/pipe.h include/nimera/pmm.h include/nimera/process.h include/nimera/scheduler.h include/nimera/terminal.h include/nimera/user.h include/nimera/vfs.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@


$(BUILD_DIR)/hello-payload.o: $(USER_APP_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@

$(BUILD_DIR)/cat-payload.o: $(USER_CAT_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@

$(BUILD_DIR)/filetest-payload.o: $(USER_FILETEST_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@

$(BUILD_DIR)/keytest-payload.o: $(USER_KEYTEST_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@

$(BUILD_DIR)/faulttest-payload.o: $(USER_FAULTTEST_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@

$(BUILD_DIR)/edit-payload.o: $(USER_EDIT_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@

$(BUILD_DIR)/ls-payload.o: $(USER_LS_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/mkdir-payload.o: $(USER_MKDIR_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/touch-payload.o: $(USER_TOUCH_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/rm-payload.o: $(USER_RM_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/rmdir-payload.o: $(USER_RMDIR_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/mv-payload.o: $(USER_MV_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/pwd-payload.o: $(USER_PWD_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/write-payload.o: $(USER_WRITE_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/append-payload.o: $(USER_APPEND_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/pidtest-payload.o: $(USER_PIDTEST_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/proctest-payload.o: $(USER_PROCTEST_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/procfault-payload.o: $(USER_PROCFAULT_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/upper-payload.o: $(USER_UPPER_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
$(BUILD_DIR)/pipetest-payload.o: $(USER_PIPETEST_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@

$(BUILD_DIR)/jobtest-payload.o: $(USER_JOBTEST_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@

$(BUILD_DIR)/termcheck-payload.o: $(USER_TERMCHECK_ELF) | $(BUILD_DIR)/.dir
	$(USER_OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@

user-app: $(USER_APP_ELF) $(USER_CAT_ELF) $(USER_FILETEST_ELF) $(USER_KEYTEST_ELF) $(USER_FAULTTEST_ELF) $(USER_TERMCHECK_ELF) $(USER_EDIT_ELF) $(USER_LS_ELF) $(USER_MKDIR_ELF) $(USER_TOUCH_ELF) $(USER_RM_ELF) $(USER_RMDIR_ELF) $(USER_MV_ELF) $(USER_PWD_ELF) $(USER_WRITE_ELF) $(USER_APPEND_ELF) $(USER_PIDTEST_ELF) $(USER_PROCTEST_ELF) $(USER_PROCFAULT_ELF) $(USER_UPPER_ELF) $(USER_PIPETEST_ELF) $(USER_JOBTEST_ELF)

$(USER_APP_ELF): $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/hello.o $(USER_APP_BUILD_DIR)/syscall.o user/apps/hello/linker.ld | $(USER_APP_BUILD_DIR)/.dir
	$(USER_LD) -T user/apps/hello/linker.ld -m aarch64elf -e _start -z max-page-size=0x1000 -Map=$(USER_APP_BUILD_DIR)/hello.map -o $@ $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/hello.o $(USER_APP_BUILD_DIR)/syscall.o

$(USER_CAT_ELF): $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/cat.o $(USER_APP_BUILD_DIR)/fsutil.o $(USER_APP_BUILD_DIR)/syscall.o user/apps/cat/linker.ld | $(USER_APP_BUILD_DIR)/.dir
	$(USER_LD) -T user/apps/cat/linker.ld -m aarch64elf -e _start -z max-page-size=0x1000 -Map=$(USER_APP_BUILD_DIR)/cat.map -o $@ $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/cat.o $(USER_APP_BUILD_DIR)/fsutil.o $(USER_APP_BUILD_DIR)/syscall.o

$(USER_FILETEST_ELF): $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/filetest.o $(USER_APP_BUILD_DIR)/syscall.o user/apps/filetest/linker.ld | $(USER_APP_BUILD_DIR)/.dir
	$(USER_LD) -T user/apps/filetest/linker.ld -m aarch64elf -e _start -z max-page-size=0x1000 -Map=$(USER_APP_BUILD_DIR)/filetest.map -o $@ $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/filetest.o $(USER_APP_BUILD_DIR)/syscall.o

$(USER_KEYTEST_ELF): $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/keytest.o $(USER_APP_BUILD_DIR)/syscall.o user/apps/keytest/linker.ld | $(USER_APP_BUILD_DIR)/.dir
	$(USER_LD) -T user/apps/keytest/linker.ld -m aarch64elf -e _start -z max-page-size=0x1000 -Map=$(USER_APP_BUILD_DIR)/keytest.map -o $@ $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/keytest.o $(USER_APP_BUILD_DIR)/syscall.o

$(USER_FAULTTEST_ELF): $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/faulttest.o $(USER_APP_BUILD_DIR)/syscall.o user/apps/faulttest/linker.ld | $(USER_APP_BUILD_DIR)/.dir
	$(USER_LD) -T user/apps/faulttest/linker.ld -m aarch64elf -e _start -z max-page-size=0x1000 -Map=$(USER_APP_BUILD_DIR)/faulttest.map -o $@ $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/faulttest.o $(USER_APP_BUILD_DIR)/syscall.o

$(USER_TERMCHECK_ELF): $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/termcheck.o $(USER_APP_BUILD_DIR)/syscall.o user/apps/termcheck/linker.ld | $(USER_APP_BUILD_DIR)/.dir
	$(USER_LD) -T user/apps/termcheck/linker.ld -m aarch64elf -e _start -z max-page-size=0x1000 -Map=$(USER_APP_BUILD_DIR)/termcheck.map -o $@ $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/termcheck.o $(USER_APP_BUILD_DIR)/syscall.o

$(USER_EDIT_ELF): $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/edit.o $(USER_APP_BUILD_DIR)/edit-model.o $(USER_APP_BUILD_DIR)/syscall.o user/apps/edit/linker.ld | $(USER_APP_BUILD_DIR)/.dir
	$(USER_LD) -T user/apps/edit/linker.ld -m aarch64elf -e _start -z max-page-size=0x1000 -Map=$(USER_APP_BUILD_DIR)/edit.map -o $@ $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/edit.o $(USER_APP_BUILD_DIR)/edit-model.o $(USER_APP_BUILD_DIR)/syscall.o

$(USER_APP_BUILD_DIR)/syscall.o: user/runtime/syscall.S include/nimera/abi/syscall.h include/nimera/abi/terminal.h | $(USER_APP_BUILD_DIR)/.dir
	$(USER_CC) --target=aarch64-none-elf -Iuser/include -Iinclude -c user/runtime/syscall.S -o $@

$(USER_APP_BUILD_DIR)/start.o: user/runtime/start.S | $(USER_APP_BUILD_DIR)/.dir
	$(USER_CC) --target=aarch64-none-elf -c $< -o $@

$(USER_APP_BUILD_DIR)/hello.o: user/apps/hello/main.c | $(USER_APP_BUILD_DIR)/.dir
	$(USER_CC) --target=aarch64-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -mgeneral-regs-only -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -Iuser/include -Iinclude -c $< -o $@

$(USER_APP_BUILD_DIR)/cat.o: user/apps/cat/main.c user/include/nimera/user.h | $(USER_APP_BUILD_DIR)/.dir
	$(USER_CC) --target=aarch64-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -mgeneral-regs-only -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -Iuser/include -Iinclude -c $< -o $@

$(USER_APP_BUILD_DIR)/filetest.o: user/apps/filetest/main.c user/include/nimera/user.h | $(USER_APP_BUILD_DIR)/.dir
	$(USER_CC) --target=aarch64-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -mgeneral-regs-only -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -Iuser/include -Iinclude -c $< -o $@

$(USER_APP_BUILD_DIR)/keytest.o: user/apps/keytest/main.c user/include/nimera/user.h | $(USER_APP_BUILD_DIR)/.dir
	$(USER_CC) --target=aarch64-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -mgeneral-regs-only -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -Iuser/include -Iinclude -c $< -o $@

$(USER_APP_BUILD_DIR)/faulttest.o: user/apps/faulttest/main.c user/include/nimera/user.h | $(USER_APP_BUILD_DIR)/.dir
	$(USER_CC) --target=aarch64-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -mgeneral-regs-only -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -Iuser/include -Iinclude -c $< -o $@

$(USER_APP_BUILD_DIR)/termcheck.o: user/apps/termcheck/main.c user/include/nimera/user.h include/nimera/abi/terminal.h | $(USER_APP_BUILD_DIR)/.dir
	$(USER_CC) --target=aarch64-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -mgeneral-regs-only -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -Iuser/include -Iinclude -c $< -o $@

$(USER_APP_BUILD_DIR)/edit.o: user/apps/edit/main.c user/apps/edit/editor.h user/include/nimera/user.h include/nimera/abi/terminal.h | $(USER_APP_BUILD_DIR)/.dir
	$(USER_CC) --target=aarch64-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -mgeneral-regs-only -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -Iuser/include -Iinclude -Iuser/apps/edit -DNIMERA_USER_EDITOR_SELF_TEST=$(USER_EDITOR_SELF_TEST) -c $< -o $@

$(USER_APP_BUILD_DIR)/edit-model.o: user/apps/edit/editor.c user/apps/edit/editor.h user/include/nimera/user.h | $(USER_APP_BUILD_DIR)/.dir
	$(USER_CC) --target=aarch64-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -mgeneral-regs-only -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -Iuser/include -Iinclude -Iuser/apps/edit -c $< -o $@

$(USER_APP_BUILD_DIR)/fsutil.o: user/runtime/fsutil.c user/runtime/fsutil.h user/include/nimera/user.h | $(USER_APP_BUILD_DIR)/.dir
	$(USER_CC) --target=aarch64-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -mgeneral-regs-only -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -Iuser/include -Iinclude -Iuser/runtime -c $< -o $@

define USER_FS_APP_RULES
$(USER_APP_BUILD_DIR)/$(1).o: user/apps/$(1)/main.c user/runtime/fsutil.h user/include/nimera/user.h | $(USER_APP_BUILD_DIR)/.dir
	$$(USER_CC) --target=aarch64-none-elf -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -mgeneral-regs-only -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -Iuser/include -Iinclude -Iuser/runtime -c $$< -o $$@
$(USER_APP_BUILD_DIR)/$(1).elf: $(USER_APP_BUILD_DIR)/start.o $(USER_APP_BUILD_DIR)/$(1).o $(USER_APP_BUILD_DIR)/fsutil.o $(USER_APP_BUILD_DIR)/syscall.o user/apps/$(1)/linker.ld | $(USER_APP_BUILD_DIR)/.dir
	$$(USER_LD) -T user/apps/$(1)/linker.ld -m aarch64elf -e _start -z max-page-size=0x1000 -Map=$$(USER_APP_BUILD_DIR)/$(1).map -o $$@ $$(USER_APP_BUILD_DIR)/start.o $$(USER_APP_BUILD_DIR)/$(1).o $$(USER_APP_BUILD_DIR)/fsutil.o $$(USER_APP_BUILD_DIR)/syscall.o
endef

$(foreach app,ls mkdir touch rm rmdir mv pwd write append pidtest proctest procfault upper pipetest jobtest,$(eval $(call USER_FS_APP_RULES,$(app))))

$(BUILD_DIR)/panic.o: kernel/panic.c include/nimera/console.h include/nimera/panic.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/exception.o: kernel/exception.c include/nimera/console.h include/nimera/exception.h include/nimera/format.h include/nimera/halt.h include/nimera/process.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall.o: kernel/syscall.c include/nimera/abi/syscall.h include/nimera/abi/terminal.h include/nimera/console.h include/nimera/elf.h include/nimera/mmu.h include/nimera/pipe.h include/nimera/process.h include/nimera/scheduler.h include/nimera/syscall.h include/nimera/terminal.h include/nimera/vfs.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/irq.o: kernel/irq.c include/nimera/irq.h include/nimera/panic.h include/nimera/scheduler.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/scheduler.o: kernel/scheduler.c include/nimera/console.h include/nimera/format.h include/nimera/irq.h include/nimera/mmu.h include/nimera/panic.h include/nimera/process.h include/nimera/scheduler.h include/nimera/timer.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pipe.o: kernel/pipe.c include/nimera/abi/syscall.h include/nimera/heap.h include/nimera/irq.h include/nimera/pipe.h include/nimera/scheduler.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs.o: kernel/vfs.c include/nimera/heap.h include/nimera/panic.h include/nimera/ramfs.h include/nimera/version.h include/nimera/vfs.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ramfs.o: kernel/ramfs.c include/nimera/heap.h include/nimera/ramfs.h include/nimera/vfs.h | $(BUILD_DIR)/.dir
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

$(BUILD_DIR)/qemu-virtio.o: platform/qemu-virt/virtio.c include/nimera/block.h include/nimera/display.h include/nimera/input.h include/nimera/mmu.h include/nimera/panic.h include/nimera/pmm.h include/nimera/types.h include/nimera/virtio.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gic.o: platform/qemu-virt/gic.c include/nimera/irq.h include/nimera/panic.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pmm.o: kernel/pmm.c include/nimera/memory.h include/nimera/panic.h include/nimera/pmm.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/heap.o: kernel/heap.c include/nimera/heap.h include/nimera/panic.h include/nimera/pmm.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/format.o: kernel/format.c include/nimera/console.h include/nimera/format.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/block.o: kernel/block.c include/nimera/block.h include/nimera/panic.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/nimfs.o: kernel/nimfs.c include/nimera/heap.h include/nimera/nimfs.h include/nimera/panic.h include/nimera/version.h include/nimera/vfs.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shell.o: kernel/shell.c include/nimera/console.h include/nimera/editor.h include/nimera/elf.h include/nimera/format.h include/nimera/heap.h include/nimera/irq.h include/nimera/memory.h include/nimera/mmu.h include/nimera/nimfs.h include/nimera/pipe.h include/nimera/pmm.h include/nimera/process.h include/nimera/scheduler.h include/nimera/shell.h include/nimera/terminal.h include/nimera/timer.h include/nimera/version.h include/nimera/vfs.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/uart.o: platform/qemu-virt/uart.c include/nimera/irq.h include/nimera/panic.h include/nimera/scheduler.h include/nimera/types.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user-test.o: user/test_user.c include/nimera/user.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/user-syscall.o: user/syscall.S include/nimera/abi/syscall.h | $(BUILD_DIR)/.dir
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/.dir:
	mkdir -p $@

$(USER_APP_BUILD_DIR)/.dir:
	mkdir -p $@

run:
	rm -rf build-run
	$(MAKE) BUILD_DIR=build-run PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 TERMINAL_SIZE_NO_RESPONSE=1 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-run/baremetal-aarch64.elf,cpu-num=0

run-graphics: disk-create
	rm -rf build-graphics
	$(MAKE) BUILD_DIR=build-graphics GRAPHICS_TEST=1 BLOCK_TEST=0 NIMFS_BOOT=0 NIMFS_FORMAT_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m $(QEMU_MEMORY) -cpu cortex-a72 -display cocoa -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(STORAGE_IMAGE),format=raw,id=nimera-disk -device virtio-blk-device,drive=nimera-disk -device virtio-gpu-device -device loader,file=build-graphics/baremetal-aarch64.elf,cpu-num=0

run-fb-terminal: nimfs-elf-disk-create user-app
	rm -rf build-fb-terminal
	$(MAKE) BUILD_DIR=build-fb-terminal FRAMEBUFFER_TERMINAL=1 NIMFS_BOOT=1 ELF_INSTALL_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m $(QEMU_MEMORY) -cpu cortex-a72 -display cocoa -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimera-disk -device virtio-blk-device,drive=nimera-disk -device virtio-gpu-device -device virtio-keyboard-device,display=default -device virtio-tablet-device,display=default -device loader,file=build-fb-terminal/baremetal-aarch64.elf,cpu-num=0

run-fb-terminal-test: run-fb-terminal-test-build
	$(QEMU) -machine $(QEMU_MACHINE) -m $(QEMU_MEMORY) -cpu cortex-a72 -display cocoa -monitor none -global virtio-mmio.force-legacy=false -device virtio-gpu-device -device virtio-keyboard-device,display=default -device virtio-tablet-device,display=default -device loader,file=build-fb-terminal-test/baremetal-aarch64.elf,cpu-num=0

run-fb-terminal-test-build:
	rm -rf build-fb-terminal-test
	$(MAKE) BUILD_DIR=build-fb-terminal-test FRAMEBUFFER_TERMINAL=1 TERMINAL_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build

run-compositor:
	rm -rf build-compositor
	$(MAKE) BUILD_DIR=build-compositor FRAMEBUFFER_TERMINAL=0 COMPOSITOR_TEST=1 BLOCK_TEST=0 NIMFS_BOOT=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m $(QEMU_MEMORY) -cpu cortex-a72 -display cocoa -monitor none -serial stdio -global virtio-mmio.force-legacy=false -device virtio-gpu-device -device loader,file=build-compositor/baremetal-aarch64.elf,cpu-num=0

run-windows: nimfs-elf-disk-create user-app
	rm -rf build-windows
	$(MAKE) BUILD_DIR=build-windows FRAMEBUFFER_TERMINAL=1 WINDOW_TEST=1 NIMFS_BOOT=1 ELF_INSTALL_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m $(QEMU_MEMORY) -cpu cortex-a72 -display cocoa -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimera-disk -device virtio-blk-device,drive=nimera-disk -device virtio-gpu-device -device virtio-keyboard-device,display=default -device virtio-tablet-device,display=default -device loader,file=build-windows/baremetal-aarch64.elf,cpu-num=0

run-render-batching: disk-create
	rm -rf build-render-batching
	$(MAKE) BUILD_DIR=build-render-batching FRAMEBUFFER_TERMINAL=1 RENDER_BATCHING=1 RENDER_BATCHING_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m $(QEMU_MEMORY) -cpu cortex-a72 -display cocoa -monitor none -serial stdio -global virtio-mmio.force-legacy=false -device virtio-gpu-device -device loader,file=build-render-batching/baremetal-aarch64.elf,cpu-num=0

run-native-input-test:
	rm -rf build-native-input-test
	$(MAKE) BUILD_DIR=build-native-input-test FRAMEBUFFER_TERMINAL=1 INPUT_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 build
	$(QEMU) -machine $(QEMU_MACHINE) -m $(QEMU_MEMORY) -cpu cortex-a72 -display cocoa -monitor none -serial stdio -global virtio-mmio.force-legacy=false -device virtio-gpu-device -device virtio-keyboard-device,display=default -device virtio-tablet-device,display=default -device loader,file=build-native-input-test/baremetal-aarch64.elf,cpu-num=0

run-utf8-test:
	rm -rf build-utf8-test
	$(MAKE) BUILD_DIR=build-utf8-test UTF8_TEST=1 PANIC_TEST=0 TIMER_TEST=0 TERMINAL_SIZE_NO_RESPONSE=1 build
	$(QEMU) -machine $(QEMU_MACHINE) -cpu cortex-a72 -nographic -monitor none -serial stdio -device loader,file=build-utf8-test/baremetal-aarch64.elf,cpu-num=0

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

run-blocking:
	rm -rf build-blocking
	$(MAKE) BUILD_DIR=build-blocking PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=0 UART_IRQ_TEST=0 UART_OVERFLOW_TEST=0 SCHED_TEST=0 BLOCKING_TEST=1 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-blocking/baremetal-aarch64.elf,cpu-num=0

run-vfs:
	rm -rf build-vfs
	$(MAKE) BUILD_DIR=build-vfs PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=0 UART_IRQ_TEST=0 UART_OVERFLOW_TEST=0 SCHED_TEST=0 BLOCKING_TEST=0 VFS_TEST=1 VFS_WRITE_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-vfs/baremetal-aarch64.elf,cpu-num=0

run-vfs-write:
	rm -rf build-vfs-write
	$(MAKE) BUILD_DIR=build-vfs-write PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=0 UART_IRQ_TEST=0 UART_OVERFLOW_TEST=0 SCHED_TEST=0 BLOCKING_TEST=0 VFS_TEST=0 VFS_WRITE_TEST=1 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-vfs-write/baremetal-aarch64.elf,cpu-num=0

run-terminal:
	rm -rf build-terminal
	$(MAKE) BUILD_DIR=build-terminal PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=0 UART_IRQ_TEST=0 UART_OVERFLOW_TEST=0 SCHED_TEST=0 BLOCKING_TEST=0 VFS_TEST=0 VFS_WRITE_TEST=0 TERMINAL_TEST=1 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-terminal/baremetal-aarch64.elf,cpu-num=0

run-terminal-size:
	rm -rf build-terminal-size
	$(MAKE) BUILD_DIR=build-terminal-size PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=0 UART_IRQ_TEST=0 UART_OVERFLOW_TEST=0 SCHED_TEST=0 BLOCKING_TEST=0 VFS_TEST=0 VFS_WRITE_TEST=0 TERMINAL_TEST=0 TERMINAL_SIZE_TEST=1 TERMINAL_SIZE_NO_RESPONSE=0 EDITOR_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-terminal-size/baremetal-aarch64.elf,cpu-num=0

run-terminal-size-fallback:
	rm -rf build-terminal-size-fallback
	$(MAKE) BUILD_DIR=build-terminal-size-fallback PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=0 UART_IRQ_TEST=0 UART_OVERFLOW_TEST=0 SCHED_TEST=0 BLOCKING_TEST=0 VFS_TEST=0 VFS_WRITE_TEST=0 TERMINAL_TEST=0 TERMINAL_SIZE_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 EDITOR_TEST=0 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-terminal-size-fallback/baremetal-aarch64.elf,cpu-num=0

run-editor:
	rm -rf build-editor
	$(MAKE) BUILD_DIR=build-editor PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=0 UART_IRQ_TEST=0 UART_OVERFLOW_TEST=0 SCHED_TEST=0 BLOCKING_TEST=0 VFS_TEST=0 VFS_WRITE_TEST=0 TERMINAL_TEST=0 TERMINAL_SIZE_NO_RESPONSE=1 EDITOR_TEST=1 build
	$(QEMU) \
		-machine $(QEMU_MACHINE) \
		-m $(QEMU_MEMORY) \
		-cpu cortex-a72 \
		-nographic \
		-monitor none \
		-serial stdio \
		-device loader,file=build-editor/baremetal-aarch64.elf,cpu-num=0

disk-create:
	mkdir -p $(STORAGE_DIR)
	@test -e $(STORAGE_IMAGE) || $(QEMU_IMG) create -f raw $(STORAGE_IMAGE) $(STORAGE_SIZE)

disk-reset:
	rm -f $(STORAGE_IMAGE)
	$(MAKE) disk-create

run-block: disk-create
	rm -rf build-block
	$(MAKE) BUILD_DIR=build-block BLOCK_TEST=1 PANIC_TEST=0 TIMER_TEST=0 MEMORY_TEST=0 EXCEPTION_TEST=0 PMM_TEST=0 HEAP_TEST=0 MMU_TEST=0 MMU_FAULT_TEST=0 PROTECTION_TEST=0 PROTECTION_WRITE_TEST=0 PROTECTION_EXEC_TEST=0 IRQ_TEST=0 UART_IRQ_TEST=0 UART_OVERFLOW_TEST=0 SCHED_TEST=0 BLOCKING_TEST=0 VFS_TEST=0 VFS_WRITE_TEST=0 TERMINAL_TEST=0 TERMINAL_SIZE_TEST=0 EDITOR_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m $(QEMU_MEMORY) -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(STORAGE_IMAGE),format=raw,id=nimera-disk -device virtio-blk-device,drive=nimera-disk -device loader,file=build-block/baremetal-aarch64.elf,cpu-num=0

nimfs-disk-create:
	mkdir -p build-storage
	@test -e $(NIMFS_STORAGE_IMAGE) || $(QEMU_IMG) create -f raw $(NIMFS_STORAGE_IMAGE) 64M

nimfs-disk-reset:
	rm -f $(NIMFS_STORAGE_IMAGE)
	$(MAKE) nimfs-disk-create

nimfs-elf-disk-create:
	mkdir -p $(STORAGE_DIR)
	@test -e $(NIMFS_ELF_IMAGE) || $(QEMU_IMG) create -f raw $(NIMFS_ELF_IMAGE) 64M

nimfs-elf-disk-reset:
	rm -f $(NIMFS_ELF_IMAGE)
	$(MAKE) nimfs-elf-disk-create

nimfs-root-create:
	mkdir -p $(STORAGE_DIR)
	@test -e $(NIMFS_ROOT_IMAGE) || $(QEMU_IMG) create -f raw $(NIMFS_ROOT_IMAGE) 64M

nimfs-data-create:
	mkdir -p $(STORAGE_DIR)
	@test -e $(NIMFS_DATA_IMAGE) || $(QEMU_IMG) create -f raw $(NIMFS_DATA_IMAGE) 64M

nimfs-data-reset:
	rm -f $(NIMFS_DATA_IMAGE)
	$(MAKE) nimfs-data-create

nimfs-data2-create:
	mkdir -p $(STORAGE_DIR)
	@test -e $(NIMFS_DATA2_IMAGE) || $(QEMU_IMG) create -f raw $(NIMFS_DATA2_IMAGE) 64M

nimfs-data2-reset:
	rm -f $(NIMFS_DATA2_IMAGE)
	$(MAKE) nimfs-data2-create

nimfs-root-reset:
	rm -f $(NIMFS_ROOT_IMAGE)
	$(MAKE) nimfs-root-create

run-nimfs-format: nimfs-disk-create
	rm -rf build-nimfs-format
	$(MAKE) BUILD_DIR=build-nimfs-format NIMFS_FORMAT_TEST=1 NIMFS_BOOT=0 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_STORAGE_IMAGE),format=raw,id=nimfs-disk -device virtio-blk-device,drive=nimfs-disk -device loader,file=build-nimfs-format/baremetal-aarch64.elf,cpu-num=0

run-nimfs: nimfs-disk-create
	rm -rf build-nimfs
	$(MAKE) BUILD_DIR=build-nimfs NIMFS_FORMAT_TEST=0 NIMFS_BOOT=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_STORAGE_IMAGE),format=raw,id=nimfs-disk -device virtio-blk-device,drive=nimfs-disk -device loader,file=build-nimfs/baremetal-aarch64.elf,cpu-num=0

run-elf-format: nimfs-elf-disk-reset user-app
	rm -rf build-elf-format
	$(MAKE) BUILD_DIR=build-elf-format NIMFS_FORMAT_TEST=1 NIMFS_BOOT=0 ELF_INSTALL_TEST=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-elf-format/baremetal-aarch64.elf,cpu-num=0

run-elf: nimfs-elf-disk-create user-app
	rm -rf build-elf
	$(MAKE) BUILD_DIR=build-elf NIMFS_FORMAT_TEST=0 NIMFS_BOOT=1 ELF_INSTALL_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-elf/baremetal-aarch64.elf,cpu-num=0

run-elf-test: nimfs-elf-disk-create user-app
	rm -rf build-elf-test
	$(MAKE) BUILD_DIR=build-elf-test NIMFS_FORMAT_TEST=0 NIMFS_BOOT=1 ELF_INSTALL_TEST=1 ELF_RUN_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-elf-test/baremetal-aarch64.elf,cpu-num=0

run-user-files: nimfs-elf-disk-create user-app
	rm -rf build-user-files
	$(MAKE) BUILD_DIR=build-user-files NIMFS_FORMAT_TEST=0 NIMFS_BOOT=1 ELF_INSTALL_TEST=1 USER_FILES_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-user-files/baremetal-aarch64.elf,cpu-num=0

run-commands: nimfs-elf-disk-create user-app
	rm -rf build-commands
	$(MAKE) BUILD_DIR=build-commands NIMFS_BOOT=1 ELF_INSTALL_TEST=1 COMMAND_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-commands/baremetal-aarch64.elf,cpu-num=0

run-terminal-app: nimfs-elf-disk-create user-app
	rm -rf build-terminal-app
	$(MAKE) BUILD_DIR=build-terminal-app NIMFS_BOOT=1 ELF_INSTALL_TEST=1 TERMINAL_APP_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-terminal-app/baremetal-aarch64.elf,cpu-num=0

run-terminal-app-format: nimfs-elf-disk-reset user-app
	rm -rf build-terminal-format
	$(MAKE) BUILD_DIR=build-terminal-format NIMFS_FORMAT_TEST=1 ELF_INSTALL_TEST=1 TERMINAL_APP_TEST=1 TERMINAL_FAULT_TEST=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-terminal-format/baremetal-aarch64.elf,cpu-num=0

run-terminal-fault: nimfs-elf-disk-create user-app
	rm -rf build-terminal-fault
	$(MAKE) BUILD_DIR=build-terminal-fault NIMFS_BOOT=1 ELF_INSTALL_TEST=1 TERMINAL_FAULT_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-terminal-fault/baremetal-aarch64.elf,cpu-num=0

run-user-terminal: nimfs-elf-disk-create user-app
	rm -rf build-user-terminal
	$(MAKE) BUILD_DIR=build-user-terminal NIMFS_BOOT=1 ELF_INSTALL_TEST=1 TERMINAL_CHECK_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-user-terminal/baremetal-aarch64.elf,cpu-num=0

run-user-editor-format: nimfs-elf-disk-reset user-app
	rm -rf build-user-editor-format
	$(MAKE) BUILD_DIR=build-user-editor-format NIMFS_FORMAT_TEST=1 ELF_INSTALL_TEST=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-user-editor-format/baremetal-aarch64.elf,cpu-num=0

run-user-editor: nimfs-elf-disk-create user-app
	rm -rf build-user-editor
	$(MAKE) BUILD_DIR=build-user-editor NIMFS_BOOT=1 ELF_INSTALL_TEST=1 USER_EDITOR_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-user-editor/baremetal-aarch64.elf,cpu-num=0

run-user-editor-test: nimfs-elf-disk-create
	rm -rf build-user-editor-test
	$(MAKE) -B USER_EDITOR_SELF_TEST=1 build-user-app/edit.elf
	$(MAKE) BUILD_DIR=build-user-editor-test NIMFS_BOOT=1 ELF_INSTALL_TEST=1 USER_EDITOR_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-user-editor-test/baremetal-aarch64.elf,cpu-num=0

run-user-utils: nimfs-elf-disk-create user-app
	rm -rf build-user-utils
	$(MAKE) BUILD_DIR=build-user-utils NIMFS_BOOT=1 ELF_INSTALL_TEST=1 USER_UTILS_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-user-utils/baremetal-aarch64.elf,cpu-num=0

run-processes: nimfs-elf-disk-reset user-app
	rm -rf build-processes
	$(MAKE) BUILD_DIR=build-processes NIMFS_BOOT=1 NIMFS_FORMAT_TEST=1 ELF_INSTALL_TEST=1 PROCESS_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-processes/baremetal-aarch64.elf,cpu-num=0

run-pipes: nimfs-elf-disk-reset user-app
	rm -rf build-pipes
	$(MAKE) BUILD_DIR=build-pipes NIMFS_BOOT=1 NIMFS_FORMAT_TEST=1 ELF_INSTALL_TEST=1 PIPE_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-pipes/baremetal-aarch64.elf,cpu-num=0

run-redirection: nimfs-elf-disk-reset user-app
	rm -rf build-redirection
	$(MAKE) BUILD_DIR=build-redirection NIMFS_BOOT=1 NIMFS_FORMAT_TEST=1 ELF_INSTALL_TEST=1 REDIRECTION_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimfs-elf -device virtio-blk-device,drive=nimfs-elf -device loader,file=build-redirection/baremetal-aarch64.elf,cpu-num=0

run-jobs: nimfs-elf-disk-reset user-app
	rm -rf build-jobs
	$(MAKE) BUILD_DIR=build-jobs NIMFS_BOOT=1 NIMFS_FORMAT_TEST=1 ELF_INSTALL_TEST=1 JOBS_TEST=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_ELF_IMAGE),format=raw,id=nimera-disk -device virtio-blk-device,drive=nimera-disk -device loader,file=build-jobs/baremetal-aarch64.elf,cpu-num=0

run-nimfs-multi-format: nimfs-root-create nimfs-data-create
	rm -rf build-nimfs-multi-format
	$(MAKE) BUILD_DIR=build-nimfs-multi-format NIMFS_FORMAT_TEST=1 NIMFS_MULTI_FORMAT_TEST=1 NIMFS_BOOT=0 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_DATA_IMAGE),format=raw,id=nimfs-data -device virtio-blk-device,drive=nimfs-data -drive if=none,file=$(NIMFS_ROOT_IMAGE),format=raw,id=nimfs-root -device virtio-blk-device,drive=nimfs-root -device loader,file=build-nimfs-multi-format/baremetal-aarch64.elf,cpu-num=0

run-nimfs-multi: nimfs-root-create nimfs-data-create
	rm -rf build-nimfs-multi
	$(MAKE) BUILD_DIR=build-nimfs-multi NIMFS_FORMAT_TEST=0 NIMFS_BOOT=1 TERMINAL_SIZE_NO_RESPONSE=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_DATA_IMAGE),format=raw,id=nimfs-data -device virtio-blk-device,drive=nimfs-data -drive if=none,file=$(NIMFS_ROOT_IMAGE),format=raw,id=nimfs-root -device virtio-blk-device,drive=nimfs-root -device loader,file=build-nimfs-multi/baremetal-aarch64.elf,cpu-num=0

run-nimfs-data-format: run-nimfs-multi-format

run-nimfs-volume-format: nimfs-root-create nimfs-data-create nimfs-data2-create
	rm -rf build-nimfs-volume-format
	$(MAKE) BUILD_DIR=build-nimfs-volume-format NIMFS_FORMAT_TEST=1 NIMFS_MULTI_FORMAT_TEST=1 NIMFS_BOOT=0 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_DATA2_IMAGE),format=raw,id=nimfs-data2 -device virtio-blk-device,drive=nimfs-data2 -drive if=none,file=$(NIMFS_DATA_IMAGE),format=raw,id=nimfs-data -device virtio-blk-device,drive=nimfs-data -drive if=none,file=$(NIMFS_ROOT_IMAGE),format=raw,id=nimfs-root -device virtio-blk-device,drive=nimfs-root -device loader,file=build-nimfs-volume-format/baremetal-aarch64.elf,cpu-num=0

run-volume: nimfs-root-reset nimfs-data-reset nimfs-data2-reset
	$(MAKE) run-nimfs-volume-format
	rm -rf build-volume
	$(MAKE) BUILD_DIR=build-volume NIMFS_FORMAT_TEST=0 NIMFS_BOOT=1 NIMFS_VOLUME_TEST=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_DATA2_IMAGE),format=raw,id=nimfs-data2 -device virtio-blk-device,drive=nimfs-data2 -drive if=none,file=$(NIMFS_DATA_IMAGE),format=raw,id=nimfs-data -device virtio-blk-device,drive=nimfs-data -drive if=none,file=$(NIMFS_ROOT_IMAGE),format=raw,id=nimfs-root -device virtio-blk-device,drive=nimfs-root -device loader,file=build-volume/baremetal-aarch64.elf,cpu-num=0

run-user:
	rm -rf build-user
	$(MAKE) BUILD_DIR=build-user USER_TEST=1 USER_PROTECTION_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -device loader,file=build-user/baremetal-aarch64.elf,cpu-num=0

run-user-protection:
	rm -rf build-user-protection
	$(MAKE) BUILD_DIR=build-user-protection USER_TEST=0 USER_PROTECTION_TEST=1 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -device loader,file=build-user-protection/baremetal-aarch64.elf,cpu-num=0

run-mounts: nimfs-root-reset nimfs-data-reset
	$(MAKE) run-nimfs-multi-format
	rm -rf build-mounts
	$(MAKE) BUILD_DIR=build-mounts NIMFS_FORMAT_TEST=0 NIMFS_BOOT=1 NIMFS_MOUNT_TEST=1 BLOCK_TEST=0 build
	$(QEMU) -machine $(QEMU_MACHINE) -m 128M -cpu cortex-a72 -nographic -monitor none -serial stdio -global virtio-mmio.force-legacy=false -drive if=none,file=$(NIMFS_DATA_IMAGE),format=raw,id=nimfs-data -device virtio-blk-device,drive=nimfs-data -drive if=none,file=$(NIMFS_ROOT_IMAGE),format=raw,id=nimfs-root -device virtio-blk-device,drive=nimfs-root -device loader,file=build-mounts/baremetal-aarch64.elf,cpu-num=0


clean:
	rm -rf build build-run build-panic build-timer build-memory build-exception build-pmm build-heap build-mmu build-mmu-fault build-protection build-protection-write build-protection-exec build-irq build-uart-irq build-uart-overflow build-sched build-blocking build-vfs build-vfs-write build-terminal build-terminal-size build-terminal-size-fallback build-editor build-block build-user build-user-protection build-nimfs build-nimfs-format build-nimfs-multi-format build-nimfs-multi build-nimfs-volume-format build-mounts build-volume build-elf build-elf-format build-elf-test build-elf-test2 build-user-files build-user-format build-shell-user build-shell-terminal build-commands build-command-format build-terminal-app build-terminal-format build-terminal-fault build-user-terminal build-user-terminal-format build-user-editor build-user-editor-format build-user-editor-test build-user-editor-shell build-user-utils build-processes build-pipes build-redirection build-jobs build-jobs-interactive build-graphics build-fb-terminal build-fb-terminal-test build-compositor build-windows build-render-batching build-render-before build-native-input-test build-utf8-test build-user-app build-user-app-test build-utils build-utils-format
