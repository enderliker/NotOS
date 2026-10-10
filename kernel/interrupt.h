#ifndef NOTOS_INTERRUPT_H
#define NOTOS_INTERRUPT_H
#include "base.h"
/* Same-ring interrupt frame. ESP is pushal's saved ESP, not a user stack. */
struct interrupt_frame {
    u32 gs,fs,es,ds;
    u32 edi,esi,ebp,saved_esp,ebx,edx,ecx,eax;
    u32 vector,error,eip,cs,eflags;
};
void interrupts_init(void);
void interrupt_dispatch(struct interrupt_frame *frame);
int interrupt_selftest(void);
unsigned interrupts_breakpoints(void);
void fault_divide(void);
void fault_opcode(void);
void fault_gp(void);
#endif
