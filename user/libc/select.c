#include "os.h"
#include "../../kernel/poll_abi.h"
#include <sys/select.h>
#include <poll.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>

#define SELECT_MAX_FDS (FD_SETSIZE < (int)NOTOS_POLL_MAX ? FD_SETSIZE : (int)NOTOS_POLL_MAX)

static int select_check(void *pointer, size_t bytes, int writable) {
    if (!pointer) return 0;
    return notos_checked(notos_raw_call(SYS_USER_CHECK, (uint32_t)pointer, bytes, writable));
}

static int select_timeout(const struct timeval *timeout) {
    if (!timeout) return -1;
    if (timeout->tv_sec < 0 || timeout->tv_usec < 0 || timeout->tv_usec >= 1000000) return -2;
    if (timeout->tv_sec > INT_MAX / 1000) return INT_MAX;
    unsigned long long timeout_ms = (unsigned long long)timeout->tv_sec * 1000 +
        ((unsigned long long)timeout->tv_usec + 999) / 1000;
    return timeout_ms > INT_MAX ? INT_MAX : (int)timeout_ms;
}

static int select_results(struct pollfd *descriptors, int count,
    fd_set *readfds, fd_set *writefds, fd_set *exceptfds) {
    for (int i = 0; i < count; i++) {
        if (descriptors[i].revents & POLLNVAL) {
            errno = EBADF;
            return -1;
        }
    }
    if (readfds) FD_ZERO(readfds);
    if (writefds) FD_ZERO(writefds);
    if (exceptfds) FD_ZERO(exceptfds);
    int ready = 0;
    for (int i = 0; i < count; i++) {
        short events = descriptors[i].events;
        short returned = descriptors[i].revents;
        int fd = descriptors[i].fd;
        if (readfds && (events & POLLIN) && (returned & (POLLIN | POLLHUP | POLLERR))) {
            FD_SET(fd, readfds);
            ready++;
        }
        if (writefds && (events & POLLOUT) && (returned & (POLLOUT | POLLERR))) {
            FD_SET(fd, writefds);
            ready++;
        }
        if (exceptfds && (events & POLLPRI) && (returned & POLLPRI)) {
            FD_SET(fd, exceptfds);
            ready++;
        }
    }
    return ready;
}

int select(int nfds, fd_set *readfds, fd_set *writefds,
    fd_set *exceptfds, struct timeval *timeout) {
    if (nfds < 0 || nfds > SELECT_MAX_FDS) {
        errno = EINVAL;
        return -1;
    }
    /* Validate every supplied span before reading inputs or changing output sets. */
    if (select_check(readfds, sizeof(*readfds), 1) ||
        select_check(writefds, sizeof(*writefds), 1) ||
        select_check(exceptfds, sizeof(*exceptfds), 1) ||
        select_check(timeout, sizeof(*timeout), 0)) return -1;
    int timeout_ms = select_timeout(timeout);
    if (timeout_ms == -2) {
        errno = EINVAL;
        return -1;
    }
    struct pollfd descriptors[SELECT_MAX_FDS];
    int count = 0;
    for (int fd = 0; fd < nfds; fd++) {
        short events = 0;
        if (readfds && FD_ISSET(fd, readfds)) events |= POLLIN;
        if (writefds && FD_ISSET(fd, writefds)) events |= POLLOUT;
        if (exceptfds && FD_ISSET(fd, exceptfds)) events |= POLLPRI;
        if (events) descriptors[count++] = (struct pollfd){fd, events, 0};
    }
    if (poll(count ? descriptors : NULL, (nfds_t)count, timeout_ms) < 0) return -1;
    return select_results(descriptors, count, readfds, writefds, exceptfds);
}
