	.section .text, "ax", %progbits
	.balign 4
	.global nimera_write
	.type nimera_write, %function
	nimera_write:
	mov x8, #1
	svc #0
	ret

	.global nimera_exit
	.type nimera_exit, %function
	nimera_exit:
	mov x8, #2
	svc #0
.Lexit:
	wfe
	b .Lexit
