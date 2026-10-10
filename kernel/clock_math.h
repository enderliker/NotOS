#ifndef NOTOS_CLOCK_MATH_H
#define NOTOS_CLOCK_MATH_H
#include "base.h"
#define PIT_HZ 1193182u
#define PIT_DIVISOR 1193u
struct clock_state {volatile u32 elapsed,ticks;u32 fraction;volatile u32 elapsed_high;};
static inline void clock_advance(struct clock_state *state) {
    state->ticks++;
    state->fraction+=PIT_DIVISOR*1000u;
    if(state->fraction>=PIT_HZ) {if (++state->elapsed==0) state->elapsed_high++;state->fraction-=PIT_HZ;}
}
#endif
