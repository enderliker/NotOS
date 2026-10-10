#include "scheduler_policy.h"

unsigned round_robin_next(const enum task_state *states, unsigned count, unsigned previous) {
    if (!states || !count || count > SCHEDULER_CAPACITY) {
        return SCHEDULER_NO_TASK;
    }
    unsigned first = previous < count ? (previous + 1) % count : 0;
    for (unsigned offset = 0; offset < count; ++offset) {
        unsigned index = (first + offset) % count;
        if (states[index] == TASK_READY) {
            return index;
        }
    }
    return SCHEDULER_NO_TASK;
}
