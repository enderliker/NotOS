/* Persistent process slots are owned by the event loop, never by an IRQ. */
#include "process.h"
#include "executable.h"
#include "elf.h"
#include "system_file.h"
#include "poll_wait.h"
#include "socket_stack.h"
#include "task.h"
#include "scheduler.h"
#include "user_test_abi.h"
#include "clock.h"
#include "wait_policy.h"
#include "syscall_numbers.h"
#define PROCESS_NAME_BYTES 24u
struct process_slot {
    struct task task;
    char name[PROCESS_NAME_BYTES];
    int quiet;
    u32 parent_id;
    u32 zombie_id;
    u32 zombie_status;
};
static struct process_slot slots[SCHEDULER_CAPACITY];
static unsigned cursor;
static int trace_enabled;
static u32 next_pid = 1;
static u32 foreground_pid;
static int foreground_reprompt;
extern const u8 user_blob_start[], user_blob_end[];

static int slot_unused(const struct process_slot *slot) {
    return slot->task.state == TASK_UNUSED && !slot->zombie_id;
}
u32 process_spawn_image(const char *name, const u8 *code, u32 bytes,
    const u8 *data, u32 data_bytes) {
    if (!name || !*name || length(name) >= PROCESS_NAME_BYTES || data_bytes > 4096
        || (data_bytes && !data)) return 0;
    unsigned index = 0;
    while (index < SCHEDULER_CAPACITY && !slot_unused(&slots[index])) index++;
    if (index == SCHEDULER_CAPACITY || !next_pid || next_pid>0x7fffffffu) return 0;
    struct process_slot *slot = &slots[index];
    slot->quiet = 0;
    slot->parent_id = slot->zombie_id = slot->zombie_status = 0;
    if (!task_create(&slot->task, next_pid, code, bytes)) return 0;
    for (unsigned i = 0; (slot->name[i] = name[i]); ++i) {}
    for (unsigned i = 0; i < data_bytes; ++i) ((u8 *)slot->task.space.data)[i] = data[i];
    return next_pid++;
}
static u32 process_spawn_elf_arguments(const char *name,const u8 *image,u32 bytes,
    const char *argument,int split) {
    if (!name || length(name)>=PROCESS_NAME_BYTES || !next_pid || next_pid>0x7fffffffu) return 0;
    unsigned index=0;
    while (index<SCHEDULER_CAPACITY && !slot_unused(&slots[index])) index++;
    if (index==SCHEDULER_CAPACITY || !task_create_elf(&slots[index].task,next_pid,image,bytes)) return 0;
    slots[index].quiet = 0;
    slots[index].parent_id = slots[index].zombie_id = slots[index].zombie_status = 0;
    slots[index].task.execution_budget = system_file_execution_budget(image);
    struct user_space *space=&slots[index].task.space;
    int prepared=split?space_arguments_split(space,name,argument):space_arguments(space,name,argument);
    if (!prepared) {
        task_destroy(&slots[index].task);return 0;
    }
    /* Creation built a context before argc/argv existed; update only its saved ESP. */
    struct user_interrupt_tail *tail=(struct user_interrupt_tail *)(slots[index].task.context+1);
    tail->esp=slots[index].task.space.initial_stack;
    for (unsigned i=0;(slots[index].name[i]=name[i]);i++) {}
    return next_pid++;
}
u32 process_spawn_elf_args(const char *name,const u8 *image,u32 bytes,const char *argument) {
    return process_spawn_elf_arguments(name,image,bytes,argument,0);
}
u32 process_spawn_elf_split(const char *name,const u8 *image,u32 bytes,const char *arguments) {
    return process_spawn_elf_arguments(name,image,bytes,arguments,1);
}
u32 process_spawn_elf(const char *name,const u8 *image,u32 bytes) {
    return process_spawn_elf_args(name,image,bytes,0);
}
int process_spawn_user(struct task *parent,const char *path,const char *const *argv,u32 argc,
    const char *const *envp,u32 envc,const struct notos_fd_map *maps,u32 map_count) {
    if (!parent || !path || !*path || !argv || !argc) return -ERROR_INVALID_ARGUMENT;
    const char *name=path;
    for (const char *p=path;*p;p++) if (*p=='/') name=p+1;
    if (!*name || length(name)>=PROCESS_NAME_BYTES) return -ERROR_NAME_TOO_LONG;
    if (argv[0]) {
        const char *arg0=argv[0];
        for (const char *p=argv[0];*p;p++) if (*p=='/') arg0=p+1;
        if (*arg0 && length(arg0)<PROCESS_NAME_BYTES) name=arg0;
    }
    struct executable_image image;
    int error=executable_open(path,&image);
    if (error) return error;
    struct elf_plan plan;
    if (!elf_validate(image.bytes,image.size,&plan)) {
        executable_close(&image);return -ERROR_BAD_EXECUTABLE;
    }
    unsigned index=0;
    while (index<SCHEDULER_CAPACITY && !slot_unused(&slots[index])) index++;
    if (index==SCHEDULER_CAPACITY || !next_pid || next_pid>0x7fffffffu) {
        executable_close(&image);return -ERROR_AGAIN;
    }
    struct process_slot *slot=&slots[index];
    if (!task_create_elf(&slot->task,next_pid,image.bytes,image.size)) {
        executable_close(&image);return -ERROR_MEMORY;
    }
    slot->task.execution_budget=system_file_execution_budget(image.bytes);
    executable_close(&image);
    error=fd_table_spawn(&slot->task.files,&parent->files,maps,map_count);
    if (!error && !space_stack_build(&slot->task.space,argv,argc,envp,envc)) error=-ERROR_INVALID_ARGUMENT;
    if (error) {
        task_destroy(&slot->task);return error;
    }
    struct user_interrupt_tail *tail=(struct user_interrupt_tail *)(slot->task.context+1);
    tail->esp=slot->task.space.initial_stack;
    for (unsigned i=0;(slot->task.cwd[i]=parent->cwd[i]);i++) {}
    for (unsigned i=0;(slot->name[i]=name[i]);i++) {}
    slot->quiet=1;
    slot->parent_id=parent->id;
    slot->zombie_id=0;
    slot->zombie_status=0;
    return (int)next_pid++;
}
int process_wait_try(u32 parent_id,int target_pid,u32 flags,u32 *status_out) {
    if ((flags&~NOTOS_WNOHANG) || target_pid==0 || target_pid<-1) return -ERROR_INVALID_ARGUMENT;
    int live_match=0;
    for (unsigned i=0;i<SCHEDULER_CAPACITY;i++) {
        if (slots[i].parent_id!=parent_id) continue;
        if (slots[i].zombie_id && (target_pid==-1 || slots[i].zombie_id==(u32)target_pid)) {
            u32 reaped=slots[i].zombie_id;
            if (status_out) *status_out=slots[i].zombie_status;
            slots[i].name[0]=0;
            slots[i].parent_id=slots[i].zombie_id=slots[i].zombie_status=0;
            return (int)reaped;
        }
        if (slots[i].task.state!=TASK_UNUSED && (target_pid==-1 || slots[i].task.id==(u32)target_pid))
            live_match=1;
    }
    if (!live_match) return -ERROR_CHILD;
    return (flags&NOTOS_WNOHANG)?0:-ERROR_AGAIN;
}
u32 process_getppid(u32 pid) {
    for (unsigned i=0;i<SCHEDULER_CAPACITY;i++) {
        if (slots[i].task.state!=TASK_UNUSED && slots[i].task.id==pid) {
            u32 parent=slots[i].parent_id;
            if (!parent) return 0;
            for (unsigned j=0;j<SCHEDULER_CAPACITY;j++)
                if (slots[j].task.state!=TASK_UNUSED && slots[j].task.id==parent) return parent;
            return 0;
        }
    }
    return 0;
}
void process_quiet(u32 pid) {
    for (unsigned i = 0; i < SCHEDULER_CAPACITY; i++) {
        if (slots[i].task.state != TASK_UNUSED && slots[i].task.id == pid) {
            slots[i].quiet = 1;
            return;
        }
    }
}
void process_set_foreground(u32 pid,int reprompt) {
    foreground_pid=pid;foreground_reprompt=reprompt;
}
int process_foreground_active(void) {
    if (!foreground_pid) return 0;
    for (unsigned i=0;i<SCHEDULER_CAPACITY;i++)
        if (slots[i].task.state!=TASK_UNUSED && slots[i].task.id==foreground_pid) return 1;
    foreground_pid=0;foreground_reprompt=0;
    return 0;
}
void process_foreground_input(u8 byte) {
    if (!foreground_pid) return;
    for (unsigned i=0;i<SCHEDULER_CAPACITY;i++) {
        if (slots[i].task.state!=TASK_UNUSED && slots[i].task.id==foreground_pid) {
            slots[i].task.files.input_eof=0;
            fd_feed(&slots[i].task.files,&byte,1,0);
            return;
        }
    }
    foreground_pid=0;foreground_reprompt=0;
}
void process_rename(u32 pid, const char *name) {
    if (!name || !*name || length(name) >= PROCESS_NAME_BYTES) return;
    for (unsigned i = 0; i < SCHEDULER_CAPACITY; i++) {
        if (slots[i].task.state != TASK_UNUSED && slots[i].task.id == pid) {
            for (unsigned j = 0; (slots[i].name[j] = name[j]); j++) {}
            return;
        }
    }
}
int process_trace_enabled(void) {
    return trace_enabled;
}
static void collect(struct process_slot *slot) {
    u32 id = slot->task.id, status = slot->task.result.status, parent = slot->parent_id;
    int reprompt = (id == foreground_pid && foreground_reprompt);
    if (id == foreground_pid) {
        foreground_pid = 0;
        foreground_reprompt = 0;
    }
    if (!slot->quiet || trace_enabled) {
        console_puts("PROCESS: pid="); console_uint(id);
        console_puts(" exit="); console_uint(status);
        console_puts(" ticks="); console_uint(slot->task.statistics.cpu_ticks);
        console_putc('\n');
    }
    if (slot->task.result.reason==TASK_EXIT_FAULT) {
        console_puts("PROCESS FAULT: vector=");console_uint(slot->task.result.fault_vector);
        console_puts(" error=");console_uint(slot->task.result.fault_error);
        console_puts(" cr2=");console_uint(slot->task.result.fault_address);console_putc('\n');
    }
    if (!task_destroy(&slot->task)) console_puts("PROCESS ERROR: resource cleanup failed\n");
    /* Orphan live children and immediately reap any uncollected zombie children. */
    for (unsigned i = 0; i < SCHEDULER_CAPACITY; i++) {
        if (slots[i].parent_id == id) {
            if (slots[i].zombie_id) slots[i].name[0] = 0;
            slots[i].parent_id = 0;
            slots[i].zombie_id = 0;
            slots[i].zombie_status = 0;
        }
    }
    int parent_alive = 0;
    if (parent) {
        for (unsigned i = 0; i < SCHEDULER_CAPACITY; i++)
            if (slots[i].task.state != TASK_UNUSED && slots[i].task.id == parent) parent_alive = 1;
    }
    if (parent_alive) {
        slot->zombie_id = id;
        slot->zombie_status = status;
    } else {
        slot->name[0] = 0;
        slot->parent_id = 0;
        slot->zombie_id = 0;
        slot->zombie_status = 0;
    }
    if (reprompt) console_puts("notos> ");
}
int process_kill(u32 pid,u32 sig) {
    if (sig>NOTOS_SIG_MAX) return -ERROR_INVALID_ARGUMENT;
    if (!pid) return -ERROR_NO_PROCESS;
    for (unsigned i=0;i<SCHEDULER_CAPACITY;i++) {
        if (slots[i].task.state!=TASK_UNUSED && slots[i].task.id==pid) {
            if (!sig) return 0;
            if (&slots[i].task==task_current()) return -ERROR_INVALID_ARGUMENT;
            slots[i].task.result.status=128u+sig;
            slots[i].task.result.reason=TASK_EXIT_NORMAL;
            slots[i].task.state=TASK_FINISHED;
            collect(&slots[i]);
            return 0;
        }
    }
    return -ERROR_NO_PROCESS;
}
u32 process_list(struct notos_process_info *entries,u32 capacity) {
    u32 count=0;
    for (unsigned i=0;i<SCHEDULER_CAPACITY;i++) {
        const struct process_slot *slot=&slots[i];
        if (slot_unused(slot)) continue;
        if (entries && count<capacity) {
            struct notos_process_info info={0};
            if (slot->task.state==TASK_UNUSED) {
                info.pid=slot->zombie_id;
                info.ppid=slot->parent_id;
                info.state=NOTOS_PROCESS_STATE_ZOMBIE;
            } else {
                info.pid=slot->task.id;
                info.ppid=process_getppid(slot->task.id);
                info.state=(&slot->task==task_current())?NOTOS_PROCESS_STATE_RUNNING:
                    slot->task.state==TASK_BLOCKED?NOTOS_PROCESS_STATE_BLOCKED:NOTOS_PROCESS_STATE_READY;
                if (slot->task.state==TASK_BLOCKED) {
                    info.wait_kind=slot->task.wait_kind==TASK_WAIT_INPUT?NOTOS_PROCESS_WAIT_INPUT:
                        slot->task.wait_kind==TASK_WAIT_OUTPUT?NOTOS_PROCESS_WAIT_OUTPUT:
                        slot->task.wait_kind==TASK_WAIT_CHILD?NOTOS_PROCESS_WAIT_CHILD:
                        slot->task.wait_kind==TASK_WAIT_POLL?NOTOS_PROCESS_WAIT_POLL:
                        slot->task.wait_kind==TASK_WAIT_TIMER?NOTOS_PROCESS_WAIT_TIMER:NOTOS_PROCESS_WAIT_SOCKET;
                }
                info.cpu_ticks=slot->task.statistics.cpu_ticks;
            }
            for (unsigned j=0;j<NOTOS_PROCESS_NAME_BYTES && slot->name[j];j++) info.name[j]=slot->name[j];
            entries[count]=info;
        }
        count++;
    }
    return count;
}
void process_poll(void) {
    u32 now=clock_ms();
    u8 buffer[NOTOS_IO_LIMIT];
    for (unsigned i=0;i<SCHEDULER_CAPACITY;i++) {
        struct task *task=&slots[i].task;
        if (task->state!=TASK_BLOCKED) continue;
        if (task->wait_kind==TASK_WAIT_TIMER) {
            if (wait_due(task->wait_started,task->wait_duration,now)) {
                task->wait_duration=0;task->state=TASK_READY;
            }
        } else if (task->wait_kind==TASK_WAIT_POLL) {
            poll_wait_resume(task,now);
        } else if (task->wait_kind==TASK_WAIT_CHILD) {
            u32 status=0;
            int result=process_wait_try(task->id,task->pending_fd,task->pending_flags,&status);
            if (result==-ERROR_AGAIN) continue;
            if (result>0 && task->pending_buffer
                && !space_copy_to_user(&task->space,task->pending_buffer,&status,sizeof(status)))
                result=-ERROR_BAD_ADDRESS;
            task->context->eax=(u32)result;task->state=TASK_READY;
            task->pending_buffer=task->pending_flags=0;task->pending_fd=0;
        } else if (task->wait_kind==TASK_WAIT_OUTPUT) {
            u32 bytes=task->pending_bytes;
            if (bytes>sizeof(buffer)) bytes=sizeof(buffer);
            int result=space_copy_from_user(&task->space,task->pending_buffer,buffer,bytes)?
                fd_write(&task->files,task->pending_fd,buffer,bytes):-ERROR_BAD_ADDRESS;
            if (result==-ERROR_AGAIN) continue;
            task->context->eax=(u32)result;task->state=TASK_READY;
            task->pending_buffer=task->pending_bytes=0;
        } else if (task->wait_kind!=TASK_WAIT_INPUT) {
            int handle=fd_socket_handle(&task->files,task->pending_fd),result;
            int expired=task->wait_duration && wait_due(task->wait_started,task->wait_duration,now);
            u32 bytes=task->pending_bytes;
            if (bytes>sizeof(buffer)) bytes=sizeof(buffer);
            if (handle<0) result=handle;
            else if (expired) {
                result=task->wait_kind==TASK_WAIT_CONNECT?-ERROR_TIMED_OUT:-ERROR_AGAIN;
                if (task->wait_kind==TASK_WAIT_CONNECT) socket_connect_timeout(handle);
            } else if (task->wait_kind==TASK_WAIT_CONNECT) result=socket_connected(handle);
            else if (task->wait_kind==TASK_WAIT_SEND) {
                if (!(socket_ready(handle) & (NOTOS_POLLOUT | NOTOS_POLLERR | NOTOS_POLLHUP))) result = -ERROR_AGAIN;
                else result=space_copy_from_user(&task->space,task->pending_buffer,buffer,bytes)?
                    socket_write(handle,buffer,bytes):-ERROR_BAD_ADDRESS;
            } else {
                result=socket_read(handle,buffer,bytes,task->pending_flags);
                if (result>0 && !space_copy_to_user(&task->space,task->pending_buffer,buffer,(u32)result)) result=-ERROR_BAD_ADDRESS;
            }
            if (!expired && (result==-ERROR_AGAIN || result==-ERROR_IN_PROGRESS)) continue;
            task->context->eax=(u32)result;task->state=TASK_READY;
            task->pending_buffer=task->pending_bytes=task->pending_flags=0;
        } else {
            u32 bytes=task->pending_bytes;
            if (bytes>sizeof(buffer)) bytes=sizeof(buffer);
            if (!task->files.input_count && !task->files.input_eof
                && fd_is_tty(&task->files,task->pending_fd)==1 && slots[i].parent_id) {
                u32 parent_id=slots[i].parent_id;
                for (unsigned depth=0;depth<SCHEDULER_CAPACITY && parent_id;depth++) {
                    u32 next_parent=0;
                    for (unsigned j=0;j<SCHEDULER_CAPACITY;j++) {
                        if (slots[j].task.state!=TASK_UNUSED && slots[j].task.id==parent_id) {
                            if (slots[j].task.files.input_count || slots[j].task.files.input_eof)
                                fd_forward_tty(&slots[j].task.files,&task->files,bytes);
                            next_parent=slots[j].parent_id;
                            break;
                        }
                    }
                    if (task->files.input_count || task->files.input_eof) break;
                    parent_id=next_parent;
                }
            }
            int result=fd_read(&task->files,task->pending_fd,buffer,bytes);
            if (result==-ERROR_AGAIN) continue;
            if (result>0 && !space_copy_to_user(&task->space,task->pending_buffer,buffer,(u32)result))
                result=-ERROR_BAD_ADDRESS;
            task->context->eax=(u32)result;task->state=TASK_READY;
            task->pending_buffer=task->pending_bytes=0;
        }
    }
    for (unsigned offset = 0; offset < SCHEDULER_CAPACITY; ++offset) {
        unsigned index = (cursor + offset) % SCHEDULER_CAPACITY;
        struct task *task = &slots[index].task;
        if (task->state != TASK_READY) continue;
        cursor = (index + 1) % SCHEDULER_CAPACITY;
        if (!scheduler_pulse(task)) console_puts("PROCESS ERROR: resume failed\n");
        if (task->state == TASK_FINISHED) collect(&slots[index]);
        return; /* Bounded user quantum between event-loop iterations. */
    }
}
void process_command(const char *command, const char *argument) {
    if (equal(command, "process-trace")) {
        if (equal(argument, "on")) trace_enabled = 1;
        else if (equal(argument, "off")) trace_enabled = 0;
        else {
            console_puts("PROCESS ERROR: use process-trace on/off\n");
            return;
        }
        console_puts(trace_enabled ? "PROCESS: trace on\n" : "PROCESS: trace off\n");
        return;
    }
    if (equal(command, "ps")) {
        for (unsigned i = 0; i < SCHEDULER_CAPACITY; ++i) if (slots[i].task.state != TASK_UNUSED) {
            console_puts("PROCESS: pid="); console_uint(slots[i].task.id);
            console_puts(" name="); console_puts(slots[i].name);
            console_puts(" ticks="); console_uint(slots[i].task.statistics.cpu_ticks); console_putc('\n');
            if (slots[i].task.state==TASK_BLOCKED) {
                console_puts("PROCESS WAIT: pid=");console_uint(slots[i].task.id);
                if (slots[i].task.wait_kind==TASK_WAIT_INPUT) console_puts(" stdin=1");
                else if (slots[i].task.wait_kind==TASK_WAIT_OUTPUT) console_puts(" pipe=1");
                else if (slots[i].task.wait_kind==TASK_WAIT_CHILD) console_puts(" child=1");
                else if (slots[i].task.wait_kind==TASK_WAIT_POLL) console_puts(" poll=1");
                else if (slots[i].task.wait_kind!=TASK_WAIT_TIMER) console_puts(" socket=1");
                else {console_puts(" timeout_ms=");console_uint(slots[i].task.wait_duration);}console_putc('\n');
            }
        }
        console_puts("PROCESS: list complete\n"); return;
    }
    if (equal(command, "kill")) {
        u32 pid = 0;
        if (!*argument) {console_puts("PROCESS ERROR: invalid pid\n");return;}
        for (const char *p = argument; *p; ++p) {
            if (*p < '0' || *p > '9' || pid > (0xffffffffu - (u32)(*p-'0')) / 10u) {
                console_puts("PROCESS ERROR: invalid pid\n"); return;
            }
            pid = pid * 10 + (u32)(*p-'0');
        }
        if (!process_kill(pid, 15)) return;
        console_puts("PROCESS ERROR: pid not found\n"); return;
    }
    unsigned mode = equal(argument, "worker") ? TEST_MODE_WORKER
        : equal(argument, "spin") ? TEST_MODE_SPIN
        : equal(argument, "fault") ? TEST_MODE_KERNEL_READ : 0;
    if (!mode) {console_puts("PROCESS ERROR: choose worker/spin/fault\n");return;}
    u32 data[7] = {0}; data[TEST_DATA_MODE] = mode;
    data[TEST_DATA_MARKER] = 0x11000000;
    u32 pid = process_spawn_image(argument, user_blob_start,
        (u32)(user_blob_end-user_blob_start), (const u8 *)data, sizeof(data));
    if (!pid) {console_puts("PROCESS ERROR: no slot or memory\n");return;}
    /* Bundled diagnostic probe expects this marker. Ordinary apps do not need it. */
    for (unsigned i = 0; i < SCHEDULER_CAPACITY; ++i)
        if (slots[i].task.id == pid) slots[i].task.expected_marker = data[TEST_DATA_MARKER];
    console_puts("PROCESS: started pid="); console_uint(pid); console_putc('\n');
}

