/* Managed-process wait orchestration, independent of readiness and libc ABI. */
#include "poll_wait.h"
#include "task.h"
#include "scheduler.h"
#include "syscall_numbers.h"
#include "clock.h"
#include "wait_policy.h"
static void complete(struct task *task,int result) {
    u32 bytes=task->poll_count*sizeof(struct notos_pollfd);
    if (!space_copy_to_user(&task->space,task->pending_buffer,task->poll_fds,bytes)) result=-ERROR_BAD_ADDRESS;
    task->context->eax=(u32)result;task->state=TASK_READY;
    task->pending_buffer=task->poll_count=task->wait_duration=0;
    for (u32 i=0;i<NOTOS_POLL_MAX;i++) task->poll_fds[i]=(struct notos_pollfd){0};
}
struct interrupt_frame *poll_wait_start(struct task *task,struct interrupt_frame *frame) {
    u32 count=frame->ecx,bytes;int timeout=(int)frame->edx;
    if (count>NOTOS_POLL_MAX) {frame->eax=(u32)-ERROR_INVALID_ARGUMENT;return frame;}
    bytes=count*sizeof(struct notos_pollfd);
    /* Validate the complete input/output vector before touching descriptors or outputs. */
    if (!space_range(&task->space,frame->ebx,bytes,1)) {frame->eax=(u32)-ERROR_BAD_ADDRESS;return frame;}
    struct notos_pollfd fds[NOTOS_POLL_MAX];
    if (!space_copy_from_user(&task->space,frame->ebx,fds,bytes)) {frame->eax=(u32)-ERROR_BAD_ADDRESS;return frame;}
    int result=fd_poll(&task->files,fds,count);
    if (result || !timeout) {
        space_copy_to_user(&task->space,frame->ebx,fds,bytes);frame->eax=(u32)result;return frame;
    }
    if (!scheduler_can_block()) {frame->eax=(u32)-ERROR_UNSUPPORTED;return frame;}
    /* Only immutable fd/events snapshots survive the syscall; never retain user pointers as arrays. */
    for (u32 i=0;i<count;i++) task->poll_fds[i]=fds[i];
    task->poll_count=count;task->pending_buffer=frame->ebx;task->wait_kind=TASK_WAIT_POLL;
    task->wait_started=clock_ms();task->wait_duration=timeout<0?0:(u32)timeout;
    return scheduler_block(frame);
}
void poll_wait_resume(struct task *task,u32 now) {
    if (task->state!=TASK_BLOCKED || task->wait_kind!=TASK_WAIT_POLL) return;
    int ready=fd_poll(&task->files,task->poll_fds,task->poll_count);
    /* Ready events take precedence when readiness and timeout arrive in the same iteration. */
    if (ready || (task->wait_duration && wait_due(task->wait_started,task->wait_duration,now))) complete(task,ready);
}
