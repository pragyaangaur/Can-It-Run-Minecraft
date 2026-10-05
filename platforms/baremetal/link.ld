ENTRY(_start)
SECTIONS {
    . = 1M;
    .text : { KEEP(*(.multiboot)) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    __bss_start = .;
    .bss : { *(.bss*) *(COMMON) }
    __bss_end = .;
}
