#ifndef NOTOS_SCHEDULER_POLICY_H
#define NOTOS_SCHEDULER_POLICY_H
#define SCHEDULER_CAPACITY 4u
#define SCHEDULER_NO_TASK (~0u)

enum task_state {
    TASK_UNUSED,
    TASK_READY,
    TASK_RUNNING,
    TASK_FINISHED
};

/* Pure selection policy: no CPU registers, allocation, timers or output. */
unsigned round_robin_next(const enum task_state *states, unsigned count, unsigned previous);
#endif
