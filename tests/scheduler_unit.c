#include <assert.h>
#include <stdio.h>
#include "../kernel/scheduler_policy.h"

int main(void) {
    enum task_state states[4] = {TASK_READY, TASK_READY, TASK_READY, TASK_READY};
    unsigned visits[4] = {0};
    unsigned current = SCHEDULER_NO_TASK;
    for (unsigned i = 0; i < 1000; ++i) {
        if (current != SCHEDULER_NO_TASK) states[current] = TASK_READY;
        current = round_robin_next(states, 4, current);
        assert(current < 4);
        states[current] = TASK_RUNNING;
        visits[current]++;
    }
    for (unsigned i = 0; i < 4; ++i) assert(visits[i] == 250);
    states[0] = TASK_FINISHED; states[1] = TASK_UNUSED;
    states[2] = TASK_READY; states[3] = TASK_FINISHED;
    assert(round_robin_next(states, 4, 2) == 2);
    states[2] = TASK_FINISHED;
    assert(round_robin_next(states, 4, 0) == SCHEDULER_NO_TASK);
    assert(round_robin_next(0, 4, 0) == SCHEDULER_NO_TASK);
    assert(round_robin_next(states, 0, 0) == SCHEDULER_NO_TASK);
    assert(round_robin_next(states, 5, 0) == SCHEDULER_NO_TASK);
    puts("PASS: round-robin fairness, wrap, retired tasks and invalid inputs");
}
