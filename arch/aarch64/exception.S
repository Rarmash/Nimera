// AArch64 exception vectors and the small register capture bridge.

	.section .text.vector, "ax", %progbits
	.balign 2048
	.global exception_vectors
	.type exception_vectors, %function

.macro VECTOR_ENTRY target
	b \target
	.space 124
.endm

exception_vectors:
	// Current EL using SP_EL0.
	VECTOR_ENTRY exception_sync
	VECTOR_ENTRY exception_irq
	VECTOR_ENTRY exception_fiq
	VECTOR_ENTRY exception_serror
	// Current EL using the active SP_ELx.
	VECTOR_ENTRY exception_sync
	VECTOR_ENTRY exception_irq
	VECTOR_ENTRY exception_fiq
	VECTOR_ENTRY exception_serror
	// Lower EL using AArch64.
	VECTOR_ENTRY exception_sync
	VECTOR_ENTRY exception_irq
	VECTOR_ENTRY exception_fiq
	VECTOR_ENTRY exception_serror
	// Lower EL using AArch32.
	VECTOR_ENTRY exception_sync
	VECTOR_ENTRY exception_irq
	VECTOR_ENTRY exception_fiq
	VECTOR_ENTRY exception_serror

	.size exception_vectors, . - exception_vectors

	.text
	.balign 4
	.extern exception_fatal
	.extern irq_handle

exception_sync:
	mov x0, #0
	b exception_capture

exception_irq:
	// IRQs may interrupt arbitrary C code. Save every general-purpose
	// register and the exception return state before calling C. The frame is
	// also the complete saved context that the scheduler can switch to.
	sub sp, sp, #288
	stp x0, x1, [sp, #0]
	stp x2, x3, [sp, #16]
	stp x4, x5, [sp, #32]
	stp x6, x7, [sp, #48]
	stp x8, x9, [sp, #64]
	stp x10, x11, [sp, #80]
	stp x12, x13, [sp, #96]
	stp x14, x15, [sp, #112]
	stp x16, x17, [sp, #128]
	stp x18, x19, [sp, #144]
	stp x20, x21, [sp, #160]
	stp x22, x23, [sp, #176]
	stp x24, x25, [sp, #192]
	stp x26, x27, [sp, #208]
	stp x28, x29, [sp, #224]
	str x30, [sp, #240]
	mrs x9, elr_el1
	mrs x10, spsr_el1
	str x9, [sp, #256]
	str x10, [sp, #264]
	mov x0, sp
	bl irq_handle
	mov sp, x0
	ldr x9, [sp, #256]
	ldr x10, [sp, #264]
	msr elr_el1, x9
	msr spsr_el1, x10
	ldr x30, [sp, #240]
	ldp x28, x29, [sp, #224]
	ldp x26, x27, [sp, #208]
	ldp x24, x25, [sp, #192]
	ldp x22, x23, [sp, #176]
	ldp x20, x21, [sp, #160]
	ldp x18, x19, [sp, #144]
	ldp x16, x17, [sp, #128]
	ldp x14, x15, [sp, #112]
	ldp x12, x13, [sp, #96]
	ldp x10, x11, [sp, #80]
	ldp x8, x9, [sp, #64]
	ldp x6, x7, [sp, #48]
	ldp x4, x5, [sp, #32]
	ldp x2, x3, [sp, #16]
	ldp x0, x1, [sp, #0]
	add sp, sp, #288
	eret

	.balign 4
	.global thread_bootstrap
	.type thread_bootstrap, %function
	.extern thread_entry_returned

thread_bootstrap:
	// x1 holds the entry function and x0 holds its argument in the
	// synthetic frame created by scheduler_init().
	blr x1
	bl thread_entry_returned
	.size thread_bootstrap, . - thread_bootstrap

exception_fiq:
	mov x0, #2
	b exception_capture

exception_serror:
	mov x0, #3

exception_capture:
	// Keep the vector category in x5 while selecting the active EL registers.
	mov x5, x0
	mrs x6, currentel
	lsr x7, x6, #2
	and x7, x7, #3

	cmp x7, #1
	b.eq .Lcapture_el1
	cmp x7, #2
	b.eq .Lcapture_el2

	// The configured vector should only run at EL1 or EL2. Unknown levels are
	// still reported through the common fatal path with zero diagnostics.
	mov x1, xzr
	mov x2, xzr
	mov x3, xzr
	b .Lcall_fatal

.Lcapture_el1:
	mrs x1, esr_el1
	mrs x2, elr_el1
	mrs x3, far_el1
	b .Lcall_fatal

.Lcapture_el2:
	mrs x1, esr_el2
	mrs x2, elr_el2
	mrs x3, far_el2

.Lcall_fatal:
	mov x0, x5
	mov x4, x7
	bl exception_fatal
	// exception_fatal is noreturn; keep a safe fallback if that contract changes.
.Lexception_fallback:
	wfe
	b .Lexception_fallback
