#include "task.h"
#include "arch.h"
#include "clock.h"

static struct task *running_task;

int task_create(struct task *task, u32 id, const u8 *image, u32 bytes) {
    if (!task || !id) {
        return 0;
    }
    *task = (struct task){0};
    if (!space_create(&task->space, image, bytes)) {
        return 0;
    }
    task->id = id;
    task->state = TASK_READY;
    return 1;
}

int task_destroy(struct task *task) {
    if (!task || task == running_task || task->state == TASK_RUNNING) {
        return 0;
    }
    if (!space_destroy(&task->space)) {
        return 0;
    }
    task->state = TASK_UNUSED;
    return 1;
}

struct task *task_current(void) {
    return running_task;
}

int task_run_single(struct task *task) {
    if (!task || running_task || task->state != TASK_READY) {
        return 0;
    }
    task->state = TASK_RUNNING;
    task->started_ms = clock_ms();
    u32 first_tick = clock_ticks();
    running_task = task;
    user_enter(USER_BASE, USER_BASE + 16384 - 16, task->space.root);
    task->statistics.elapsed_ticks = clock_ticks() - first_tick;
    running_task = 0;
    return task->state == TASK_FINISHED;
}

void task_finish(u32 status, enum task_exit_reason reason) {
    struct task *task = running_task;
    task->result.status = status;
    task->result.reason = reason;
    task->state = TASK_FINISHED;
    running_task = 0;
    user_return();
}

void task_handle_fault(struct interrupt_frame *frame) {
    struct task *task = running_task;
    if (!task || (frame->cs & 3) != 3) {
        return;
    }
    /* These are system-fatal faults, not recoverable application failures. */
    if (frame->vector == 2 || frame->vector == 8 || frame->vector == 18) {
        return;
    }
    task->result.fault_vector = frame->vector;
    task->result.fault_error = frame->error;
    if (frame->vector == 14) {
        __asm__ volatile("mov %%cr2,%0" : "=r"(task->result.fault_address));
    }
    task_finish(128 + frame->vector, TASK_EXIT_FAULT);
}

void task_handle_timer(struct interrupt_frame *frame) {
    struct task *task = running_task;
    if (task && (frame->cs & 3) == 3 && clock_ms() - task->started_ms >= TASK_EXECUTION_BUDGET) {
        task_finish(124, TASK_EXIT_BUDGET);
    }
}
