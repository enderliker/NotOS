#include "syscall.h"
#include "syscall_numbers.h"
#include "task.h"
#include "scheduler.h"
#include "arch.h"
#include "clock.h"
#include "paging.h"
#include "wait_policy.h"
#include "platform_abi.h"
#include "file_abi.h"
#include "vfs.h"
#include "platform_tls.h"

static void probe(struct task *task, struct interrupt_frame *frame) {
    const struct user_interrupt_tail *tail = (const struct user_interrupt_tail *)(frame + 1);
    struct task_statistics *statistics = &task->statistics;
    statistics->observed_cpl = frame->cs & 3;
    statistics->observed_ss = tail->ss;
    statistics->observed_root = paging_current_root();
    statistics->stack_valid = (u32)frame >= arch_kernel_stack_low()
        && (u32)frame + sizeof(*frame) + sizeof(*tail) <= arch_kernel_stack_top()
        && tail->esp >= task->space.stack_low
        && tail->esp < task->space.stack_top;
    frame->eax = statistics->observed_cpl;
}

static void write_console(struct task *task, struct interrupt_frame *frame) {
    u32 bytes = frame->ecx;
    if (bytes > SYSCALL_WRITE_LIMIT) {
        frame->eax = (u32)-ERROR_INVALID_ARGUMENT;
        return;
    }
    if (!space_range(&task->space, frame->ebx, bytes, 0)) {
        frame->eax = (u32)-ERROR_BAD_ADDRESS;
        return;
    }
    /* No pointer is read before the whole span has been validated. */
    if (bytes) {
        const volatile u8 *buffer = (const volatile u8 *)frame->ebx;
        console_puts("USER OUT: ");
        for (u32 i = 0; i < bytes; ++i) {
            char character = (char)buffer[i];
            console_putc(character == '\n' || (character >= 32 && character <= 126)
                ? character : '?');
        }
    }
    frame->eax = bytes;
}

static void write_fd(struct task *task,struct interrupt_frame *frame) {
    u32 bytes=frame->edx,address=frame->ecx;
    if (bytes>NOTOS_IO_LIMIT) {frame->eax=(u32)-ERROR_INVALID_ARGUMENT;return;}
    if (!space_range(&task->space,address,bytes,0)) {frame->eax=(u32)-ERROR_BAD_ADDRESS;return;}
    frame->eax=(u32)fd_write(&task->files,(int)frame->ebx,(const void *)address,bytes);
}
static struct interrupt_frame *read_fd(struct task *task,struct interrupt_frame *frame) {
    u32 bytes=frame->edx,address=frame->ecx;
    if (bytes>NOTOS_IO_LIMIT) {frame->eax=(u32)-ERROR_INVALID_ARGUMENT;return frame;}
    if (!space_range(&task->space,address,bytes,1)) {frame->eax=(u32)-ERROR_BAD_ADDRESS;return frame;}
    int result=fd_read(&task->files,(int)frame->ebx,(void *)address,bytes);
    if (result==-ERROR_AGAIN && fd_blocking_input(&task->files,(int)frame->ebx)) {
        if (!scheduler_can_block()) {frame->eax=(u32)-ERROR_UNSUPPORTED;return frame;}
        task->wait_kind=TASK_WAIT_INPUT;task->pending_fd=(int)frame->ebx;
        task->pending_buffer=address;task->pending_bytes=bytes;
        return scheduler_block(frame);
    }
    frame->eax=(u32)result;return frame;
}
static int copy_path(struct task *task,u32 address,char path[NOTOS_PATH_BYTES]) {
    for (u32 i=0;i<NOTOS_PATH_BYTES;i++) {
        if (address>~0u-i || !space_range(&task->space,address+i,1,0)) return -ERROR_BAD_ADDRESS;
        path[i]=*(const char *)(address+i);
        if (!path[i]) return 0;
    }
    return -ERROR_NAME_TOO_LONG;
}
static void path_call(struct task *task,struct interrupt_frame *frame) {
    char path[NOTOS_PATH_BYTES];u32 number=frame->eax;
    int error=copy_path(task,frame->ebx,path);
    if (error) {frame->eax=(u32)error;return;}
    if (number==SYS_FD_OPEN) frame->eax=(u32)fd_open(&task->files,path,frame->ecx,frame->edx);
    else if (number==SYS_PATH_UNLINK) frame->eax=(u32)(starts(path,"/dev/")?-ERROR_READ_ONLY:vfs_unlink(path));
    else {
        struct notos_file_info info;
        if (!space_range(&task->space,frame->ecx,sizeof(info),1)) {frame->eax=(u32)-ERROR_BAD_ADDRESS;return;}
        error=fd_path_stat(path,&info);
        if (!error) space_copy_to_user(&task->space,frame->ecx,&info,sizeof(info));
        frame->eax=(u32)error;
    }
}
static void stat_fd(struct task *task,struct interrupt_frame *frame) {
    struct notos_file_info info;
    if (!space_range(&task->space,frame->ecx,sizeof(info),1)) {frame->eax=(u32)-ERROR_BAD_ADDRESS;return;}
    int error=fd_stat(&task->files,(int)frame->ebx,&info);
    if (!error) space_copy_to_user(&task->space,frame->ecx,&info,sizeof(info));
    frame->eax=(u32)error;
}
static void read_time(struct task *task,struct interrupt_frame *frame) {
    if (frame->ebx>NOTOS_CLOCK_MONOTONIC) {frame->eax=(u32)-ERROR_INVALID_ARGUMENT;return;}
    if (!space_range(&task->space,frame->ecx,sizeof(struct notos_timestamp),1)) {
        frame->eax=(u32)-ERROR_BAD_ADDRESS;return;
    }
    u64 seconds;u32 nanoseconds;
    if (frame->ebx==NOTOS_CLOCK_REALTIME) {
        struct utc_time utc;
        if (!rtc_utc(&utc) || utc.days<719528u) {frame->eax=(u32)-ERROR_IO;return;}
        seconds=(u64)(utc.days-719528u)*86400u+utc.seconds;nanoseconds=0;
    } else {
        u64 ms=clock_uptime_ms();seconds=ms/1000u;nanoseconds=(u32)(ms%1000u)*1000000u;
    }
    struct notos_timestamp result={(u32)seconds,(u32)(seconds>>32),nanoseconds};
    /* Raw ABI buffers need not be aligned; do not form an unaligned struct pointer. */
    const u8 *source=(const u8 *)&result;
    u8 *output=(u8 *)frame->ecx;
    for (u32 i=0;i<sizeof(result);i++) output[i]=source[i];
    frame->eax=0;
}
static void random_bytes(struct task *task,struct interrupt_frame *frame) {
    u32 bytes=frame->ecx;
    if (bytes>NOTOS_ENTROPY_LIMIT) {frame->eax=(u32)-ERROR_INVALID_ARGUMENT;return;}
    if (!space_range(&task->space,frame->ebx,bytes,1)) {frame->eax=(u32)-ERROR_BAD_ADDRESS;return;}
    if (!bytes) {frame->eax=0;return;}
    if (!entropy_ready()) {frame->eax=(u32)-ERROR_IO;return;}
    u8 *buffer=(u8 *)frame->ebx;
    while (bytes) {
        u32 n=bytes>64?64:bytes;
        if (!entropy_get(buffer,n)) {frame->eax=(u32)-ERROR_IO;return;}
        buffer+=n;bytes-=n;
    }
    frame->eax=0;
}

