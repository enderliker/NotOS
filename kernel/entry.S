.section .text.entry
.code32
.global _start
.extern kernel_main, __bss_start, __bss_end
_start:
    cld
    mov $__bss_start, %edi
    mov $__bss_end, %ecx
    sub %edi, %ecx
    shr $2, %ecx
    xor %eax, %eax
    rep stosl
    call kernel_main
1:  cli
    hlt
    jmp 1b
.section .note.GNU-stack,"",@progbits
