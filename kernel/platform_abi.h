#ifndef NOTOS_PLATFORM_ABI_H
#define NOTOS_PLATFORM_ABI_H
#include "base.h"
#define NOTOS_CLOCK_REALTIME 0u
#define NOTOS_CLOCK_MONOTONIC 1u
#define NOTOS_CONSOLE_LIMIT 4096u
#define NOTOS_ENTROPY_LIMIT 256u
/* Fixed layout across i386 and host tests; never expose native time_t or padding. */
struct notos_timestamp {u32 seconds_low,seconds_high,nanoseconds;};
_Static_assert(sizeof(struct notos_timestamp)==12,"time syscall ABI");
#endif
