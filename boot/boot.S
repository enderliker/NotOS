/* NotOS BIOS loader: load 32 KiB, then enter 32-bit protected mode. */
.section .text
.code16
.global _start
_start:
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x7c00, %sp
    sti
    mov %dl, drive
    mov $0x41, %ah
    mov $0x55aa, %bx
    int $0x13
    jc disk_error
    cmp $0xaa55, %bx
    jne disk_error
    test $1, %cx
    jz disk_error
    mov $packet, %si
    mov drive, %dl
    mov $0x42, %ah
    int $0x13
    jc disk_error
    cli
    in $0x92, %al
    or $2, %al
    and $0xfe, %al
    out %al, $0x92
    lgdt gdt_ptr
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0
    ljmp $0x08, $protected

disk_error:
    mov $error, %si
1:  lodsb
    test %al, %al
    jz 2f
    mov $0x0e, %ah
    mov $7, %bx
    int $0x10
    jmp 1b
2:  cli
    hlt
    jmp 2b
.align 4
packet:
    .byte 16, 0
    .word 64
    .word 0, 0x1000
    .quad 1
drive: .byte 0
error: .asciz "NotOS: disk error"
.align 8
gdt:
    .quad 0
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt_ptr:
    .word 23
    .long gdt
.code32
protected:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov $0x90000, %esp
    jmp 0x10000
.org 510
.word 0xaa55
