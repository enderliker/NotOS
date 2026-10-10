#ifndef NOTOS_USER_H
#define NOTOS_USER_H
#include "interrupt.h"
#define USER_BASE 0x08000000u
#define USER_SIZE 0x00400000u
void user_command(void);
void user_syscall(struct interrupt_frame *frame);
#endif
