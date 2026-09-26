// Minimal architecture-specific CPU halt primitive shared by fatal paths.

	.section .text, "ax", %progbits
	.balign 4
	.global cpu_halt
	.type cpu_halt, %function

cpu_halt:
.Lhalt:
	// WFE safely stops this CPU until an event; no interrupt subsystem is needed.
	wfe
	b	.Lhalt

	.size cpu_halt, . - cpu_halt
