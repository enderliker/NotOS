#include "syscall_numbers.h"
#include "user_test_abi.h"
.section .rodata.user,"a"
.code32
.global user_blob_start,user_blob_end
user_blob_start:
    mov $SYS_PROBE, %eax
    int $0x80
    cmp $3, %eax
    jne user_fail
    std
    mov $SYS_PROBE, %eax
    int $0x80
    pushfl
    pop %edx
    test $0x400, %edx
    jz user_fail
    cld
    cmpl $TEST_MODE_FPU, 0x08001000
    je unsupported_fpu
    cmpl $TEST_MODE_YIELD, 0x08001000
    je worker_begin
    cmpl $TEST_MODE_WORKER, 0x08001000
    je worker_begin
    mov $SYS_TICKS, %eax
    int $0x80
    mov %eax, 0x08001004
user_wait:
    mov $SYS_TICKS, %eax
    int $0x80
    sub 0x08001004, %eax
    cmp $20, %eax
    jb user_wait
    movl $0x12345678, 0x08001008
    mov 0x0800100c, %ebx
    mov $SYS_TEST_REPORT, %eax
    int $0x80
    cmp $1, %eax
    jne user_fail
    mov $255, %eax
    int $0x80
    cmp $-ERROR_NO_SYSCALL, %eax
    jne user_fail
    mov 0x08001000, %eax
    cmp $1, %eax
    je read_kernel
    cmp $2, %eax
    je write_code
    cmp $3, %eax
    je forbidden_io
    cmp $4, %eax
    je write_guard
    cmp $5, %eax
    je forbidden_gate
    cmp $6, %eax
    je bad_buffers
    cmp $7, %eax
    je foreign_read
    cmp $8, %eax
    je spin_forever
    cmp $9, %eax
    je valid_write
    cmp $13, %eax
    je nx_data
    cmp $14, %eax
    je nx_stack
user_success:
    mov $42, %ebx
    jmp user_exit
read_kernel:
    mov 0x00010000, %eax
    jmp user_fail
write_code:
    movb $0, 0x08000000
    jmp user_fail
forbidden_io:
    mov $0x3f8, %dx
    mov $65, %al
    out %al, %dx
    jmp user_fail
write_guard:
    movl $1, 0x08002000
    jmp user_fail
forbidden_gate:
    int $0x20
    jmp user_fail
foreign_read:
    mov 0x08001014, %edx
    mov (%edx), %eax
    jmp user_fail
spin_forever:
    jmp spin_forever
.macro reject_buffer pointer, length, expected
    mov $SYS_WRITE, %eax
    mov $\pointer, %ebx
    mov $\length, %ecx
    int $0x80
    cmp $\expected, %eax
    jne user_fail
.endm
bad_buffers:
    reject_buffer 0x10000, 4, -14
    reject_buffer 0x08002000, 4, -14
    reject_buffer 0x08001ffc, 8, -14
    reject_buffer 0xfffffff8, 16, -14
    reject_buffer 0x08001000, 161, -22
    reject_buffer 0xffffffff, 0, 0
    /* Synchronous diagnostic batches cannot register a managed-process wait. */
    mov $SYS_SLEEP, %eax
    mov $1, %ebx
    int $0x80
    cmp $-ERROR_UNSUPPORTED, %eax
    jne user_fail
    jmp user_success
valid_write:
    mov $SYS_WRITE, %eax
    mov $(0x08000000+user_text-user_blob_start), %ebx
    mov $(user_text_end-user_text), %ecx
    int $0x80
    cmp $(user_text_end-user_text), %eax
    jne user_fail
    jmp user_success
nx_data:
    mov $0x08001000, %eax
    jmp *%eax
nx_stack:
    mov $0x08003000, %eax
    jmp *%eax
user_fail:
    cld
    mov $93, %ebx
user_exit:
    mov $SYS_EXIT, %eax
    int $0x80
    ud2
unsupported_fpu:
    fldz /* CR0.TS must raise #NM; floating-point state is deliberately unsupported. */
    jmp user_fail
/* Canary registers and DF survive timer and voluntary context switches. */
worker_begin:
    mov 0x0800100c, %ebx
    mov $0x33445566, %edx
    mov $0x55667788, %esi
    mov $0x66778899, %edi
    mov $0x778899aa, %ebp
    mov $SYS_TICKS, %eax
    int $0x80
    mov %eax, 0x08001010
    std
worker_loop:
    cmp 0x0800100c, %ebx
    jne user_fail
    cmp $0x33445566, %edx
    jne user_fail
    cmp $0x55667788, %esi
    jne user_fail
    cmp $0x66778899, %edi
    jne user_fail
    cmp $0x778899aa, %ebp
    jne user_fail
    pushfl
    pop %ecx
    test $0x400, %ecx
    jz user_fail
    incl 0x08001018
    cmpl $TEST_MODE_YIELD, 0x08001000
    jne worker_clock
    mov $SYS_YIELD, %eax
    int $0x80
    test %eax, %eax
    jne user_fail
worker_clock:
    mov $SYS_TICKS, %eax
    int $0x80
    sub 0x08001010, %eax
    cmp $80, %eax
    jb worker_loop
    cld
    mov $SYS_TEST_REPORT, %eax
    int $0x80
    cmp $1, %eax
    jne user_fail
    movl $0x12345678, 0x08001008
    jmp user_success
user_text: .ascii "hello from ring3\n"
user_text_end:
user_blob_end:
.section .note.GNU-stack,"",@progbits
