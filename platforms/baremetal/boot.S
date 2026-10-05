/* Multiboot entry for rdgeneric. GRUB or QEMU's -kernel loads this. */
.section .multiboot, "a"
.align 4
.long 0x1BADB002            /* magic */
.long 0                     /* flags */
.long -(0x1BADB002)         /* checksum */

.section .bss
.align 16
stack_bottom:
.skip 65536
stack_top:

.section .text
.global _start
_start:
    mov $stack_top, %esp
    /* Zero .bss ourselves rather than trust the loader. */
    mov $__bss_start, %edi
    mov $__bss_end, %ecx
    sub %edi, %ecx
    xor %eax, %eax
    cld
    rep stosb
    mov $stack_top, %esp
    call kmain
1:  cli
    hlt
    jmp 1b

/* IRQ0, the PIT at 1000 Hz. */
.global isr_timer
isr_timer:
    pushal
    incl ticks
    movb $0x20, %al
    outb %al, $0x20
    popal
    iret
