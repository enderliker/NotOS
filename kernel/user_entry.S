.section .text
.code32
.global gdt_load
gdt_load:
    mov 4(%esp), %eax
    lgdt (%eax)
    ljmp $0x08, $1f
1:  mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ret
.global user_enter
user_enter:
    pushfl
    pushal
    mov %esp, user_saved_sp
    cli
    mov 40(%esp), %edx
    mov 44(%esp), %ecx
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    pushl $0x23
    pushl %ecx
    pushl $0x202
    pushl $0x1b
    pushl %edx
    /* Do not disclose the caller's register contents to the user program. */
    xor %eax, %eax
    xor %ebx, %ebx
    xor %ecx, %ecx
    xor %edx, %edx
    xor %esi, %esi
    xor %edi, %edi
    xor %ebp, %ebp
    iret
.global user_return
user_return:
    cli
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov user_saved_sp, %esp
    popal
    popfl
    ret
.section .bss
.align 4
user_saved_sp: .skip 4
.section .note.GNU-stack,"",@progbits
