#include "scheduler.h"
#include "arch.h"
#include "clock.h"
#include "paging.h"

#define TRACE_CAPACITY 32u
struct schedule_event {
    u32 from_id;
    u32 to_id;
    enum schedule_cause cause;
};

static struct {
    struct task *tasks[SCHEDULER_CAPACITY];
    unsigned count;
    unsigned current;
    unsigned slice_ticks;
    int active;
    int pulse;
    struct scheduler_statistics statistics;
    struct schedule_event trace[TRACE_CAPACITY];
    unsigned trace_count;
} runtime;

static void invariant_failed(void) __attribute__((noreturn));
static void invariant_failed(void) {
    __asm__ volatile("cli" ::: "memory");
    console_puts("\nSCHED PANIC: invalid kernel context\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

struct task *scheduler_current(void) {
    return runtime.active && runtime.current < runtime.count
        ? runtime.tasks[runtime.current] : 0;
}

static unsigned choose(unsigned previous) {
    enum task_state states[SCHEDULER_CAPACITY];
    for (unsigned i = 0; i < runtime.count; ++i) {
        states[i] = runtime.tasks[i]->state;
    }
    return round_robin_next(states, runtime.count, previous);
}

static void record(unsigned previous, unsigned next, enum schedule_cause cause) {
    if (runtime.trace_count == TRACE_CAPACITY) {
        return;
    }
    struct schedule_event *event = &runtime.trace[runtime.trace_count++];
    event->from_id = previous < runtime.count ? runtime.tasks[previous]->id : 0;
    event->to_id = next < runtime.count ? runtime.tasks[next]->id : 0;
    event->cause = cause;
}

static struct interrupt_frame *activate(unsigned next, unsigned previous, enum schedule_cause cause) {
    struct task *task = runtime.tasks[next];
    if (!task_context_valid(task, task->context)) {
        invariant_failed();
    }
    record(previous, next, cause);
    if (previous < runtime.count && previous != next) {
        runtime.statistics.switches++;
        if (cause == SCHEDULE_TIMER) {
            runtime.statistics.preemptions++;
            runtime.tasks[previous]->statistics.preemptions++;
        }
    }
    if (!task->statistics.dispatches) {
        task->first_tick = clock_ticks();
    }
    task->statistics.dispatches++;
    task->state = TASK_RUNNING;
    runtime.current = next;
    runtime.slice_ticks = 0;
    /* Never permit a restored user frame to enable IOPL, NT or VM. */
    task->context->eflags = (task->context->eflags & ~(0x3000u | 0x4000u | 0x20000u)) | 0x202u;
    arch_select_kernel_stack((u32)task->kernel_stack,
        (u32)task->kernel_stack + TASK_KERNEL_STACK_BYTES);
    u32 control;
    __asm__ volatile("mov %%cr0,%0" : "=r"(control));
    control |= 8u; /* TS: unsupported FPU use traps rather than leaking cross-task state. */
    __asm__ volatile("mov %0,%%cr0" :: "r"(control) : "memory");
    __asm__ volatile("mov %0,%%cr3" :: "r"(task->space.root) : "memory");
    return task->context;
}

static struct interrupt_frame *handoff(struct interrupt_frame *frame, enum schedule_cause cause) {
    struct task *task = scheduler_current();
    if (!task_context_valid(task, frame) || paging_current_root() != task->space.root) {
        invariant_failed();
    }
    task->context = frame;
    unsigned previous = runtime.current;
    if (runtime.pulse) {
        record(previous, SCHEDULER_NO_TASK, cause);
        runtime.active = 0;
        arch_reset_kernel_stack();
        arch_return_to_caller();
    }
    unsigned next = choose(previous);
    if (next == SCHEDULER_NO_TASK) {
        record(previous, next, cause);
        runtime.active = 0;
        arch_reset_kernel_stack();
        arch_return_to_caller();
    }
    if (next == previous) {
        task->state = TASK_RUNNING;
        runtime.slice_ticks = 0;
        return frame;
    }
    return activate(next, previous, cause);
}

static int run_batch(struct task *const tasks[], unsigned count, int pulse) {
    if (runtime.active || !tasks || !count || count > SCHEDULER_CAPACITY) {
        return 0;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (!tasks[i] || tasks[i]->state != TASK_READY
            || !task_context_valid(tasks[i], tasks[i]->context)) {
            return 0;
        }
        for (unsigned j = 0; j < i; ++j) {
            if (tasks[i] == tasks[j] || tasks[i]->id == tasks[j]->id) {
                return 0;
            }
        }
    }
    runtime.pulse = pulse;
    runtime.count = count;
    runtime.current = SCHEDULER_NO_TASK;
    runtime.trace_count = 0;
    runtime.statistics = (struct scheduler_statistics){0};
    for (unsigned i = 0; i < count; ++i) {
        runtime.tasks[i] = tasks[i];
    }
    /* Prepare the first TSS/frame without changing the caller's CR3. */
    struct task *first = tasks[0];
    /* Managed resumes must apply the same return-flags policy as batch switches. */
    first->context->eflags=(first->context->eflags & ~(0x3000u | 0x4000u | 0x20000u)) | 0x202u;
    if (!first->statistics.dispatches) first->first_tick = clock_ticks();
    first->statistics.dispatches++;
    first->state = TASK_RUNNING;
    runtime.current = 0;
    runtime.slice_ticks = 0;
    arch_select_kernel_stack((u32)first->kernel_stack,
        (u32)first->kernel_stack + TASK_KERNEL_STACK_BYTES);
    record(SCHEDULER_NO_TASK, 0, SCHEDULE_START);
    u32 control;
    __asm__ volatile("mov %%cr0,%0" : "=r"(control));
    __asm__ volatile("mov %0,%%cr0" :: "r"(control | 8u) : "memory");
    runtime.active = 1;
    arch_run_context(first->context, first->space.root);
    if (pulse) return first->state == TASK_READY || first->state == TASK_FINISHED || first->state == TASK_BLOCKED;
    for (unsigned i = 0; i < count; ++i) {
        if (tasks[i]->state != TASK_FINISHED) {
            return 0;
        }
    }
    return 1;
}

int scheduler_run(struct task *const tasks[], unsigned count) {
    return run_batch(tasks, count, 0);
}
int scheduler_pulse(struct task *task) {
    struct task *tasks[] = {task};
    return run_batch(tasks, 1, 1);
}

struct interrupt_frame *scheduler_tick(struct interrupt_frame *frame) {
    struct task *task = scheduler_current();
    if (!task || (frame->cs & 3) != 3) {
        return frame;
    }
    task->statistics.cpu_ticks++;
    if (task->statistics.cpu_ticks >= TASK_EXECUTION_BUDGET) {
        return task_finish(frame, 124, TASK_EXIT_BUDGET);
    }
    if (++runtime.slice_ticks < SCHEDULER_QUANTUM_TICKS) {
        return frame;
    }
    task->state = TASK_READY;
    return handoff(frame, SCHEDULE_TIMER);
}

struct interrupt_frame *scheduler_finish(struct interrupt_frame *frame, enum schedule_cause cause) {
    return handoff(frame, cause);
}

struct interrupt_frame *scheduler_yield(struct interrupt_frame *frame) {
    struct task *task = scheduler_current();
    if (!task) {
        return frame;
    }
    runtime.statistics.yields++;
    task->state = TASK_READY;
    return handoff(frame, SCHEDULE_YIELD);
}

int scheduler_can_block(void) {
    return runtime.active && runtime.pulse && scheduler_current()!=0;
}
struct interrupt_frame *scheduler_block(struct interrupt_frame *frame) {
    struct task *task=scheduler_current();
    if (!scheduler_can_block() || !task_context_valid(task,frame)) invariant_failed();
    task->state=TASK_BLOCKED;
    return handoff(frame,SCHEDULE_BLOCK);
}

const struct scheduler_statistics *scheduler_statistics(void) {
    return &runtime.statistics;
}

void scheduler_report(void) {
    console_puts("SCHED: tasks="); console_uint(runtime.count);
    console_puts(" switches="); console_uint(runtime.statistics.switches);
    console_puts(" preemptions="); console_uint(runtime.statistics.preemptions);
    console_puts(" yields="); console_uint(runtime.statistics.yields);
    console_putc('\n');
    for (unsigned i = 0; i < runtime.trace_count; ++i) {
        const struct schedule_event *event = &runtime.trace[i];
        console_puts("SWITCH: from="); console_uint(event->from_id);
        console_puts(" to="); console_uint(event->to_id);
        console_puts(" cause="); console_uint(event->cause);
        console_putc('\n');
    }
}
