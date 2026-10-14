#include "syscall.h"
#include "poll_wait.h"
#include "socket_stack.h"
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
#include "path_core.h"
#include "executable.h"
#include "elf.h"
#include "system_file.h"
#include "argument_core.h"
#include "platform_tls.h"
#include "process.h"

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

static struct interrupt_frame *wait_network(struct task *task,struct interrupt_frame *frame,
        enum task_wait_kind kind,int handle,u32 flags) {
    if (!scheduler_can_block()) {frame->eax=(u32)-ERROR_UNSUPPORTED;return frame;}
    task->wait_kind=kind;task->pending_fd=(int)frame->ebx;
    task->pending_buffer=frame->ecx;task->pending_bytes=frame->edx;task->pending_flags=flags;
    task->wait_started=clock_ms();task->wait_duration=socket_timeout(handle,kind!=TASK_WAIT_RECEIVE);
    if (kind==TASK_WAIT_CONNECT && !task->wait_duration) task->wait_duration=10000;
    return scheduler_block(frame);
}
static struct interrupt_frame *write_fd(struct task *task,struct interrupt_frame *frame) {
    u32 bytes=frame->edx,address=frame->ecx,flags=frame->eax==SYS_SEND?frame->esi:0;
    int handle=fd_socket_handle(&task->files,(int)frame->ebx);
    if (frame->eax==SYS_SEND && handle<0) {frame->eax=(u32)handle;return frame;}
    if (bytes>NOTOS_IO_LIMIT || (flags&~NOTOS_MSG_DONTWAIT)) {frame->eax=(u32)-ERROR_INVALID_ARGUMENT;return frame;}
    if (!space_range(&task->space,address,bytes,0)) {frame->eax=(u32)-ERROR_BAD_ADDRESS;return frame;}
    int result=fd_write(&task->files,(int)frame->ebx,(const void *)address,bytes);
    if (result==-ERROR_AGAIN && handle>=0 && !fd_nonblocking(&task->files,(int)frame->ebx) && !(flags&NOTOS_MSG_DONTWAIT))
        return wait_network(task,frame,TASK_WAIT_SEND,handle,flags);
    if (result==-ERROR_AGAIN && fd_blocking_output(&task->files,(int)frame->ebx)) {
        if (!scheduler_can_block()) {frame->eax=(u32)-ERROR_UNSUPPORTED;return frame;}
        task->wait_kind=TASK_WAIT_OUTPUT;task->pending_fd=(int)frame->ebx;
        task->pending_buffer=address;task->pending_bytes=bytes;
        return scheduler_block(frame);
    }
    frame->eax=(u32)result;return frame;
}
static struct interrupt_frame *read_fd(struct task *task,struct interrupt_frame *frame) {
    u32 bytes=frame->edx,address=frame->ecx,flags=frame->eax==SYS_RECV?frame->esi:0;
    int handle=fd_socket_handle(&task->files,(int)frame->ebx);
    if (frame->eax==SYS_RECV && handle<0) {frame->eax=(u32)handle;return frame;}
    if (bytes>NOTOS_IO_LIMIT || (flags&~(NOTOS_MSG_DONTWAIT|NOTOS_MSG_PEEK))) {frame->eax=(u32)-ERROR_INVALID_ARGUMENT;return frame;}
    if (!space_range(&task->space,address,bytes,1)) {frame->eax=(u32)-ERROR_BAD_ADDRESS;return frame;}
    int result=handle>=0?socket_read(handle,(void *)address,bytes,flags):fd_read(&task->files,(int)frame->ebx,(void *)address,bytes);
    if (result==-ERROR_AGAIN && handle>=0 && !fd_nonblocking(&task->files,(int)frame->ebx) && !(flags&NOTOS_MSG_DONTWAIT))
        return wait_network(task,frame,TASK_WAIT_RECEIVE,handle,flags);
    if (result==-ERROR_AGAIN && fd_blocking_input(&task->files,(int)frame->ebx)) {
        if (!scheduler_can_block()) {frame->eax=(u32)-ERROR_UNSUPPORTED;return frame;}
        task->wait_kind=TASK_WAIT_INPUT;task->pending_fd=(int)frame->ebx;
        task->pending_buffer=address;task->pending_bytes=bytes;
        return scheduler_block(frame);
    }
    frame->eax=(u32)result;return frame;
}
static int copy_string(struct task *task,u32 address,char *text,u32 capacity) {
    for (u32 i=0;i<capacity;i++) {
        if (address>~0u-i || !space_range(&task->space,address+i,1,0)) return -ERROR_BAD_ADDRESS;
        text[i]=*(const char *)(address+i);if (!text[i]) return 0;
    }
    return -ERROR_NAME_TOO_LONG;
}
static int copy_path(struct task *task,u32 address,char path[NOTOS_PATH_BYTES]) {
    char input[NOTOS_PATH_BYTES];
    int error = copy_string(task,address,input,NOTOS_PATH_BYTES);
    if (!error && !input[0]) return -ERROR_NOT_FOUND;
    return error ? error : path_resolve(task->cwd, input, path);
}
static struct interrupt_frame *network_call(struct task *task,struct interrupt_frame *frame) {
    u32 number=frame->eax;int result;
    if (number==SYS_SOCKET) {
        if (frame->ebx!=NOTOS_AF_INET) result=-ERROR_ADDRESS_FAMILY;
        else if (frame->ecx!=NOTOS_SOCK_STREAM || (frame->edx&~(NOTOS_O_NONBLOCK|NOTOS_O_CLOEXEC))) result=-ERROR_INVALID_ARGUMENT;
        else result=fd_socket_open(&task->files,frame->edx);
    } else if (number==SYS_RESOLVE) {
        char name[96];result=copy_string(task,frame->ebx,name,sizeof(name));
        if (!result) result=socket_resolve_start(&task->files,name);
    } else if (number==SYS_RESOLVE_RESULT) {
        u8 address[4];
        if (!space_range(&task->space,frame->ecx,sizeof(address),1)) result=-ERROR_BAD_ADDRESS;
        else {
            result=socket_resolve_result(&task->files,frame->ebx,address);
            if (!result) space_copy_to_user(&task->space,frame->ecx,address,sizeof(address));
        }
    } else {
        int handle=fd_socket_handle(&task->files,(int)frame->ebx);
        if (handle<0) result=handle;
        else if (number==SYS_CONNECT) {
            struct notos_socket_address address;
            if (!space_copy_from_user(&task->space,frame->ecx,&address,sizeof(address))) result=-ERROR_BAD_ADDRESS;
            else {
                result=socket_connect(handle,&address);
                if (result==-ERROR_IN_PROGRESS && !fd_nonblocking(&task->files,(int)frame->ebx))
                    return wait_network(task,frame,TASK_WAIT_CONNECT,handle,0);
            }
        } else if (number==SYS_SHUTDOWN) result=socket_shutdown(handle,frame->ecx);
        else if (number==SYS_SETOPT) result=socket_set_option(handle,frame->ecx,frame->edx);
        else if (number==SYS_GETOPT) {
            u32 value;
            if (!space_range(&task->space,frame->edx,sizeof(value),1)) result=-ERROR_BAD_ADDRESS;
            else {
                result=socket_get_option(handle,frame->ecx,&value);
                if (!result) space_copy_to_user(&task->space,frame->edx,&value,sizeof(value));
            }
        } else {
            struct notos_socket_address address;
            if (frame->edx>1) result=-ERROR_INVALID_ARGUMENT;
            else if (!space_range(&task->space,frame->ecx,sizeof(address),1)) result=-ERROR_BAD_ADDRESS;
            else {
                result=socket_get_address(handle,(int)frame->edx,&address);
                if (!result) space_copy_to_user(&task->space,frame->ecx,&address,sizeof(address));
            }
        }
    }
    frame->eax=(u32)result;return frame;
}
static void path_call(struct task *task,struct interrupt_frame *frame) {
    char path[NOTOS_PATH_BYTES];u32 number=frame->eax;
    int error=copy_path(task,frame->ebx,path);
    if (error) {frame->eax=(u32)error;return;}
    if (number==SYS_FD_OPEN) frame->eax=(u32)fd_open(&task->files,path,frame->ecx,frame->edx);
    else if (number==SYS_PATH_UNLINK) frame->eax=(u32)(starts(path,"/dev/")?-ERROR_READ_ONLY:vfs_unlink(path));
    else if (number == SYS_MKDIR) frame->eax = (u32)vfs_mkdir(path, frame->ecx);
    else if (number == SYS_RMDIR) frame->eax = (u32)vfs_rmdir(path);
    else if (number == SYS_CHDIR) {
        struct notos_file_info info;
        error = vfs_stat(path, &info);
        if (!error && info.kind != NOTOS_FILE_DIRECTORY) error = -ERROR_NOT_DIRECTORY;
        if (!error) for (u32 i = 0; (task->cwd[i] = path[i]); i++) {}
        frame->eax = (u32)error;
    } else {
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

static struct interrupt_frame *execute_image(struct task *task, struct interrupt_frame *frame) {
    char path[NOTOS_PATH_BYTES], arguments[ARGUMENT_MAX_BYTES + 1];
    int error = copy_path(task, frame->ebx, path);
    if (!error) error = copy_string(task, frame->ecx, arguments, sizeof(arguments));
    if (error) {frame->eax = (u32)error; return frame;}
    struct executable_image image;
    error = executable_open(path, &image);
    if (error) {frame->eax = (u32)error; return frame;}
    struct user_space replacement = {0};
    const char *name = path;
    for (const char *cursor = path; *cursor; cursor++) if (*cursor == '/') name = cursor + 1;
    struct elf_plan plan;
    if (!elf_validate(image.bytes, image.size, &plan)) error = -ERROR_BAD_EXECUTABLE;
    else if (!space_create_elf(&replacement, image.bytes, image.size)) error = -ERROR_MEMORY;
    else if (!space_arguments_split(&replacement, name, arguments)) error = -ERROR_INVALID_ARGUMENT;
    u32 budget = system_file_execution_budget(image.bytes);
    executable_close(&image);
    if (error) {
        space_destroy(&replacement);
        frame->eax = (u32)error;
        return frame;
    }
    if (budget > task->execution_budget) task->execution_budget = budget;
    process_rename(task->id, name);
    return task_exec(task, &replacement);
}
static void directory_call(struct task *task, struct interrupt_frame *frame) {
    if (frame->eax == SYS_GETCWD) {
        u32 bytes = length(task->cwd) + 1;
        if (frame->ecx < bytes) {frame->eax = (u32)-ERROR_NAME_TOO_LONG; return;}
        if (!space_range(&task->space, frame->ebx, frame->ecx, 1)) {frame->eax = (u32)-ERROR_BAD_ADDRESS; return;}
        space_copy_to_user(&task->space, frame->ebx, task->cwd, bytes);
        frame->eax = 0;
    } else {
        struct notos_directory_entry entry;
        if (!space_range(&task->space, frame->ecx, sizeof(entry), 1)) {frame->eax = (u32)-ERROR_BAD_ADDRESS; return;}
        int result = fd_readdir(&task->files, (int)frame->ebx, &entry);
        if (result > 0) space_copy_to_user(&task->space, frame->ecx, &entry, sizeof(entry));
        frame->eax = (u32)result;
    }
}
struct spawn_strings {
    char storage[NOTOS_SPAWN_STR_BYTES];
    u32 used;
    const char *argv[NOTOS_SPAWN_ARG_MAX];
    const char *envp[NOTOS_SPAWN_ENV_MAX];
    u32 argc;
    u32 envc;
};
static int copy_spawn_vector(struct task *task,u32 address,u32 limit,
    struct spawn_strings *strings,const char **out,u32 *count) {
    *count=0;
    for (u32 i=0;i<=limit;i++) {
        u32 slot_addr=address+i*sizeof(u32),ptr=0;
        if (slot_addr<address || !space_copy_from_user(&task->space,slot_addr,&ptr,sizeof(ptr)))
            return -ERROR_BAD_ADDRESS;
        if (!ptr) return 0;
        if (i==limit || strings->used>=sizeof(strings->storage)) return -ERROR_INVALID_ARGUMENT;
        int error=copy_string(task,ptr,strings->storage+strings->used,sizeof(strings->storage)-strings->used);
        if (error) return error==-ERROR_NAME_TOO_LONG?-ERROR_INVALID_ARGUMENT:error;
        out[i]=strings->storage+strings->used;
        strings->used+=length(out[i])+1;
        (*count)++;
    }
    return -ERROR_INVALID_ARGUMENT;
}
static void spawn_call(struct task *task,struct interrupt_frame *frame) {
    u32 map_count=frame->edi&0xffffu,flags=frame->edi>>16;
    if (flags || map_count>NOTOS_FD_MAP_MAX) {frame->eax=(u32)-ERROR_INVALID_ARGUMENT;return;}
    struct notos_fd_map maps[NOTOS_FD_MAP_MAX];
    if (map_count && !space_copy_from_user(&task->space,frame->esi,maps,map_count*sizeof(maps[0]))) {
        frame->eax=(u32)-ERROR_BAD_ADDRESS;return;
    }
    char path[NOTOS_PATH_BYTES];
    int error=copy_path(task,frame->ebx,path);
    if (error) {frame->eax=(u32)error;return;}
    if (!frame->ecx) {frame->eax=(u32)-ERROR_BAD_ADDRESS;return;}
    struct spawn_strings strings={0};
    error=copy_spawn_vector(task,frame->ecx,NOTOS_SPAWN_ARG_MAX,&strings,strings.argv,&strings.argc);
    if (!error && !strings.argc) error=-ERROR_INVALID_ARGUMENT;
    if (!error && frame->edx)
        error=copy_spawn_vector(task,frame->edx,NOTOS_SPAWN_ENV_MAX,&strings,strings.envp,&strings.envc);
    if (error) {frame->eax=(u32)error;return;}
    frame->eax=(u32)process_spawn_user(task,path,strings.argv,strings.argc,
        strings.envp,strings.envc,maps,map_count);
}
static struct interrupt_frame *wait_call(struct task *task,struct interrupt_frame *frame) {
    int target_pid=(int)frame->ebx;
    u32 status_addr=frame->ecx,flags=frame->edx,status=0;
    if (status_addr && !space_range(&task->space,status_addr,sizeof(status),1)) {
        frame->eax=(u32)-ERROR_BAD_ADDRESS;return frame;
    }
    int result=process_wait_try(task->id,target_pid,flags,&status);
    if (result==-ERROR_AGAIN) {
        if (!scheduler_can_block()) {frame->eax=(u32)-ERROR_UNSUPPORTED;return frame;}
        task->wait_kind=TASK_WAIT_CHILD;task->pending_fd=target_pid;
        task->pending_buffer=status_addr;task->pending_flags=flags;
        return scheduler_block(frame);
    }
    if (result>0 && status_addr) space_copy_to_user(&task->space,status_addr,&status,sizeof(status));
    frame->eax=(u32)result;return frame;
}
static void pipe_call(struct task *task,struct interrupt_frame *frame) {
    int fds[2];
    if (!space_range(&task->space,frame->ebx,sizeof(fds),1)) {frame->eax=(u32)-ERROR_BAD_ADDRESS;return;}
    int error=fd_pipe(&task->files,fds,frame->ecx);
    if (!error) space_copy_to_user(&task->space,frame->ebx,fds,sizeof(fds));
    frame->eax=(u32)error;
}
static struct interrupt_frame *kill_call(struct task *task,struct interrupt_frame *frame) {
    int pid=(int)frame->ebx,sig=(int)frame->ecx;
    if (sig<0 || sig>NOTOS_SIG_MAX) {frame->eax=(u32)-ERROR_INVALID_ARGUMENT;return frame;}
    if (pid<=0) {frame->eax=(u32)-ERROR_NO_PROCESS;return frame;}
    if ((u32)pid==task->id) {
        if (!sig) {frame->eax=0;return frame;}
        return task_finish(frame,128u+(u32)sig,TASK_EXIT_NORMAL);
    }
    frame->eax=(u32)process_kill((u32)pid,(u32)sig);
    return frame;
}

struct interrupt_frame *syscall_dispatch(struct interrupt_frame *frame) {
    struct task *task = task_current();
    if (!task || (frame->cs & 3) != 3) {
        frame->eax = (u32)-ERROR_PERMISSION;
        return frame;
    }
    task->statistics.syscalls++;
    switch (frame->eax) {
    case SYS_EXEC:return execute_image(task, frame);
    case SYS_SPAWN:spawn_call(task, frame);break;
    case SYS_WAIT:return wait_call(task, frame);
    case SYS_GETPPID:frame->eax = process_getppid(task->id);break;
    case SYS_PIPE:pipe_call(task, frame);break;
    case SYS_KILL:return kill_call(task, frame);
    case SYS_GETCWD:case SYS_READDIR:directory_call(task, frame);break;
    case SYS_POLL:return poll_wait_start(task,frame);
    case SYS_EXIT:
        return task_finish(frame, frame->ebx & 0xffu, TASK_EXIT_NORMAL);
    case SYS_YIELD:
        frame->eax = 0;
        return scheduler_yield(frame);
    case SYS_FD_OPEN:case SYS_PATH_STAT:case SYS_PATH_UNLINK:case SYS_MKDIR:case SYS_RMDIR:case SYS_CHDIR:
        path_call(task,frame);break;
    case SYS_SOCKET:case SYS_CONNECT:case SYS_GETOPT:case SYS_SETOPT:case SYS_SHUTDOWN:
    case SYS_ADDRESS:case SYS_RESOLVE:case SYS_RESOLVE_RESULT:return network_call(task,frame);
    case SYS_FD_READ:case SYS_RECV:return read_fd(task,frame);
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
    case SYS_FD_WRITE:case SYS_SEND:return write_fd(task,frame);
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
