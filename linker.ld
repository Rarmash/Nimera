/* QEMU virt RAM starts at 0x40000000; 0x40000000..0x40100000 holds the DTB. */
ENTRY(_start)

SECTIONS
{
	/* Start after QEMU's default DTB placement to avoid a loader overlap. */
	. = 0x40100000;
	__kernel_start = .;

	.text : ALIGN(0x1000)
	{
		__text_start = .;
		KEEP(*(.text.boot))
		. = ALIGN(2048);
		KEEP(*(.text.vector))
		*(.text .text.*)
		. = ALIGN(0x1000);
		__text_end = .;
	}

	.rodata : ALIGN(0x1000)
	{
		__rodata_start = .;
		*(.rodata .rodata.*)
		. = ALIGN(0x1000);
		__rodata_end = .;
	}

	.data : ALIGN(0x1000)
	{
		__data_start = .;
		*(.data .data.*)
		. = ALIGN(0x1000);
		__data_end = .;
	}

	.bss (NOLOAD) : ALIGN(0x1000)
	{
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(0x1000);
		__bss_end = .;

		/* Private 16 KiB stack; SP is initialized to its high address. */
		__stack_bottom = .;
		. += 16K;
		. = ALIGN(0x1000);
		__stack_top = .;
	}

	/* Link-time embedded user image. These sections are mapped separately. */
	.user.text : ALIGN(0x1000)
	{
		__user_text_start = .;
		*(.user.text .user.text.*)
		. = ALIGN(0x1000);
		__user_text_end = .;
	}

	.user.rodata : ALIGN(0x1000)
	{
		__user_rodata_start = .;
		*(.user.rodata .user.rodata.*)
		. = ALIGN(0x1000);
		__user_rodata_end = .;
	}

	.user.data : ALIGN(0x1000)
	{
		__user_data_start = .;
		*(.user.data .user.data.*)
		. = ALIGN(0x1000);
		__user_data_end = .;
	}

	.user.bss (NOLOAD) : ALIGN(0x1000)
	{
		__user_stack_bottom = .;
		*(.user.bss .user.bss.*)
		. = ALIGN(0x1000);
		__user_stack_top = .;
	}

	__kernel_end = .;

	/DISCARD/ :
	{
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}
}
