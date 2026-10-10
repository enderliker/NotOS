#ifndef NOTOS_USER_H
#define NOTOS_USER_H
#include "interrupt.h"
#include "user_memory.h"
void user_command(const char *scenario);
void user_syscall(struct interrupt_frame *frame);
void user_fault(struct interrupt_frame *frame);
void user_timer(struct interrupt_frame *frame);
#endif
