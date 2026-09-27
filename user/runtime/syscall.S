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
	.global nimera_read_key
nimera_read_key:
	mov x8, #7
	svc #0
	ret
	.global nimera_terminal_size
nimera_terminal_size:
	mov x8, #8
	svc #0
	ret
	.global nimera_terminal_clear
nimera_terminal_clear:
	mov x8, #9
	svc #0
	ret
	.global nimera_terminal_move_cursor
nimera_terminal_move_cursor:
	mov x8, #10
	svc #0
	ret
	.global nimera_terminal_clear_line
nimera_terminal_clear_line:
	mov x8, #11
	svc #0
	ret
	.global nimera_terminal_cursor_visible
nimera_terminal_cursor_visible:
	mov x8, #12
	svc #0
	ret
	.global nimera_alloc
nimera_alloc:
	mov x8, #13
	svc #0
	ret
	.global nimera_free
nimera_free:
	mov x8, #14
	svc #0
	ret
	.global nimera_open_directory
nimera_open_directory:
	mov x8, #15
	svc #0
	ret
	.global nimera_read_directory
nimera_read_directory:
	mov x8, #16
	svc #0
	ret
	.global nimera_mkdir
nimera_mkdir:
	mov x8, #17
	svc #0
	ret
	.global nimera_unlink
nimera_unlink:
	mov x8, #18
	svc #0
	ret
	.global nimera_rmdir
nimera_rmdir:
	mov x8, #19
	svc #0
	ret
	.global nimera_rename
nimera_rename:
	mov x8, #20
	svc #0
	ret
	.global nimera_getcwd
nimera_getcwd:
	mov x8, #21
	svc #0
	ret
	.global nimera_getpid
nimera_getpid:
	mov x8, #22
	svc #0
	ret
