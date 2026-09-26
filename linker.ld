/* QEMU virt RAM starts at 0x40000000; 0x40000000..0x40100000 holds the DTB. */
ENTRY(_start)

SECTIONS
{
	/* Start after QEMU's default DTB placement to avoid a loader overlap. */
	. = 0x40100000;

	.text : ALIGN(16)
	{
		KEEP(*(.text.boot))
		*(.text .text.*)
	}

	.rodata : ALIGN(16)
	{
		*(.rodata .rodata.*)
	}

	.data : ALIGN(16)
	{
		*(.data .data.*)
	}

	.bss (NOLOAD) : ALIGN(16)
	{
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		__bss_end = .;

		/* Private 16 KiB stack; SP is initialized to its high address. */
		. = ALIGN(16);
		__stack_bottom = .;
		. += 16K;
		__stack_top = .;
	}

	/DISCARD/ :
	{
		*(.comment)
		*(.note*)
		*(.eh_frame*)
	}
}