/* Shell explicitly routes input to one process; it never steals another task's stdin. */
void process_input_command(const char *argument,int eof) {
    u32 pid=0;const char *s=argument;
    while (*s>='0' && *s<='9') {
        u32 digit=(u32)(*s++-'0');
        if (pid>(0x7fffffffu-digit)/10u) {console_puts("INPUT ERROR: invalid pid\n");return;}
        pid=pid*10+digit;
    }
    if (!pid || s==argument || (eof?*s!=0:*s!=' ')) {console_puts("INPUT ERROR: use feed PID TEXT / feed-eof PID\n");return;}
    for (u32 i=0;i<SCHEDULER_CAPACITY;i++) if (slots[i].task.id==pid
        && slots[i].task.state!=TASK_UNUSED && slots[i].task.state!=TASK_FINISHED) {
        u8 text[NOTOS_INPUT_BYTES];u32 bytes=0;
        if (!eof) {
            s++;bytes=length(s);
            if (bytes>=sizeof(text)) {console_puts("INPUT ERROR: queue full\n");return;}
            for (u32 j=0;j<bytes;j++) text[j]=(u8)s[j];
            text[bytes++]='\n';
        }
        if (fd_feed(&slots[i].task.files,text,bytes,eof)) console_puts("INPUT ERROR: queue full or EOF\n");
        else console_puts("INPUT: delivered\n");
        return;
    }
    console_puts("INPUT ERROR: pid not found\n");
}
