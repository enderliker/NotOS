/* Position-independent, real ring-3 program copied into a user read-only page. */
.section .rodata.user,"a"
.code32
.global user_blob_start,user_blob_end
user_blob_start:
    mov $2, %eax
    int $0x80
    cmp $3, %eax
    jne user_fail
    std
    mov $2, %eax
    int $0x80
    pushfl
    pop %edx
    test $0x400, %edx
    jz user_fail
    cld
    mov $1, %eax
    int $0x80
    mov %eax, 0x08001004
user_wait:
    mov $1, %eax
    int $0x80
    sub 0x08001004, %eax
    cmp $20, %eax
    jb user_wait
    movl $0x12345678, 0x08001008
    mov $255, %eax
    int $0x80
    cmp $-38, %eax
    jne user_fail
    mov $42, %ebx
    jmp user_exit
user_fail:
    cld
    mov $93, %ebx
user_exit:
    xor %eax, %eax
    int $0x80
    ud2
user_blob_end:
.section .note.GNU-stack,"",@progbits
