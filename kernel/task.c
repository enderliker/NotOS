#include "task.h"
#include "socket_stack.h"
#include "heap.h"
#include "scheduler.h"
#include "clock.h"

struct initial_context {
    struct interrupt_frame registers;
    struct user_interrupt_tail user;
};
_Static_assert(sizeof(struct initial_context) == 76, "x86 user context size");

static void initialize_context(struct task *task) {
    u8 *top = task->kernel_stack + TASK_KERNEL_STACK_BYTES;
    struct initial_context *context = (struct initial_context *)(top - sizeof(*context));
    *context = (struct initial_context){0};
    context->registers.gs = context->registers.fs = 0x23;
    context->registers.es = context->registers.ds = 0x23;
    context->registers.eip = task->space.entry;
    context->registers.cs = 0x1b;
    context->registers.eflags = 0x202;
    context->user.esp = task->space.initial_stack ? task->space.initial_stack : task->space.stack_top - 16;
    context->user.ss = 0x23;
    task->context = &context->registers;
    *(u32 *)task->kernel_stack = TASK_STACK_CANARY;
}

static int finish_creation(struct task *task,u32 id) {
    task->kernel_stack=kcalloc(1,TASK_KERNEL_STACK_BYTES);
    if (!task->kernel_stack) {
        space_destroy(&task->space);
        return 0;
    }
    if (!fd_table_init(&task->files)) {
        kfree(task->kernel_stack);task->kernel_stack=0;
        space_destroy(&task->space);return 0;
    }
    task->cwd[0] = '/';
    task->cwd[1] = 0;
    task->execution_budget = TASK_EXECUTION_BUDGET;
    task->id=id;
    task->state=TASK_READY;
    initialize_context(task);
    return 1;
}

int task_create(struct task *task, u32 id, const u8 *image, u32 bytes) {
    if (!task || !id || task->state != TASK_UNUSED || task->space.root || task->kernel_stack) {
        return 0;
    }
    *task = (struct task){0};
    if (!space_create(&task->space, image, bytes)) {
        return 0;
    }
    return finish_creation(task,id);
}

int task_create_elf(struct task *task,u32 id,const u8 *image,u32 bytes) {
    if (!task || !id || task->state!=TASK_UNUSED || task->space.root || task->kernel_stack) return 0;
    *task=(struct task){0};
    if (!space_create_elf(&task->space,image,bytes)) return 0;
    return finish_creation(task,id);
}

int task_destroy(struct task *task) {
    if (!task || task == task_current() || task->state == TASK_RUNNING) {
        return 0;
    }
    if (!space_destroy(&task->space)) {
        return 0;
    }
    fd_table_destroy(&task->files);
    u8 *stack = task->kernel_stack;
    task->kernel_stack = 0;
    task->context = 0;
    task->state = TASK_UNUSED;
    if (stack && !kfree(stack)) {
        return 0;
    }
    return 1;
}

struct task *task_current(void) {
    return scheduler_current();
}

int task_run_single(struct task *task) {
    struct task *tasks[] = {task};
    return scheduler_run(tasks, 1);
}

int task_context_valid(const struct task *task, const struct interrupt_frame *frame) {
    if (!task || !task->kernel_stack || !frame) {
        return 0;
    }
    u32 low = (u32)task->kernel_stack;
    u32 top = low + TASK_KERNEL_STACK_BYTES;
    return *(const u32 *)task->kernel_stack == TASK_STACK_CANARY
        && (u32)frame >= low + 16
        && (u32)frame <= top - sizeof(struct initial_context)
        && frame->cs == 0x1b;
}

struct interrupt_frame *task_finish(struct interrupt_frame *frame, u32 status, enum task_exit_reason reason) {
    struct task *task = task_current();
    task->result.status = status;
    task->result.reason = reason;
    task->statistics.elapsed_ticks = clock_ticks() - task->first_tick;
    task->state = TASK_FINISHED;
    enum schedule_cause cause = reason == TASK_EXIT_FAULT ? SCHEDULE_FAULT
        : reason == TASK_EXIT_BUDGET ? SCHEDULE_BUDGET : SCHEDULE_EXIT;
    return scheduler_finish(frame, cause);
}

struct interrupt_frame *task_handle_fault(struct interrupt_frame *frame) {
    struct task *task = task_current();
    if (!task || (frame->cs & 3) != 3) {
        return frame;
    }
    if (frame->vector == 2 || frame->vector == 8 || frame->vector == 18) {
        return frame;
    }
    task->result.fault_vector = frame->vector;
    task->result.fault_error = frame->error;
    if (frame->vector == 14) {
        __asm__ volatile("mov %%cr2,%0" : "=r"(task->result.fault_address));
    }
    return task_finish(frame, 128 + frame->vector, TASK_EXIT_FAULT);
}

struct interrupt_frame *task_exec(struct task *task, struct user_space *replacement) {
    struct user_space previous = task->space;
    u32 flags = irq_save();
    task->space = *replacement;
    *replacement = (struct user_space){0};
    __asm__ volatile("mov %0,%%cr3" :: "r"(task->space.root) : "memory");
    initialize_context(task);
    irq_restore(flags);
    /* The old root is inactive; the kernel stack is shared supervisor memory. */
    socket_resolve_cancel(&task->files);
    fd_close_exec(&task->files);
    space_destroy(&previous);
    return task->context;
}
