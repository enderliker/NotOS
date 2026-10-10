#ifndef NOTOS_SYSCALL_H
#define NOTOS_SYSCALL_H
#include "interrupt.h"
struct interrupt_frame *syscall_dispatch(struct interrupt_frame *frame);
#endif
