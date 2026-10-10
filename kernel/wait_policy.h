#ifndef NOTOS_WAIT_POLICY_H
#define NOTOS_WAIT_POLICY_H
#include "base.h"
#define WAIT_MAX_MS 60000u
/* Relative unsigned elapsed time remains correct across the 32-bit clock wrap. */
static inline int wait_due(u32 begun,u32 duration,u32 now) {return (u32)(now-begun)>=duration;}
#endif
