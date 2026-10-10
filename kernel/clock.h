#ifndef NOTOS_CLOCK_H
#define NOTOS_CLOCK_H
#include "base.h"
void clock_init(void);
void clock_irq(void);
unsigned clock_ms(void);
u64 clock_uptime_ms(void);
unsigned clock_ticks(void);
int clock_sleep(unsigned ms);
#endif
