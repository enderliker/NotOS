/* Persistent process slots are owned by the event loop, never by an IRQ. */
#include "process.h"
#include "socket_stack.h"
#include "task.h"
#include "scheduler.h"
#include "user_test_abi.h"
#include "clock.h"
#include "wait_policy.h"
#include "syscall_numbers.h"
#define PROCESS_NAME_BYTES 24u
struct process_slot { struct task task; char name[PROCESS_NAME_BYTES]; };
static struct process_slot slots[SCHEDULER_CAPACITY];
static unsigned cursor;
static u32 next_pid = 1;
extern const u8 user_blob_start[], user_blob_end[];

u32 process_spawn_image(const char *name, const u8 *code, u32 bytes,
    const u8 *data, u32 data_bytes) {
    if (!name || length(name) >= PROCESS_NAME_BYTES || data_bytes > 4096
        || (data_bytes && !data)) return 0;
    unsigned index = 0;
    while (index < SCHEDULER_CAPACITY && slots[index].task.state != TASK_UNUSED) index++;
    if (index == SCHEDULER_CAPACITY || !next_pid || next_pid>0x7fffffffu) return 0;
    struct process_slot *slot = &slots[index];
    if (!task_create(&slot->task, next_pid, code, bytes)) return 0;
    for (unsigned i = 0; (slot->name[i] = name[i]); ++i) {}
    for (unsigned i = 0; i < data_bytes; ++i) ((u8 *)slot->task.space.data)[i] = data[i];
    return next_pid++;
}
u32 process_spawn_elf_args(const char *name,const u8 *image,u32 bytes,const char *argument) {
    if (!name || length(name)>=PROCESS_NAME_BYTES || !next_pid || next_pid>0x7fffffffu) return 0;
    unsigned index=0;
    while (index<SCHEDULER_CAPACITY && slots[index].task.state!=TASK_UNUSED) index++;
    if (index==SCHEDULER_CAPACITY || !task_create_elf(&slots[index].task,next_pid,image,bytes)) return 0;
    if (!space_arguments(&slots[index].task.space,name,argument)) {
        task_destroy(&slots[index].task);return 0;
    }
    /* Creation built a context before argc/argv existed; update only its saved ESP. */
    struct user_interrupt_tail *tail=(struct user_interrupt_tail *)(slots[index].task.context+1);
    tail->esp=slots[index].task.space.initial_stack;
    for (unsigned i=0;(slots[index].name[i]=name[i]);i++) {}
    return next_pid++;
}
u32 process_spawn_elf(const char *name,const u8 *image,u32 bytes) {
    return process_spawn_elf_args(name,image,bytes,0);
}
static void collect(struct process_slot *slot) {
    console_puts("PROCESS: pid="); console_uint(slot->task.id);
    console_puts(" exit="); console_uint(slot->task.result.status);
    console_puts(" ticks="); console_uint(slot->task.statistics.cpu_ticks);
    console_putc('\n');
    if (slot->task.result.reason==TASK_EXIT_FAULT) {
        console_puts("PROCESS FAULT: vector=");console_uint(slot->task.result.fault_vector);
        console_puts(" error=");console_uint(slot->task.result.fault_error);
        console_puts(" cr2=");console_uint(slot->task.result.fault_address);console_putc('\n');
    }
    if (!task_destroy(&slot->task)) console_puts("PROCESS ERROR: resource cleanup failed\n");
    slot->name[0] = 0;
}
void process_poll(void) {
    u32 now=clock_ms();
    for (unsigned i=0;i<SCHEDULER_CAPACITY;i++) {
        struct task *task=&slots[i].task;
        if (task->state!=TASK_BLOCKED) continue;
        if (task->wait_kind==TASK_WAIT_TIMER) {
            if (wait_due(task->wait_started,task->wait_duration,now)) {
                task->wait_duration=0;task->state=TASK_READY;
            }
        } else if (task->wait_kind!=TASK_WAIT_INPUT) {
            int handle=fd_socket_handle(&task->files,task->pending_fd),result;
            int expired=task->wait_duration && wait_due(task->wait_started,task->wait_duration,now);
            u8 buffer[NOTOS_IO_LIMIT];u32 bytes=task->pending_bytes;
            if (bytes>sizeof(buffer)) bytes=sizeof(buffer);
            if (handle<0) result=handle;
            else if (expired) {
                result=task->wait_kind==TASK_WAIT_CONNECT?-ERROR_TIMED_OUT:-ERROR_AGAIN;
                if (task->wait_kind==TASK_WAIT_CONNECT) socket_connect_timeout(handle);
            } else if (task->wait_kind==TASK_WAIT_CONNECT) result=socket_connected(handle);
            else if (task->wait_kind==TASK_WAIT_SEND) {
                result=space_copy_from_user(&task->space,task->pending_buffer,buffer,bytes)?
                    socket_write(handle,buffer,bytes):-ERROR_BAD_ADDRESS;
            } else {
                result=socket_read(handle,buffer,bytes,task->pending_flags);
                if (result>0 && !space_copy_to_user(&task->space,task->pending_buffer,buffer,(u32)result)) result=-ERROR_BAD_ADDRESS;
            }
            if (!expired && (result==-ERROR_AGAIN || result==-ERROR_IN_PROGRESS)) continue;
            task->context->eax=(u32)result;task->state=TASK_READY;
            task->pending_buffer=task->pending_bytes=task->pending_flags=0;
        } else {
            u8 buffer[NOTOS_INPUT_BYTES];
            u32 bytes=task->pending_bytes;
            if (bytes>sizeof(buffer)) bytes=sizeof(buffer);
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
    if (equal(command, "ps")) {
        for (unsigned i = 0; i < SCHEDULER_CAPACITY; ++i) if (slots[i].task.state != TASK_UNUSED) {
            console_puts("PROCESS: pid="); console_uint(slots[i].task.id);
            console_puts(" name="); console_puts(slots[i].name);
            console_puts(" ticks="); console_uint(slots[i].task.statistics.cpu_ticks); console_putc('\n');
            if (slots[i].task.state==TASK_BLOCKED) {
                console_puts("PROCESS WAIT: pid=");console_uint(slots[i].task.id);
                if (slots[i].task.wait_kind==TASK_WAIT_INPUT) console_puts(" stdin=1");
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
        for (unsigned i = 0; i < SCHEDULER_CAPACITY; ++i) {
            if (slots[i].task.state != TASK_UNUSED && slots[i].task.id == pid) {
                slots[i].task.result.status = 143; slots[i].task.state = TASK_FINISHED;
                collect(&slots[i]); return;
            }
        }
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
