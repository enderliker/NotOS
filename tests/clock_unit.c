#include <assert.h>
#include <stdio.h>
#include "../kernel/clock_math.h"
int main(void) {
    struct clock_state state={0,0,0};
    for(unsigned i=1;i<=1000000;i++) {
        clock_advance(&state);
        unsigned long long expected=(unsigned long long)i*PIT_DIVISOR*1000u;
        assert(state.ticks==i);
        assert(state.elapsed==expected/PIT_HZ);
        assert(state.fraction==expected%PIT_HZ);
    }
    state.ticks=0xffffffffu;state.elapsed=0xffffffffu;state.fraction=PIT_HZ-1;
    unsigned begin=state.elapsed;
    clock_advance(&state);
    assert(state.ticks==0 && state.elapsed==0);
    assert((unsigned)(state.elapsed-begin)==1);
    assert(state.fraction==PIT_DIVISOR*1000u-1);
    state.elapsed=0xfffffffau;state.ticks=0;state.fraction=0;
    begin=state.elapsed;
    for(unsigned i=0;i<10;i++) clock_advance(&state);
    assert((unsigned)(state.elapsed-begin)==9);
    puts("PASS: PIT rational time accounting over 1000000 IRQs and unsigned rollover");
}
