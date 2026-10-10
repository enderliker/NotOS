#ifndef NOTOS_USER_POLL_H
#define NOTOS_USER_POLL_H
#include <stdint.h>
typedef uint32_t nfds_t;
struct pollfd {int fd;short events,revents;};
#define POLLIN 0x001
#define POLLPRI 0x002
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010
#define POLLNVAL 0x020
#define POLLRDHUP 0x2000
int poll(struct pollfd *,nfds_t,int timeout);
#endif
