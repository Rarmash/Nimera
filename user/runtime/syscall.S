	.section .text, "ax", %progbits
	.balign 4
	.global nimera_write
nimera_write:
	mov x8, #1
	svc #0
	ret
	.global nimera_exit
nimera_exit:
	mov x8, #2
	svc #0
	ret
	.global nimera_open
nimera_open:
	mov x8, #3
	svc #0
	ret
	.global nimera_read
nimera_read:
	mov x8, #4
	svc #0
	ret
	.global nimera_write_file
nimera_write_file:
	mov x8, #5
	svc #0
	ret
	.global nimera_close
nimera_close:
	mov x8, #6
	svc #0
	ret
