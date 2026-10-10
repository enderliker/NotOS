#include "base.h"
#include "interrupt.h"
#include "pic.h"
#include "clock.h"
#include "keyboard.h"
#include "user.h"
struct idt_gate {u16 low,selector;u8 zero,flags;u16 high;} __attribute__((packed));
struct idt_pointer {u16 limit;u32 base;} __attribute__((packed));
static struct idt_gate idt[256] __attribute__((aligned(16)));
extern void (*const isr_table[256])(void);
static volatile unsigned breakpoints;
static volatile unsigned irq_counts[16],spurious7,spurious15,stray;
static volatile unsigned panic_active;
_Static_assert(sizeof(struct idt_gate)==8,"IDT gate size");
_Static_assert(sizeof(struct idt_pointer)==6,"IDTR size");
_Static_assert(__builtin_offsetof(struct interrupt_frame,vector)==48,"assembly vector offset");
_Static_assert(__builtin_offsetof(struct interrupt_frame,eip)==56,"assembly EIP offset");
_Static_assert(sizeof(struct interrupt_frame)==68,"assembly frame size");
static void hex(u32 value) {
    static const char digits[]="0123456789abcdef";
    console_puts("0x");
    for(int shift=28;shift>=0;shift-=4) console_putc(digits[(value>>shift)&15]);
}
static void stop(void) __attribute__((noreturn));
static void stop(void) {for(;;) __asm__ volatile("cli; hlt" ::: "memory");}
static void panic(struct interrupt_frame *f) __attribute__((noreturn));
static void panic(struct interrupt_frame *f) {
    static const char *const names[32]={
        "Divide Error","Debug","NMI","Breakpoint","Overflow","Bound Range",
        "Invalid Opcode","Device Not Available","Double Fault","Reserved",
        "Invalid TSS","Segment Not Present","Stack Fault","General Protection",
        "Page Fault","Reserved","x87 Floating Point","Alignment Check",
        "Machine Check","SIMD Floating Point","Virtualization","Control Protection",
        "Reserved","Reserved","Reserved","Reserved","Reserved","Reserved",
        "Hypervisor Injection","VMM Communication","Security","Reserved"
    };
    __asm__ volatile("cli" ::: "memory");
    if(panic_active++) stop();
    console_puts("\nPANIC: vector=");console_int((int)f->vector);
    console_puts(" ");console_puts(f->vector<32?names[f->vector]:"Unhandled Interrupt");
    console_puts("\nERROR=");hex(f->error);console_puts(" EIP=");hex(f->eip);
    console_puts(" CS=");hex(f->cs);console_puts(" EFLAGS=");hex(f->eflags);
    console_puts("\nEAX=");hex(f->eax);console_puts(" EBX=");hex(f->ebx);
    console_puts(" ECX=");hex(f->ecx);console_puts(" EDX=");hex(f->edx);
    console_puts("\nESI=");hex(f->esi);console_puts(" EDI=");hex(f->edi);
    console_puts(" EBP=");hex(f->ebp);console_puts(" SAVED_ESP=");hex(f->saved_esp);
    if(f->vector==14) {
        u32 cr2;__asm__ volatile("mov %%cr2,%0" : "=r"(cr2));
        console_puts("\nCR2=");hex(cr2);
    }
    console_puts("\nSYSTEM HALTED: restart required\n");
    stop();
}
void interrupts_init(void) {
    __asm__ volatile("cli" ::: "memory");
    for(unsigned n=0;n<256;n++) {
        u32 address=(u32)(unsigned long)isr_table[n];
        idt[n].low=(u16)address;idt[n].selector=0x08;
        idt[n].zero=0;idt[n].flags=n==128?0xee:0x8e;idt[n].high=(u16)(address>>16);
    }
    const struct idt_pointer pointer={sizeof(idt)-1,(u32)(unsigned long)idt};
    __asm__ volatile("lidt %0" :: "m"(pointer) : "memory");
}
unsigned interrupts_breakpoints(void) {return breakpoints;}
unsigned interrupts_irq_count(unsigned irq) {return irq<16?irq_counts[irq]:0;}
unsigned interrupts_spurious_count(unsigned irq) {return irq==7?spurious7:irq==15?spurious15:0;}
unsigned interrupts_stray_count(void) {return stray;}
void interrupt_dispatch(struct interrupt_frame *frame) {
    if(frame->vector<32 && (frame->cs&3)==3) user_fault(frame);
    if(frame->vector==128) {user_syscall(frame);return;}
    if(frame->vector==3) {breakpoints++;return;}
    if(frame->vector>=32 && frame->vector<48) {
        unsigned irq=frame->vector-32;
        if(pic_spurious(irq)) {
            if(irq==7) spurious7++;else spurious15++;
            return;
        }
        irq_counts[irq]++;
        if(irq==0) {clock_irq();pic_eoi(irq);user_timer(frame);return;}
        else if(irq==1) keyboard_irq();
        else {stray++;pic_mask(irq);}
        pic_eoi(irq);
        return;
    }
    panic(frame);
}
