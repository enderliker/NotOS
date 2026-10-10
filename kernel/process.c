/* Persistent process slots are owned by the event loop, never by an IRQ. */
#include "process.h"
#include "task.h"
#include "scheduler.h"
#include "user_test_abi.h"
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
    if (index == SCHEDULER_CAPACITY || !next_pid) return 0;
    struct process_slot *slot = &slots[index];
    if (!task_create(&slot->task, next_pid, code, bytes)) return 0;
    for (unsigned i = 0; (slot->name[i] = name[i]); ++i) {}
    for (unsigned i = 0; i < data_bytes; ++i) ((u8 *)slot->task.space.data)[i] = data[i];
    return next_pid++;
}
static void collect(struct process_slot *slot) {
    console_puts("PROCESS: pid="); console_uint(slot->task.id);
    console_puts(" exit="); console_uint(slot->task.result.status);
    console_puts(" ticks="); console_uint(slot->task.statistics.cpu_ticks);
    console_putc('\n');
    if (!task_destroy(&slot->task)) console_puts("PROCESS ERROR: resource cleanup failed\n");
    slot->name[0] = 0;
}
void process_poll(void) {
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
