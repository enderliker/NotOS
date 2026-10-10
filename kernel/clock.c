#include "base.h"
#include "clock.h"
#include "clock_math.h"
static struct clock_state state;
void clock_init(void) {
    state.elapsed=state.ticks=state.fraction=0;
    outb(0x43,0x34);outb(0x40,(u8)PIT_DIVISOR);outb(0x40,(u8)(PIT_DIVISOR>>8));
}
void clock_irq(void) {clock_advance(&state);}
unsigned clock_ms(void) {return state.elapsed;}
unsigned clock_ticks(void) {return state.ticks;}
int clock_sleep(unsigned ms) {
    if(!irq_enabled()) return 0;
    unsigned begin=clock_ms();
    for(;;) {
        /* No lost-wakeup window: check with IF=0, then atomic STI/HLT pair. */
        u32 flags=irq_save();
        if((unsigned)(clock_ms()-begin)>=ms) {irq_restore(flags);return 1;}
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}
