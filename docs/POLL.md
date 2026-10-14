# Multiplexed I/O (`poll` and `select`)

## Overview

`poll(struct pollfd *fds, nfds_t count, int timeout)` and `select(...)` are
provided in the NotOS user-space C library (`user/libc/poll.c`). `poll` waits
for readiness across multiple file descriptors and sockets via native kernel
syscall **`SYS_POLL` (`36`)**—never a `nanosleep` polling loop or host socket
passthrough. When no requested events are ready, the calling process enters
`TASK_BLOCKED` (`TASK_WAIT_POLL`) and yields control back to the kernel event
loop while the shell, NIC, IRQs, and other processes continue running.

- `kernel/poll_abi.h`: Fixed wire format and event bitmasks; up to **64 entries**
  per call (independent of the 16-FD-per-process table size), allowing duplicate
  and negative FDs.
- `fd_poll` (`kernel/fd.c`): Queries descriptor readiness without consuming data
  or mutating file offsets, flags, or `SO_ERROR`.
- `kernel/socket_events.h`: Pure, host-tested readiness policy mapped by
  `socket_ready` (`kernel/socket_stack.c`) from lwIP endpoint state and TX
  buffer capacity.
- `kernel/poll_wait.c`: Validates user buffers, snapshots `fd`/`events` into
  bounded task storage, and registers/completes `TASK_WAIT_POLL`.
- `process_poll` (`kernel/process.c`): Evaluates pending waits before each
  round-robin step and copies `revents` back to the blocked task's address space
  via physical frame aliases without switching `CR3` in IRQ context.

## `poll` Contract

```c
struct pollfd {
    int fd;
    short events;
    short revents;
};

int poll(struct pollfd *fds, nfds_t count, int timeout);
```

Returns the **number of `pollfd` entries** with non-zero `revents` (not the bit
count or unique FD count), `0` on timeout expiration, or `-1` with `errno` set
on error:
- `timeout == 0`: Non-blocking poll; returns immediately.
- `timeout > 0`: Relative wait up to `INT_MAX` ms using wrap-safe unsigned
  32-bit tick arithmetic.
- `timeout < 0`: Infinite wait until at least one entry becomes ready or the
  process is terminated (`kill`).
- `count == 0`: Pure timed or infinite sleep without descriptors (`fds` pointer
  is ignored; returns `0` immediately when `timeout == 0`).
- `count > 64`: Fails immediately with `EINVAL` before reading user memory.
- **Full buffer validation**: The kernel validates read/write permissions across
  the entire `fds` array before touching memory. Kernel, read-only, overflowing,
  or unmapped user pointers fail with `EFAULT`. Unaligned `pollfd` vectors are
  copied bytewise (`memcpy`).
- `fd < 0`: Ignored (`revents` set to `0`).
- Unopened or out-of-range `fd >= 0`: Sets `POLLNVAL` in that entry's `revents`
  (never a global `EBADF`).
- `POLLIN`, `POLLOUT`, and `POLLRDHUP` are masked by `events`; `POLLERR`,
  `POLLHUP`, and `POLLNVAL` are reported unconditionally even when `events == 0`.
- If an event and timeout occur on the same event-loop pass, readiness takes
  priority over timeout.

## Readiness by Descriptor Backend

| Backend | Reported Events |
| --- | --- |
| `tmpfs` / `NotFS v2` / `/system` / `/dev/null` | `POLLIN` and `POLLOUT` whenever requested (including at EOF) |
| `stdout` / `stderr` | `POLLOUT` |
| `stdin` / `/dev/console` | `POLLIN` when input bytes or EOF are queued (`POLLOUT` also on `/dev/console`); `POLLHUP` on permanent EOF |
| Pipe read end | `POLLIN` when `count > 0`; `POLLHUP` when `writers == 0` (both `POLLIN \| POLLHUP` if buffered bytes remain after writers close) |
| Pipe write end | `POLLOUT` when `readers > 0` and `count < 4096`; `POLLERR` when `readers == 0` |
| TCP connecting | None until handshake completes or fails |
| TCP connected | `POLLIN` on queued RX bytes, remote FIN/EOF, or `SHUT_RD`; `POLLOUT` when TX buffer/queue space is available or `SHUT_WR` is set |
| TCP remote FIN | `POLLIN` (and `POLLRDHUP` if requested); `POLLHUP` when both directions are closed |
| TCP failed / RST / refused | Requested `POLLIN`/`POLLOUT` plus `POLLHUP`, and `POLLERR` while `SO_ERROR` remains unconsumed |
| TCP unbound / unconnected socket | `POLLHUP` and requested `POLLOUT` (never `POLLNVAL`) |

Reporting `POLLERR` does **not** consume `SO_ERROR`: repeated `poll` calls
continue reporting `POLLERR` until `getsockopt(..., SO_ERROR, ...)` consumes it
once.

## `select` Adapter (`user/libc/poll.c`)

Picolibc provides `select(nfds, readfds, writefds, exceptfds, timeout)` layered
on top of `poll` for `0 <= nfds <= 64`:
- Translates `POLLNVAL` on any watched descriptor into `EBADF`.
- Clears all descriptor sets on timeout (`0`), preserves input sets unchanged on
  error (`-1`), and returns the total number of ready bits set across the output
  sets on success.
- Converts 64-bit `struct timeval` values to milliseconds by rounding up
  microseconds and saturating at `INT_MAX` ms without signed overflow, without
  mutating the caller's `timeval`.

## Shell Diagnostics & Verification

```text
socket-run poll-basic 1
socket-run poll-timer 1
socket-run poll-stdin 1
feed PID hello
feed-eof PID
socket-run poll-wait 1
socket-run poll-web 80
```

- `tests/poll_unit.c` (ASAN/UBSAN): Verifies snapshot immutability against
  concurrent user memory mutation, invalid buffer ranges, duplicate/negative
  FDs, zero/relative/wrapping/infinite timeouts, cancellation, and readiness
  priority at deadline.
- `tests/poll.py` (QEMU 16/64 MiB): Verifies dual non-blocking connects, delayed
  multi-peer RX, `MSG_PEEK`, `FIN`/`POLLRDHUP`/`POLLHUP`, refused/RST sockets,
  non-destructive `SO_ERROR`, TX backpressure (`EAGAIN` -> `POLLOUT`), `stdin`
  wakeups via `feed`/`feed-eof`, 4 concurrent blocked processes, and exact
  resource restoration after `kill` and `#UD` faults.
