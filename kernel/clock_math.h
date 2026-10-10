#ifndef NOTOS_CLOCK_MATH_H
#define NOTOS_CLOCK_MATH_H
#include "base.h"
#define PIT_HZ 1193182u
#define PIT_DIVISOR 1193u
struct clock_state {volatile u32 elapsed,ticks;u32 fraction;};
static inline void clock_advance(struct clock_state *state) {
    state->ticks++;
    state->fraction+=PIT_DIVISOR*1000u;
    if(state->fraction>=PIT_HZ) {state->elapsed++;state->fraction-=PIT_HZ;}
}
#endif
