.section .text._start,"ax"
.code32
.global _start
.type _start,@function
_start:
    cld
    xor %ebp,%ebp
    mov (%esp),%eax
    lea 4(%esp),%edx
    and $-16,%esp
    sub $8,%esp
    push %edx
    push %eax
    call notos_start
    ud2
.size _start,.-_start
.section .note.GNU-stack,"",@progbits
