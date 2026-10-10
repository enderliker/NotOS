#ifndef NOTOS_POLL_ABI_H
#define NOTOS_POLL_ABI_H
#include "base.h"
/* Stable wire layout; vectors are bounded independently of fd-table capacity. */
#define NOTOS_POLL_MAX 64u
#define NOTOS_POLLIN 0x001u
#define NOTOS_POLLOUT 0x004u
#define NOTOS_POLLERR 0x008u
#define NOTOS_POLLHUP 0x010u
#define NOTOS_POLLNVAL 0x020u
#define NOTOS_POLLRDHUP 0x2000u
#define NOTOS_POLL_ALWAYS (NOTOS_POLLERR|NOTOS_POLLHUP|NOTOS_POLLNVAL)
struct notos_pollfd {int fd;u16 events,revents;};
_Static_assert(sizeof(struct notos_pollfd)==8,"pollfd wire ABI");
#endif
