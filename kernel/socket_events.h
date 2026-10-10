#ifndef NOTOS_SOCKET_EVENTS_H
#define NOTOS_SOCKET_EVENTS_H
#include "poll_abi.h"
/* Pure policy: no PCB pointers, I/O, consumption of bytes, or SO_ERROR mutation. */
struct socket_readiness {
    int pending,connected,failed,error,readable,eof,read_closed,write_closed,writable;
};
static inline u16 socket_events(struct socket_readiness s) {
    if (s.pending) return 0;
    u16 events=s.error?NOTOS_POLLERR:0;
    if (s.failed) return events|NOTOS_POLLHUP|NOTOS_POLLIN|NOTOS_POLLOUT;
    if (!s.connected) return events|NOTOS_POLLHUP|NOTOS_POLLOUT;
    if (s.readable || s.eof || s.read_closed) events|=NOTOS_POLLIN;
    if (s.writable || s.write_closed) events|=NOTOS_POLLOUT;
    if (s.eof || s.read_closed) events|=NOTOS_POLLRDHUP;
    if ((s.eof || s.read_closed) && s.write_closed) events|=NOTOS_POLLHUP;
    return events;
}
#endif
