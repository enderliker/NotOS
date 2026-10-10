#include "multitask.h"
#include "task.h"
#include "scheduler.h"
#include "physical.h"
#include "paging.h"
#include "user_test_abi.h"

extern const u8 user_blob_start[], user_blob_end[];

struct test_scenario {
    const char *name;
    unsigned count;
    unsigned modes[SCHEDULER_CAPACITY];
};
static const struct test_scenario scenarios[] = {
    {"workers", 2, {TEST_MODE_WORKER, TEST_MODE_WORKER}},
    {"four", 4, {TEST_MODE_WORKER, TEST_MODE_WORKER, TEST_MODE_WORKER, TEST_MODE_WORKER}},
    {"yield", 2, {TEST_MODE_YIELD, TEST_MODE_YIELD}},
    {"fault", 3, {TEST_MODE_WORKER, TEST_MODE_KERNEL_READ, TEST_MODE_WORKER}},
    {"budget", 2, {TEST_MODE_WORKER, TEST_MODE_SPIN}},
    {"fpu", 2, {TEST_MODE_FPU, TEST_MODE_WORKER}},
};

static const struct test_scenario *find_scenario(const char *name) {
    for (unsigned i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
        if (equal(name, scenarios[i].name)) return &scenarios[i];
    }
    return 0;
}

static void report_task(const struct task *task) {
    console_puts("TASK: id="); console_uint(task->id);
    console_puts(" exit="); console_uint(task->result.status);
    console_puts(" ticks="); console_uint(task->statistics.cpu_ticks);
    console_puts(" dispatches="); console_uint(task->statistics.dispatches);
    console_puts(" preemptions="); console_uint(task->statistics.preemptions);
    console_puts(" cr3="); console_uint(task->space.root);
    console_puts(" stack="); console_uint((u32)task->kernel_stack);
    console_puts(" counter="); console_uint(((const u32 *)task->space.data)[TEST_DATA_COUNTER]);
    console_putc('\n');
    if (task->result.reason == TASK_EXIT_FAULT) {
        console_puts("TASK FAULT: id="); console_uint(task->id);
        console_puts(" vector="); console_uint(task->result.fault_vector);
        console_puts(" error="); console_uint(task->result.fault_error);
        console_puts(" address="); console_uint(task->result.fault_address);
        console_putc('\n');
    }
}

static int valid_result(const struct task *task, unsigned mode) {
    const struct task_statistics *statistics = &task->statistics;
    const struct task_result *result = &task->result;
    if (task->state != TASK_FINISHED || !statistics->stack_valid
        || statistics->observed_cpl != 3 || statistics->observed_ss != 0x23
        || statistics->observed_root != task->space.root
        || !task_context_valid(task, task->context)) return 0;
    if (mode == TEST_MODE_KERNEL_READ) {
        return result->reason == TASK_EXIT_FAULT && result->status == 142
            && result->fault_vector == 14 && result->fault_error == 5
            && result->fault_address == 0x10000;
    }
    if (mode == TEST_MODE_FPU) {
        return result->reason == TASK_EXIT_FAULT && result->status == 135
            && result->fault_vector == 7 && result->fault_error == 0;
    }
    if (mode == TEST_MODE_SPIN) {
        return result->reason == TASK_EXIT_BUDGET && result->status == 124
            && statistics->cpu_ticks == TASK_EXECUTION_BUDGET;
    }
    const u32 *data = (const u32 *)task->space.data;
    return result->reason == TASK_EXIT_NORMAL && result->status == TEST_SUCCESS_STATUS
        && data[TEST_DATA_COUNTER] > 0 && data[TEST_DATA_RESULT] == TEST_SUCCESS_MARKER
        && statistics->dispatches >= 1;
}

static int valid_batch(const struct task tasks[], const struct test_scenario *scenario) {
    const struct scheduler_statistics *statistics = scheduler_statistics();
    if (equal(scenario->name, "yield")) {
        if (!statistics->yields) return 0;
    } else if (statistics->yields) return 0;
    for (unsigned i = 0; i < scenario->count; ++i) {
        if (!valid_result(&tasks[i], scenario->modes[i])) return 0;
        if (scenario->modes[i] == TEST_MODE_WORKER && scenario->count > 1
            && !equal(scenario->name, "fpu") && !tasks[i].statistics.preemptions) return 0;
        for (unsigned j = 0; j < i; ++j) {
            if (tasks[i].space.root == tasks[j].space.root
                || tasks[i].kernel_stack == tasks[j].kernel_stack) return 0;
        }
    }
    return 1;
}

static int rejected_invalid_batches(struct task *batch[], unsigned count) {
    if (scheduler_run(0, count) || scheduler_run(batch, 0)
        || scheduler_run(batch, SCHEDULER_CAPACITY + 1)) return 0;
    struct task *duplicate[] = {batch[0], batch[0]};
    if (scheduler_run(duplicate, 2)) return 0;
    u32 saved_id = batch[1]->id;
    batch[1]->id = batch[0]->id;
    int rejected_id = !scheduler_run(batch, count);
    batch[1]->id = saved_id;
    batch[0]->state = TASK_FINISHED;
    int rejected_state = !scheduler_run(batch, count);
    batch[0]->state = TASK_READY;
    return rejected_id && rejected_state;
}

void multitask_command(const char *name) {
    const struct test_scenario *scenario = find_scenario(name);
    if (!scenario) {
        console_puts("SCHED ERROR: choose workers/four/yield/fault/budget/fpu\n");
        return;
    }
    struct task tasks[SCHEDULER_CAPACITY] = {0};
    struct task *batch[SCHEDULER_CAPACITY];
    u32 frames_before = physical_available();
    u32 kernel_root = paging_current_root();
    u32 bytes = (u32)(user_blob_end - user_blob_start);
    int ok = 1;
    for (unsigned i = 0; i < scenario->count; ++i) {
        batch[i] = &tasks[i];
        if (!task_create(batch[i], i + 1, user_blob_start, bytes)) {
            ok = 0;
            break;
        }
        tasks[i].expected_marker = 0x11000000u + i;
        u32 *data = (u32 *)tasks[i].space.data;
        data[TEST_DATA_MODE] = scenario->modes[i];
        data[TEST_DATA_MARKER] = tasks[i].expected_marker;
    }
    if (ok) {
        ok = rejected_invalid_batches(batch, scenario->count)
            && scheduler_run(batch, scenario->count);
        if (ok) ok = valid_batch(tasks, scenario);
        for (unsigned i = 0; i < scenario->count; ++i) report_task(&tasks[i]);
        scheduler_report();
    }
    /* Includes partially-created batches: no active task may be destroyed. */
    for (unsigned i = 0; i < scenario->count; ++i) {
        if (!task_destroy(&tasks[i])) ok = 0;
    }
    if (physical_available() != frames_before || paging_current_root() != kernel_root) ok = 0;
    console_puts("SCHED: scenario="); console_puts(scenario->name); console_putc('\n');
    console_puts(ok ? "SCHED: preemptive multitask/contexts/cleanup PASS\n"
        : "SCHED ERROR: multitask test failed\n");
}
