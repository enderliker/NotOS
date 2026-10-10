#include "multitask.h"
#include "task.h"
#include "scheduler.h"
#include "physical.h"
#include "paging.h"

extern const u8 user_blob_start[], user_blob_end[];

static void report_task(const struct task *task) {
    console_puts("TASK: id="); console_uint(task->id);
    console_puts(" exit="); console_uint(task->result.status);
    console_puts(" ticks="); console_uint(task->statistics.cpu_ticks);
    console_puts(" dispatches="); console_uint(task->statistics.dispatches);
    console_puts(" preemptions="); console_uint(task->statistics.preemptions);
    console_puts(" cr3="); console_uint(task->space.root);
    console_puts(" stack="); console_uint((u32)task->kernel_stack);
    console_puts(" counter="); console_uint(((const u32 *)task->space.data)[6]);
    console_putc('\n');
}

void multitask_command(void) {
    struct task tasks[2] = {0};
    struct task *batch[2] = {&tasks[0], &tasks[1]};
    u32 frames_before = physical_available();
    u32 kernel_root = paging_current_root();
    u32 bytes = (u32)(user_blob_end - user_blob_start);
    int ok = 1;
    for (unsigned i = 0; i < 2; ++i) {
        if (!task_create(&tasks[i], i + 1, user_blob_start, bytes)) {
            ok = 0;
            break;
        }
        tasks[i].expected_marker = 0x11000000u + i;
        ((u32 *)tasks[i].space.data)[0] = 10; /* CPU worker: no yield calls. */
        ((u32 *)tasks[i].space.data)[3] = tasks[i].expected_marker;
    }
    if (ok) {
        ok = scheduler_run(batch, 2);
        const struct scheduler_statistics *statistics = scheduler_statistics();
        if (statistics->preemptions < 2 || tasks[0].space.root == tasks[1].space.root
            || tasks[0].kernel_stack == tasks[1].kernel_stack) {
            ok = 0;
        }
        for (unsigned i = 0; i < 2; ++i) {
            report_task(&tasks[i]);
            if (tasks[i].result.reason != TASK_EXIT_NORMAL || tasks[i].result.status != 42
                || tasks[i].statistics.preemptions == 0 || !tasks[i].statistics.stack_valid
                || tasks[i].statistics.observed_root != tasks[i].space.root
                || !task_context_valid(&tasks[i], tasks[i].context)
                || ((const u32 *)tasks[i].space.data)[6] == 0) {
                ok = 0;
            }
        }
        scheduler_report();
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (!task_destroy(&tasks[i])) ok = 0;
    }
    if (physical_available() != frames_before || paging_current_root() != kernel_root) ok = 0;
    console_puts(ok ? "SCHED: preemptive multitask/contexts/cleanup PASS\n"
        : "SCHED ERROR: multitask test failed\n");
}