struct interrupt_frame *syscall_dispatch(struct interrupt_frame *frame) {
    struct task *task = task_current();
    if (!task || (frame->cs & 3) != 3) {
        frame->eax = (u32)-ERROR_PERMISSION;
        return frame;
    }
    task->statistics.syscalls++;
    switch (frame->eax) {
    case SYS_EXIT:
        return task_finish(frame, frame->ebx, TASK_EXIT_NORMAL);
    case SYS_YIELD:
        frame->eax = 0;
        return scheduler_yield(frame);
    case SYS_FD_OPEN:case SYS_PATH_STAT:case SYS_PATH_UNLINK:
        path_call(task,frame);break;
    case SYS_FD_READ:return read_fd(task,frame);
    case SYS_FD_CLOSE:frame->eax=(u32)fd_close(&task->files,(int)frame->ebx);break;
    case SYS_FD_SEEK:frame->eax=(u32)fd_seek(&task->files,(int)frame->ebx,(int)frame->ecx,frame->edx);break;
    case SYS_FD_STAT:stat_fd(task,frame);break;
    case SYS_FD_DUP:frame->eax=(u32)fd_dup(&task->files,(int)frame->ebx,(int)frame->ecx);break;
    case SYS_FD_CONTROL:frame->eax=(u32)fd_control(&task->files,(int)frame->ebx,frame->ecx,frame->edx);break;
    case SYS_FD_TRUNCATE:frame->eax=(u32)fd_truncate(&task->files,(int)frame->ebx,frame->ecx);break;
    case SYS_FD_ISATTY:frame->eax=(u32)fd_is_tty(&task->files,(int)frame->ebx);break;
    case SYS_USER_CHECK:
        frame->eax=frame->edx>1?(u32)-ERROR_INVALID_ARGUMENT:
            space_range(&task->space,frame->ebx,frame->ecx,(int)frame->edx)?0:(u32)-ERROR_BAD_ADDRESS;
        break;
    case SYS_PID:
        frame->eax=task->id;
        break;
    case SYS_FD_WRITE:
        write_fd(task,frame);
        break;
    case SYS_TIME:
        read_time(task,frame);
        break;
    case SYS_ENTROPY:
        random_bytes(task,frame);
        break;
    case SYS_TICKS:
        frame->eax = clock_ms();
        break;
    case SYS_SLEEP:
        if (frame->ebx>WAIT_MAX_MS) {
            frame->eax=(u32)-ERROR_INVALID_ARGUMENT;
            break;
        }
        if (!frame->ebx) {
            frame->eax=0;
            return scheduler_yield(frame);
        }
        if (!scheduler_can_block()) {
            frame->eax=(u32)-ERROR_UNSUPPORTED;
            break;
        }
        task->wait_kind=TASK_WAIT_TIMER;
        task->wait_started=clock_ms();
        task->wait_duration=frame->ebx;
        frame->eax=0;
        return scheduler_block(frame);
    case SYS_BRK:
        frame->eax=space_brk(&task->space,frame->ebx);
        break;
    case SYS_MMAP:
        frame->eax=space_mmap(&task->space,frame->ebx);
        if (!frame->eax) frame->eax=(u32)-ERROR_MEMORY;
        break;
    case SYS_MUNMAP:
        frame->eax=space_munmap(&task->space,frame->ebx,frame->ecx)?0:(u32)-ERROR_INVALID_ARGUMENT;
        break;
    case SYS_PROBE:
        probe(task, frame);
        break;
    case SYS_WRITE:
        write_console(task, frame);
        break;
    case SYS_TEST_REPORT:
        frame->eax = frame->ebx == task->expected_marker
            && paging_current_root() == task->space.root;
        break;
    default:
        frame->eax = (u32)-ERROR_NO_SYSCALL;
        break;
    }
    return frame;
}
