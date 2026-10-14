# Preemptive Multitasking and Scheduler

## Module Contracts & Separation

- `task.*`: Owns task state, initial trap frame construction, private address
  space and 16 KiB kernel stack allocation, and exit/fault status recording.
  Tasks are zero-initialized, roll back partial allocations on error, and are
  never freed while active.
- `scheduler_policy.*`: Pure, host-tested round-robin selection over 4 slots
  without hardware I/O, console output, or memory allocation. Selects only
  `TASK_READY` tasks and ignores `TASK_UNUSED`, `TASK_BLOCKED`, and
  `TASK_FINISHED` slots.
- `scheduler.*`: Orchestrates synchronous diagnostic batches (`1..4` tasks) and
  single-pulse background execution (`scheduler_pulse`), tracking 5-tick
  quantums, 250-tick CPL3 budgets, dispatch/preemption statistics, and a bounded
  32-event trace buffer.
- `syscall.*`: Validates user arguments and handles `SYS_YIELD` (`5`), returning
  `0` when the task is next scheduled.
- `multitask.*`: Implements laboratory batch scenarios and verifies results
  outside IRQ context (`user_test_abi.h`).
- `arch.*`, `user_entry.S`, `isr.S`: Manage the x86 TSS (`ESP0`) and trap frame
  save/restore.

## Context Switching & Time Accounting

Each task owns a private PAE `CR3` and a 16 KiB supervisor stack allocated from
the kernel heap with a low-end canary word. On a context switch:
1. `IRQ0` sends PIC EOI **before** switching context.
2. The scheduler updates `TSS.ESP0` to the top of the incoming task's kernel
   stack and loads its private `CR3`.
3. Assembly (`isr.S`) switches `ESP` to the selected 76-byte trap frame and
   executes `IRET`, restoring general-purpose registers, segment selectors,
   `EIP`, `EFLAGS`, and user `ESP`/`SS`.

When a synchronous batch has no `TASK_READY` tasks left (or when a background
pulse yields/blocks/finishes), the scheduler restores the saved kernel trap
frame and supervisor `CR3`.

- **Quantum**: 5 `IRQ0` ticks received while executing in CPL 3.
- **CPU Budget**: 250 CPL3 `IRQ0` ticks per standard task (2,500 ticks for
  `/system/curl.elf`). Exhausting the budget terminates the task with exit
  status `124`.
- **Fault & FPU Isolation**: User-mode faults terminate only the faulting task
  and record the vector/error/`CR2`. `CR0.TS` is set so any x87/MMX instruction
  raises `#NM` (vector 7), terminating that task with status `135` without
  corrupting other tasks.

## Diagnostic Batch Scenarios

```text
multitask-test          # two preemptive workers without voluntary yield
multitask-run workers
multitask-run four      # four concurrent workers with round-robin wraparound
multitask-run yield     # two workers cooperating via SYS_YIELD
multitask-run fault     # two workers survive a third task's #PF fault
multitask-run budget    # normal worker completes while infinite loop hits 124
multitask-run fpu       # x87 instruction trapped (#NM) while peer completes
```

Each worker seeds `EBX`, `EDX`, `ESI`, `EDI`, `EBP`, and `EFLAGS.DF` with canary
values, mutates its private virtual data page, verifies all registers upon
resuming after preemption, and exits with status `42`.

## Verification & Limits

`make test` runs `tests/scheduler_unit.c` on the host and boots QEMU with 16,
32, and 64 MiB RAM (`tests/multitask.py`) to verify all batch scenarios,
rejection of invalid batches, OOM rollback when kernel stacks cannot all be
allocated, and exact restoration of physical frames and kernel heap bytes.

Synchronous diagnostic batches (`multitask-run`) run to completion before
returning to the shell; for persistent background processes that interleave with
the shell and networking, see [PROCESSES.md](PROCESSES.md).
