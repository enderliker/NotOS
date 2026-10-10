/* Host-only orchestration mocks: the real poll_wait.c and pure socket policy. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/task.h"
#include "../kernel/poll_wait.h"
#include "../kernel/socket_events.h"
#include "../kernel/syscall_numbers.h"
#define ADDRESS 0x08001000u
static struct notos_pollfd input[NOTOS_POLL_MAX];
static struct task *active;
static u32 now;
static int allow_block=1,allow_range=1,copies,scans;
static u16 masks[NOTOS_FD_COUNT];
u32 clock_ms(void) {return now;}
int scheduler_can_block(void) {return allow_block;}
struct interrupt_frame *scheduler_block(struct interrupt_frame *frame) {
    active->state=TASK_BLOCKED;active->context=frame;return frame;
}
int space_range(const struct user_space *space,u32 address,u32 bytes,int writable) {
    (void)space;(void)writable;
    return !bytes || (allow_range && address==ADDRESS && bytes<=sizeof(input));
}
int space_copy_from_user(const struct user_space *space,u32 address,void *data,u32 bytes) {
    if (!space_range(space,address,bytes,0)) return 0;
    memcpy(data,input,bytes);return 1;
}
int space_copy_to_user(struct user_space *space,u32 address,const void *data,u32 bytes) {
    if (!space_range(space,address,bytes,1)) return 0;
    memcpy(input,data,bytes);copies++;return 1;
}
int fd_poll(struct fd_table *table,struct notos_pollfd *fds,u32 count) {
    (void)table;scans++;int result=0;
    for (u32 i=0;i<count;i++) {
        u16 events=fds[i].fd<0?0:(u32)fds[i].fd>=NOTOS_FD_COUNT?NOTOS_POLLNVAL:masks[fds[i].fd];
        fds[i].revents=events&(fds[i].events|NOTOS_POLL_ALWAYS);result+=fds[i].revents!=0;
    }
    return result;
}
static struct interrupt_frame start(struct task *task,u32 count,int timeout) {
    active=task;struct interrupt_frame frame={.ebx=ADDRESS,.ecx=count,.edx=(u32)timeout};
    /* Caller's frame must persist for resume; tests install a stable frame afterwards. */
    poll_wait_start(task,&frame);return frame;
}
static void policy(void) {
    struct socket_readiness s={.pending=1};assert(!socket_events(s));
    s=(struct socket_readiness){0};assert(socket_events(s)==(NOTOS_POLLHUP|NOTOS_POLLOUT));
    s=(struct socket_readiness){.connected=1};assert(!socket_events(s));
    s.writable=1;assert(socket_events(s)==NOTOS_POLLOUT);
    s.readable=1;assert(socket_events(s)==(NOTOS_POLLIN|NOTOS_POLLOUT));
    s.readable=0;s.eof=1;assert(socket_events(s)==(NOTOS_POLLIN|NOTOS_POLLOUT|NOTOS_POLLRDHUP));
    s.write_closed=1;assert(socket_events(s)&NOTOS_POLLHUP);
    s=(struct socket_readiness){.connected=1,.write_closed=1};assert(socket_events(s)==NOTOS_POLLOUT);
    s.read_closed=1;assert(socket_events(s)==(NOTOS_POLLIN|NOTOS_POLLOUT|NOTOS_POLLRDHUP|NOTOS_POLLHUP));
    s=(struct socket_readiness){.failed=1,.error=1};assert(socket_events(s)==(NOTOS_POLLERR|NOTOS_POLLHUP|NOTOS_POLLIN|NOTOS_POLLOUT));
    s.error=0;assert(!(socket_events(s)&NOTOS_POLLERR));
}
int main(void) {
    policy();struct task task={0};struct interrupt_frame frame;
    input[0]=(struct notos_pollfd){3,NOTOS_POLLIN,99};
    frame=start(&task,1,0);assert(frame.eax==0 && !input[0].revents && task.state!=TASK_BLOCKED);
    masks[3]=NOTOS_POLLIN;input[1]=input[0];frame=start(&task,2,-1);assert(frame.eax==2);
    masks[3]=0;input[0]=(struct notos_pollfd){20,0,99};input[1]=(struct notos_pollfd){-1,0xffff,99};
    frame=start(&task,2,123);assert(frame.eax==1 && input[0].revents==NOTOS_POLLNVAL && !input[1].revents);
    int previous_copies=copies,previous_scans=scans;
    frame=start(&task,NOTOS_POLL_MAX+1,0);assert((int)frame.eax==-ERROR_INVALID_ARGUMENT);
    allow_range=0;frame=start(&task,1,0);assert((int)frame.eax==-ERROR_BAD_ADDRESS);
    assert(copies==previous_copies && scans==previous_scans);allow_range=1;
    input[0]=(struct notos_pollfd){3,NOTOS_POLLIN,99};now=0xffffff00u;
    frame=start(&task,1,500);task.context=&frame;assert(task.state==TASK_BLOCKED && task.wait_duration==500);
    /* User memory changes must not change the fd/events snapshot retained by the kernel. */
    input[0]=(struct notos_pollfd){4,NOTOS_POLLOUT,99};masks[4]=NOTOS_POLLOUT;
    poll_wait_resume(&task,now+499);assert(task.state==TASK_BLOCKED);
    masks[3]=NOTOS_POLLIN;poll_wait_resume(&task,now+500);assert(task.state==TASK_READY && frame.eax==1);
    assert(input[0].fd==3 && input[0].events==NOTOS_POLLIN && input[0].revents==NOTOS_POLLIN);
    assert(!task.poll_count && !task.pending_buffer && !task.poll_fds[0].fd);
    memset(masks,0,sizeof(masks));input[0]=(struct notos_pollfd){3,NOTOS_POLLIN,99};
    frame=start(&task,1,100);task.context=&frame;
    poll_wait_resume(&task,now+100);assert(task.state==TASK_READY && frame.eax==0 && !input[0].revents);
    frame=start(&task,0,-7);task.context=&frame;assert(task.state==TASK_BLOCKED && !task.wait_duration);
    poll_wait_resume(&task,now+0x7fffffffu);assert(task.state==TASK_BLOCKED);
    task.state=TASK_UNUSED;poll_wait_resume(&task,now+1000);assert(task.state==TASK_UNUSED);
    allow_block=0;frame=start(&task,0,1);assert((int)frame.eax==-ERROR_UNSUPPORTED);
    frame=start(&task,0,0);assert(!frame.eax);
    allow_block=1;frame=start(&task,1,1);task.context=&frame;allow_range=0;
    poll_wait_resume(&task,now+1);assert(task.state==TASK_READY && (int)frame.eax==-ERROR_BAD_ADDRESS);
    puts("PASS: poll snapshots/ranges/count/duplicates/zero/infinite/cancel/deadline wrap and socket readiness policy");
}
