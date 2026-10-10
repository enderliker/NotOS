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
.global arch_run_context
arch_run_context:
    pushfl
    pushal
    mov %esp, caller_saved_sp
    cli
    mov %cr3, %eax
    mov %eax, caller_kernel_root
    mov 40(%esp), %edx
    mov 44(%esp), %eax
    mov %eax, %cr3
    mov %edx, %esp
    jmp interrupt_restore
.global arch_return_to_caller
arch_return_to_caller:
    cli
    mov caller_kernel_root, %eax
    mov %eax, %cr3
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov caller_saved_sp, %esp
    popal
    popfl
    ret
.section .bss
.align 4
caller_saved_sp: .skip 4
caller_kernel_root: .skip 4
.section .note.GNU-stack,"",@progbits
