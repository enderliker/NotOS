#include "syscall.h"
#include "syscall_numbers.h"
#include "task.h"
#include "arch.h"
#include "clock.h"
#include "paging.h"

static void probe(struct task *task, struct interrupt_frame *frame) {
    const struct user_interrupt_tail *tail = (const struct user_interrupt_tail *)(frame + 1);
    struct task_statistics *statistics = &task->statistics;
    statistics->observed_cpl = frame->cs & 3;
    statistics->observed_ss = tail->ss;
    statistics->observed_root = paging_current_root();
    statistics->stack_valid = (u32)frame >= arch_kernel_stack_low()
        && (u32)frame + sizeof(*frame) + sizeof(*tail) <= arch_kernel_stack_top()
        && tail->esp >= USER_BASE + 12288
        && tail->esp < USER_BASE + 16384;
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

struct interrupt_frame *syscall_dispatch(struct interrupt_frame *frame) {
    struct task *task = task_current();
    if (!task || (frame->cs & 3) != 3) {
        frame->eax = (u32)-ERROR_PERMISSION;
        return frame;
    }
    task->statistics.syscalls++;
    switch (frame->eax) {
    case SYS_EXIT:
        return task_finish(frame, frame->ebx, TASK_EXIT_NORMAL);
    case SYS_TICKS:
        frame->eax = clock_ms();
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
