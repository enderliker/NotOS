#include "user.h"
#include "arch.h"
#include "space.h"
#include "paging.h"
#include "physical.h"
#include "clock.h"
extern const u8 user_blob_start[],user_blob_end[];
static unsigned active,syscalls,cpl,ss,stack_ok,exit_code,reason,started;
static unsigned fault_vector,fault_error,fault_cr2;
static u32 expected,observed_root;
static struct user_space *current;
#define BUDGET_MS 250u
static void finish(unsigned status,unsigned why) __attribute__((noreturn));
static void finish(unsigned status,unsigned why) {exit_code=status;reason=why;active=0;user_return();}
void user_fault(struct interrupt_frame *f) {
    if(!active||(f->cs&3)!=3) return;
    /* Hardware/system-fatal faults must not be mislabeled as a recoverable task failure. */
    if(f->vector==2||f->vector==8||f->vector==18) return;
    fault_vector=f->vector;fault_error=f->error;fault_cr2=0;
    if(f->vector==14) __asm__ volatile("mov %%cr2,%0" : "=r"(fault_cr2));
    finish(128+f->vector,2);
}
void user_timer(struct interrupt_frame *f) {
    if(active&&(f->cs&3)==3 && (unsigned)(clock_ms()-started)>=BUDGET_MS) finish(124,3);
}
void user_syscall(struct interrupt_frame *f) {
    if(!active || (f->cs&3)!=3) {f->eax=0xffffffffu;return;}
    syscalls++;
    if(f->eax==0) finish(f->ebx,1);
    else if(f->eax==1) f->eax=clock_ms();
    else if(f->eax==2) {
        const struct user_interrupt_tail *tail=(const struct user_interrupt_tail *)(f+1);
        cpl=f->cs&3;ss=tail->ss;observed_root=paging_current_root();
        stack_ok=(u32)f>=arch_kernel_stack_low() && (u32)f+sizeof(*f)+sizeof(*tail)<=arch_kernel_stack_top();
        if(tail->esp<USER_BASE+12288 || tail->esp>=USER_BASE+16384) stack_ok=0;
        f->eax=cpl;
    } else if(f->eax==3) {
        u32 n=f->ecx,pointer=f->ebx;
        if(n>160) {f->eax=(u32)-22;return;}
        if(!space_range(current,pointer,n,0)) {f->eax=(u32)-14;return;}
        if(n) {
            /* Validate the entire span before touching a user address. */
            console_puts("USER OUT: ");const volatile u8 *data=(const volatile u8 *)pointer;
            for(unsigned i=0;i<n;i++) {char c=(char)data[i];console_putc((c=='\n'||(c>=32&&c<=126))?c:'?');}
        }
        f->eax=n;
    } else if(f->eax==4) f->eax=(f->ebx==expected && paging_current_root()==current->root)?1:0;
    else f->eax=(u32)-38;
}
static int run(struct user_space *space,unsigned mode,u32 value) {
    current=space;expected=value;syscalls=cpl=ss=stack_ok=exit_code=reason=fault_vector=fault_error=fault_cr2=observed_root=0;
    ((u32 *)space->data)[0]=mode;((u32 *)space->data)[3]=value;
    unsigned ticks=clock_ticks();u32 root=paging_current_root();started=clock_ms();active=1;
    user_enter(USER_BASE,USER_BASE+16384-16,space->root);
    active=0;
    unsigned delta=clock_ticks()-ticks;
    console_puts("USER: ring=");console_uint(cpl);console_puts(" ss=");console_uint(ss);
    console_puts(" syscalls=");console_uint(syscalls);console_puts(" exit=");console_uint(exit_code);
    console_puts(" irq_delta=");console_uint(delta);console_putc('\n');
    console_puts("USER: cr3=");console_uint(observed_root);console_puts(" expected=");console_uint(space->root);
    console_puts(" kernel_restored=");console_uint(paging_current_root()==root);console_putc('\n');
    if(reason==2) {
        console_puts("USER FAULT: vector=");console_uint(fault_vector);console_puts(" error=");console_uint(fault_error);
        console_puts(" cr2=");console_uint(fault_cr2);console_puts(" cpl=3; task terminated\n");
    }
    if(reason==3) console_puts("USER: budget exceeded; task terminated\n");
    if(cpl!=3||ss!=0x23||!stack_ok||observed_root!=space->root||paging_current_root()!=root||delta<15) return 0;
    if(mode==8) return reason==3 && exit_code==124 && delta>=BUDGET_MS-2;
    if(mode==1) return reason==2&&fault_vector==14&&fault_error==5&&fault_cr2==0x10000;
    if(mode==2) return reason==2&&fault_vector==14&&fault_error==7&&fault_cr2==USER_BASE;
    if(mode==3) return reason==2&&fault_vector==13&&fault_error==0;
    if(mode==4) return reason==2&&fault_vector==14&&fault_error==6&&fault_cr2==USER_BASE+8192;
    if(mode==5) return reason==2&&fault_vector==13&&fault_error==258;
    if(mode==7) return reason==2&&fault_vector==14&&fault_error==5&&fault_cr2==((u32 *)space->data)[5];
    return reason==1&&exit_code==42&&((u32 *)space->data)[2]==0x12345678;
}
void user_command(const char *scenario) {
    static const char *const names[]={"demo","kernel-read","code-write","io","guard","gate","bad-buffer","foreign-read","spin","write"};
    unsigned mode=0;while(mode<10&&!equal(scenario,names[mode])) mode++;
    if(mode==10) {console_puts("USER ERROR: unknown scenario\n");return;}
    if(active) {console_puts("USER ERROR: already running\n");return;}
    unsigned before=physical_available();u32 root=paging_current_root();
    struct user_space a={0},b={0};unsigned size=(unsigned)(user_blob_end-user_blob_start);
    int ok=space_create(&a,user_blob_start,size)&&space_create(&b,user_blob_start,size);
    if(ok) {
        if(a.root==b.root||a.data==b.data) ok=0;
        ((u32 *)a.data)[5]=b.data;
        ok=run(&a,mode,0x11111111)&&ok;
        if(((u32 *)b.data)[2]!=0) ok=0; /* A must not write B's private data. */
        if(mode==0) {
            ok=run(&b,0,0x22222222)&&ok;
            if(((u32 *)a.data)[2]!=0x12345678) ok=0;
        }
    }
    current=0;
    int cleanup_a=space_destroy(&a),cleanup_b=space_destroy(&b);
    if(paging_current_root()!=root||!cleanup_a||!cleanup_b||physical_available()!=before) ok=0;
    console_puts(ok?"USER: private-CR3/TSS/syscall/cleanup PASS\n":"USER ERROR: isolation scenario failed\n");
}
