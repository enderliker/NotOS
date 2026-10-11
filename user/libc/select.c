/* select() implemented over poll() syscall. */
#include "../../kernel/poll_abi.h"
#include <sys/select.h>
#include <poll.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>

#define SELECT_MAX_FDS (FD_SETSIZE < (int)NOTOS_POLL_MAX ? FD_SETSIZE : (int)NOTOS_POLL_MAX)

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout) {
    if (nfds < 0 || nfds > SELECT_MAX_FDS) {
        errno = EINVAL;
        return -1;
    }

    int timeout_ms = -1;
    if (timeout) {
        if (timeout->tv_sec < 0 || timeout->tv_usec < 0 || timeout->tv_usec >= 1000000) {
            errno = EINVAL;
            return -1;
        }
        unsigned long long ms = (unsigned long long)timeout->tv_sec * 1000ULL +
                                ((unsigned long long)timeout->tv_usec + 999ULL) / 1000ULL;
        if (ms > (unsigned long long)INT_MAX) timeout_ms = INT_MAX;
        else timeout_ms = (int)ms;
    }

    if (nfds == 0) {
        int ret = poll(NULL, 0, timeout_ms);
        if (ret < 0) return ret;
        if (readfds) FD_ZERO(readfds);
        if (writefds) FD_ZERO(writefds);
        if (exceptfds) FD_ZERO(exceptfds);
        return 0;
    }

    struct pollfd pfds[SELECT_MAX_FDS];
    int count = 0;
    for (int fd = 0; fd < nfds; fd++) {
        short events = 0;
        if (readfds && FD_ISSET(fd, readfds)) events |= POLLIN;
        if (writefds && FD_ISSET(fd, writefds)) events |= POLLOUT;
        if (exceptfds && FD_ISSET(fd, exceptfds)) events |= POLLERR;
        if (events) {
            if (count >= SELECT_MAX_FDS) {
                errno = EINVAL;
                return -1;
            }
            pfds[count].fd = fd;
            pfds[count].events = events;
            pfds[count].revents = 0;
            count++;
        }
    }

    int ret = poll(count ? pfds : NULL, (nfds_t)count, timeout_ms);
    if (ret < 0) return ret;

    if (readfds) FD_ZERO(readfds);
    if (writefds) FD_ZERO(writefds);
    if (exceptfds) FD_ZERO(exceptfds);

    if (ret == 0) return 0;

    int ready = 0;
    for (int i = 0; i < count; i++) {
        if (readfds && (pfds[i].revents & (POLLIN | POLLHUP))) {
            FD_SET(pfds[i].fd, readfds);
            ready++;
        }
        if (writefds && (pfds[i].revents & POLLOUT)) {
            FD_SET(pfds[i].fd, writefds);
            ready++;
        }
        if (exceptfds && (pfds[i].revents & (POLLERR | POLLNVAL))) {
            FD_SET(pfds[i].fd, exceptfds);
            ready++;
        }
    }
    return ready;
}
