#ifndef NOTOS_INTERRUPT_H
#define NOTOS_INTERRUPT_H
#include "base.h"
/* Common frame. A CPL change appends user ESP/SS after EFLAGS. */
struct interrupt_frame {
    u32 gs,fs,es,ds;
    u32 edi,esi,ebp,saved_esp,ebx,edx,ecx,eax;
    u32 vector,error,eip,cs,eflags;
};
struct user_interrupt_tail {u32 esp,ss;};
void interrupts_init(void);
struct interrupt_frame *interrupt_dispatch(struct interrupt_frame *frame);
int interrupt_selftest(void);
unsigned interrupts_breakpoints(void);
unsigned interrupts_irq_count(unsigned irq);
unsigned interrupts_spurious_count(unsigned irq);
unsigned interrupts_stray_count(void);
void fault_divide(void);
void fault_opcode(void);
void fault_gp(void);
#endif
