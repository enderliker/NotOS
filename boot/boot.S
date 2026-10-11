/* NotOS BIOS loader: load 512 KiB plus a 320 KiB high image, then enter 32-bit protected mode. */
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
    /* BIOS E820 map: header at 0x5000, up to 64 entries of 24 bytes. */
    movl $0, 0x5000
    movl $0, 0x5004
    xor %ebx, %ebx
    mov $0x5008, %di
e820_next:
    movl $1, %es:20(%di)
    mov $0xe820, %eax
    mov $0x534d4150, %edx
    mov $24, %ecx
    push %ds
    push %es
    push %di
    int $0x15
    pop %di
    pop %es
    pop %ds
    jc memory_error
    cmp $0x534d4150, %eax
    jne memory_error
    cmp $20, %ecx
    jb memory_error
    cmp $24, %ecx
    ja memory_error
    incw 0x5004
    test %ebx, %ebx
    jz e820_done
    cmpw $64, 0x5004
    jae memory_error
    add $24, %di
    jmp e820_next
e820_done:
    movl $0x4e4d4150, 0x5000
    mov drive, %dl
    mov $0x41, %ah
    mov $0x55aa, %bx
    int $0x13
    jc disk_error
    cmp $0xaa55, %bx
    jne disk_error
    test $1, %cx
    jz disk_error
    movw $16, remaining
load_chunk:
    movw $64, packet+2
    mov $packet, %si
    mov drive, %dl
    mov $0x42, %ah
    push %ds
    push %es
    int $0x13
    pop %es
    pop %ds
    jc disk_error
    addw $0x800, packet+6
    addl $64, packet+8
    decw remaining
    jnz load_chunk
    movw $10, remaining
    movw $0x9000, packet+6
load_high:
    movw $64, packet+2
    mov $packet, %si
    mov drive, %dl
    mov $0x42, %ah
    push %ds
    push %es
    int $0x13
    pop %es
    pop %ds
    jc disk_error
    /* BIOS block move copies the bounce buffer above 1 MiB without touching VGA. */
    mov $move_table, %si
    mov $0x4000, %cx
    mov $0x87, %ah
    push %ds
    push %es
    int $0x15
    pop %es
    pop %ds
    jc disk_error
    addw $0x8000, move_destination+2
    adcb $0, move_destination+4
    addl $64, packet+8
    decw remaining
    jnz load_high
    mov $0x0013, %ax
    int $0x10
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

memory_error:
    mov $mem_error, %si
    jmp print_error
disk_error:
    mov $error, %si
print_error:
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
remaining: .word 0
drive: .byte 0
error: .asciz "NotOS: disk error"
mem_error: .asciz "NotOS: E820 error"
.align 8
move_table:
    .quad 0, 0
    .word 0xffff, 0
    .byte 9, 0x93
    .word 0
move_destination:
    .word 0xffff, 0
    .byte 0x10, 0x93
    .word 0
    .quad 0, 0
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
    mov $0x9f000, %esp
    jmp 0x10000
.org 510
.word 0xaa55
