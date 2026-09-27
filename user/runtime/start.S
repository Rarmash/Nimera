	.section .text.boot, "ax", %progbits
	.balign 4
	.global _start
	.type _start, %function
	.extern main
	.extern nimera_exit
_start:
	bl main
	bl nimera_exit
.Lhalt:
	wfe
	b .Lhalt
