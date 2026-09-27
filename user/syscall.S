// Minimal user-side syscall stubs. They contain no kernel symbol references.
	.section .user.text, "ax", %progbits
	.balign 4
	.global nimera_write
	.type nimera_write, %function

nimera_write:
	// x0 = user buffer, x1 = byte length, x8 = Nimera syscall number.
	mov x8, #1
	svc #0
	ret

	.global nimera_exit
	.type nimera_exit, %function

nimera_exit:
	// x0 = exit status; a successful exit never returns to user code.
	mov x8, #2
	svc #0
.Lexit_wait:
	wfe
	b .Lexit_wait
