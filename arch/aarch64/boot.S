// The only hand-written AArch64 startup code in this milestone.
// It installs the linker's private stack, calls C, and has a safe fallback
// if kernel_main ever returns.

	.section .text.boot, "ax", %progbits
	.balign 4
	.global _start
	.type _start, %function
	.extern kernel_main
	.extern __stack_top

_start:
	// Form the absolute address of the stack top without a runtime or GOT.
	adrp	x0, __stack_top
	add	x0, x0, :lo12:__stack_top

	// AArch64 requires SP to be 16-byte aligned at public call boundaries.
	mov	sp, x0
	bl	kernel_main

// Returning from C must never fall through into arbitrary memory.
.Lidle:
	// WFE is a low-power wait; the loop remains safe even without interrupts.
	wfe
	b	.Lidle

	.size _start, . - _start
