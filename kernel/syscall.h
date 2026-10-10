#ifndef NOTOS_SYSCALL_H
#define NOTOS_SYSCALL_H
#include "interrupt.h"
void syscall_dispatch(struct interrupt_frame *frame);
#endif
