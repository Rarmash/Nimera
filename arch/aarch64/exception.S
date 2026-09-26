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

exception_sync:
	mov x0, #0
	b exception_capture

exception_irq:
	mov x0, #1
	b exception_capture

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
