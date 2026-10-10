#include "user.h"
#include "task.h"
#include "physical.h"
#include "paging.h"

extern const u8 user_blob_start[], user_blob_end[];
static const char *const scenarios[] = {
    "demo", "kernel-read", "code-write", "io", "guard",
    "gate", "bad-buffer", "foreign-read", "spin", "write"
};

static void report(const struct task *task, u32 kernel_root) {
    const struct task_statistics *statistics = &task->statistics;
    const struct task_result *result = &task->result;
    console_puts("USER: ring="); console_uint(statistics->observed_cpl);
    console_puts(" ss="); console_uint(statistics->observed_ss);
    console_puts(" syscalls="); console_uint(statistics->syscalls);
    console_puts(" exit="); console_uint(result->status);
    console_puts(" irq_delta="); console_uint(statistics->elapsed_ticks);
    console_putc('\n');
    console_puts("USER: cr3="); console_uint(statistics->observed_root);
    console_puts(" expected="); console_uint(task->space.root);
    console_puts(" kernel_restored="); console_uint(paging_current_root() == kernel_root);
    console_putc('\n');
    if (result->reason == TASK_EXIT_FAULT) {
        console_puts("USER FAULT: vector="); console_uint(result->fault_vector);
        console_puts(" error="); console_uint(result->fault_error);
        console_puts(" cr2="); console_uint(result->fault_address);
        console_puts(" cpl=3; task terminated\n");
    } else if (result->reason == TASK_EXIT_BUDGET) {
        console_puts("USER: budget exceeded; task terminated\n");
    }
}

static int expected_result(const struct task *task, unsigned mode, u32 root) {
    const struct task_result *result = &task->result;
    const struct task_statistics *statistics = &task->statistics;
    if (statistics->observed_cpl != 3 || statistics->observed_ss != 0x23
        || !statistics->stack_valid || statistics->observed_root != task->space.root
        || paging_current_root() != root || statistics->elapsed_ticks < 15) {
        return 0;
    }
    if (mode == 8) {
        return result->reason == TASK_EXIT_BUDGET && result->status == 124
            && statistics->elapsed_ticks >= TASK_EXECUTION_BUDGET - 2;
    }
    if (mode == 1 || mode == 2 || mode == 4 || mode == 7) {
        u32 error = mode == 2 ? 7 : mode == 4 ? 6 : 5;
        u32 address = mode == 1 ? 0x10000 : mode == 2 ? USER_BASE
            : mode == 4 ? USER_BASE + 8192 : ((const u32 *)task->space.data)[5];
        return result->reason == TASK_EXIT_FAULT && result->fault_vector == 14
            && result->fault_error == error && result->fault_address == address;
    }
    if (mode == 3 || mode == 5) {
        return result->reason == TASK_EXIT_FAULT && result->fault_vector == 13
            && result->fault_error == (mode == 5 ? 258u : 0u);
    }
    return result->reason == TASK_EXIT_NORMAL && result->status == 42
        && ((const u32 *)task->space.data)[2] == 0x12345678;
}

static int run(struct task *task, unsigned mode, u32 marker, u32 root) {
    task->expected_marker = marker;
    ((u32 *)task->space.data)[0] = mode;
    ((u32 *)task->space.data)[3] = marker;
    if (!task_run_single(task)) {
        return 0;
    }
    report(task, root);
    return expected_result(task, mode, root);
}

void user_command(const char *scenario) {
    unsigned mode = 0;
    const unsigned count = sizeof(scenarios) / sizeof(scenarios[0]);
    while (mode < count && !equal(scenario, scenarios[mode])) {
        mode++;
    }
    if (mode == count) {
        console_puts("USER ERROR: unknown scenario\n");
        return;
    }
    if (task_current()) {
        console_puts("USER ERROR: already running\n");
        return;
    }
    u32 free_before = physical_available();
    u32 root = paging_current_root();
    u32 bytes = (u32)(user_blob_end - user_blob_start);
    struct task first = {0}, second = {0};
    int ok = task_create(&first, 1, user_blob_start, bytes)
        && task_create(&second, 2, user_blob_start, bytes);
    if (ok) {
        ((u32 *)first.space.data)[5] = second.space.data;
        ok = first.space.root != second.space.root && first.space.data != second.space.data;
        ok = run(&first, mode, 0x11111111, root) && ok;
        if (((u32 *)second.space.data)[2] != 0) {
            ok = 0;
        }
        if (mode == 0) {
            ok = run(&second, 0, 0x22222222, root) && ok;
            if (((u32 *)first.space.data)[2] != 0x12345678) {
                ok = 0;
            }
        }
    }
    int first_destroyed = task_destroy(&first);
    int second_destroyed = task_destroy(&second);
    if (!first_destroyed || !second_destroyed || paging_current_root() != root
        || physical_available() != free_before) {
        ok = 0;
    }
    console_puts(ok ? "USER: private-CR3/TSS/syscall/cleanup PASS\n"
        : "USER ERROR: isolation scenario failed\n");
}
