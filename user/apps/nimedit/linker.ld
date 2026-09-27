ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
  . = 0x10000000;
  .text ALIGN(0x1000) : { *(.text.boot .text .text.*) } :text
  .rodata ALIGN(0x1000) : { *(.rodata .rodata.*) } :rodata
  .data ALIGN(0x1000) : { *(.data .data.*) } :data
  .bss ALIGN(0x1000) (NOLOAD) : { *(.bss .bss.* COMMON) } :data
}
