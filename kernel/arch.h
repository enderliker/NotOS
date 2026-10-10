#ifndef NOTOS_ARCH_H
#define NOTOS_ARCH_H
#include "base.h"
#include "interrupt.h"
void arch_init(void);
u32 arch_kernel_stack_low(void);
u32 arch_kernel_stack_top(void);
void arch_select_kernel_stack(u32 low, u32 top);
void arch_reset_kernel_stack(void);
void arch_run_context(struct interrupt_frame *frame, u32 root);
void arch_return_to_caller(void) __attribute__((noreturn));
#endif
