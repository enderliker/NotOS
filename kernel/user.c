#include "user.h"
#include "arch.h"
#include "paging.h"
#include "physical.h"
#include "clock.h"
extern const u8 user_blob_start[],user_blob_end[];
static unsigned active,syscalls,cpl,ss,stack_ok,exit_code;
static u32 frames[3];
static const u32 addresses[3]={USER_BASE,USER_BASE+4096,USER_BASE+12288};
void user_syscall(struct interrupt_frame *f) {
    if(!active || (f->cs&3)!=3) {f->eax=0xffffffffu;return;}
    syscalls++;
    if(f->eax==0) {exit_code=f->ebx;user_return();}
    else if(f->eax==1) f->eax=clock_ms();
    else if(f->eax==2) {
        const struct user_interrupt_tail *tail=(const struct user_interrupt_tail *)(f+1);
        cpl=f->cs&3;ss=tail->ss;
        stack_ok=(u32)f>=arch_kernel_stack_low() && (u32)f+sizeof(*f)+sizeof(*tail)<=arch_kernel_stack_top();
        if(tail->esp<USER_BASE+12288 || tail->esp>=USER_BASE+16384) stack_ok=0;
        f->eax=cpl;
    } else f->eax=(u32)-38;
}
void user_command(void) {
    if(active) {console_puts("USER ERROR: already running\n");return;}
    unsigned before=physical_available(),mapped=0;
    for(unsigned i=0;i<3;i++) frames[i]=0;
    unsigned size=(unsigned)(user_blob_end-user_blob_start);
    if(size>4096) {console_puts("USER ERROR: image exceeds one page\n");return;}
    for(unsigned i=0;i<3;i++) {
        if(paging_translate(addresses[i])) break;
        frames[i]=physical_alloc();if(!frames[i]) break;
        volatile u8 *page=(volatile u8 *)frames[i];
        for(unsigned j=0;j<4096;j++) page[j]=0;
        if(i==0) for(unsigned j=0;j<size;j++) page[j]=user_blob_start[j];
        if(!paging_user_map(addresses[i],frames[i],i!=0)) break;
        mapped++;
    }
    int ok=mapped==3;
    if(ok) {
        syscalls=cpl=ss=stack_ok=exit_code=0;active=1;
        unsigned ticks=clock_ticks();
        user_enter(USER_BASE,USER_BASE+16384-16);
        active=0;
        unsigned delta=clock_ticks()-ticks;
        console_puts("USER: ring=");console_uint(cpl);console_puts(" ss=");console_uint(ss);
        console_puts(" syscalls=");console_uint(syscalls);console_puts(" exit=");console_uint(exit_code);
        console_puts(" irq_delta=");console_uint(delta);console_putc('\n');
        if(cpl!=3||ss!=0x23||!stack_ok||exit_code!=42||delta<15) ok=0;
    }
    for(unsigned i=0;i<mapped;i++) paging_user_unmap(addresses[i]);
    for(unsigned i=0;i<3;i++) if(frames[i]&&!physical_free(frames[i])) ok=0;
    if(physical_available()!=before) ok=0;
    console_puts(ok?"USER: ring3/TSS/syscall/cleanup PASS\n":"USER ERROR: ring3 test failed\n");
}
