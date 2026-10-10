#ifndef NOTOS_POLL_WAIT_H
#define NOTOS_POLL_WAIT_H
#include "base.h"
struct task;
struct interrupt_frame;
struct interrupt_frame *poll_wait_start(struct task *,struct interrupt_frame *);
void poll_wait_resume(struct task *,u32 now);
#endif
