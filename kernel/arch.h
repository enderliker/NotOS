#ifndef NOTOS_ARCH_H
#define NOTOS_ARCH_H
#include "base.h"
void arch_init(void);
u32 arch_kernel_stack_low(void);
u32 arch_kernel_stack_top(void);
void user_enter(u32 entry,u32 stack,u32 root);
void user_return(void) __attribute__((noreturn));
#endif
