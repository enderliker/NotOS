/* Freestanding ABI probe. It is not a libc or curl port. */
#include "../kernel/base.h"
#include "../kernel/syscall_numbers.h"
#ifndef DEMO_FAULT
#define DEMO_FAULT 0
#endif
static volatile u8 bss[12289];
static volatile u32 initialized=0x12345678;
static int call(u32 number,u32 argument,u32 second) {
    u32 result;
    __asm__ volatile("int $0x80":"=a"(result):"a"(number),"b"(argument),"c"(second):"memory","cc");
    return (int)result;
}
static void exit_status(unsigned status) __attribute__((noreturn));
static void exit_status(unsigned status) {call(SYS_EXIT,status,0);for (;;) {}}
static void check(int condition) {if (!condition) exit_status(90);}
void _start(void) __attribute__((noreturn,section(".text._start")));
void _start(void) {
    volatile u8 stack[12289];
    check(call(SYS_PROBE,0,0)==3 && initialized==0x12345678);
    if (DEMO_FAULT==5) {
        check(call(SYS_SLEEP,60001,0)==-ERROR_INVALID_ARGUMENT);
        check(call(SYS_SLEEP,0,0)==0);
        const char begin[]="ELF sleep begin\n",end[]="ELF sleep woke\n";
        check(call(SYS_WRITE,(u32)begin,sizeof(begin)-1)==sizeof(begin)-1);
        u32 now=(u32)call(SYS_TICKS,0,0);
        u32 flags;
        __asm__ volatile("pushfl; popl %0":"=r"(flags));
        flags|=0x4000u; /* User NT must not survive a managed context restore. */
        __asm__ volatile("pushl %0; popfl"::"r"(flags):"memory","cc");
        check(call(SYS_SLEEP,600,0)==0);
        __asm__ volatile("pushfl; popl %0":"=r"(flags));
        check(!(flags&0x4000u));
        check((u32)call(SYS_TICKS,0,0)-now>=600);
        check(call(SYS_WRITE,(u32)end,sizeof(end)-1)==sizeof(end)-1);
        exit_status(0);
    }
    u8 salt=(u8)call(SYS_TICKS,0,0);
    initialized+=salt;
    for (unsigned i=0;i<sizeof(bss);i++) {
        check(bss[i]==0);
        bss[i]=(u8)(i+salt);
        stack[i]=(u8)(i^salt);
    }
    u32 base=(u32)call(SYS_BRK,0,0);
    check(base==0x08400000);
    check((u32)call(SYS_BRK,base+12289,0)==base+12289);
    volatile u8 *heap=(volatile u8 *)base;
    for (unsigned i=0;i<12289;i++) {check(heap[i]==0);heap[i]=0xc7;}
    if (DEMO_FAULT==1) ((void (*)(void))base)();
    if (DEMO_FAULT==2) ((void (*)(void))(u32)stack)();
    if (DEMO_FAULT==3) *(volatile u8 *)0x08feffff=1;
    if (DEMO_FAULT==4) *(volatile u8 *)0x08000000=1;
    check((u32)call(SYS_BRK,0x08800001,0)==base+12289);
    check((u32)call(SYS_BRK,base+17,0)==base+17);
    check(call(SYS_WRITE,base+4096,1)==-ERROR_BAD_ADDRESS);
    check((u32)call(SYS_BRK,base+8192,0)==base+8192);
    for (unsigned i=4096;i<8192;i++) check(heap[i]==0);
    check((u32)call(SYS_BRK,base,0)==base);
    check(call(SYS_MMAP,0,0)==-ERROR_MEMORY);
    check(call(SYS_MMAP,0xffffffff,0)==-ERROR_MEMORY);
    u32 bytes=0x201000;
    u32 mapping=(u32)call(SYS_MMAP,bytes,0);
    check(mapping==0x08800000);
    volatile u8 *anon=(volatile u8 *)mapping;
    check(anon[0]==0 && anon[0x200000]==0);
    anon[0]=salt;
    u32 begun=(u32)call(SYS_TICKS,0,0);
    while ((u32)call(SYS_TICKS,0,0)-begun<200) call(SYS_YIELD,0,0);
    check(anon[0]==salt && initialized==0x12345678u+salt);
    for (unsigned i=0;i<sizeof(bss);i++) check(bss[i]==(u8)(i+salt) && stack[i]==(u8)(i^salt));
    anon[0x1ffffe]='S';anon[0x1fffff]='P';anon[0x200000]='A';anon[0x200001]='N';
    check(call(SYS_WRITE,mapping+0x1ffffe,4)==4);
    check(call(SYS_MUNMAP,mapping+1,bytes)==-ERROR_INVALID_ARGUMENT);
    check(call(SYS_MUNMAP,base,4096)==-ERROR_INVALID_ARGUMENT);
    check(call(SYS_MUNMAP,mapping,bytes)==0);
    check(call(SYS_WRITE,mapping,1)==-ERROR_BAD_ADDRESS);
    check((u32)call(SYS_MMAP,4096,0)==mapping && *(volatile u8 *)mapping==0);
    check(call(SYS_MUNMAP,mapping,4096)==0);
    const char message[]="ELF32 BSS/stack/brk/mmap PASS\n";
    check(call(SYS_WRITE,(u32)message,sizeof(message)-1)==sizeof(message)-1);
    exit_status(0);
}
