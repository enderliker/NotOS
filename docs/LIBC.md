# User-Space C Library (Picolibc Port)

## Overview

NotOS ports **upstream Picolibc 1.8.11** as a freestanding static C library
(`build/sysroot/lib/notos/libc.a`) for `i486`/`i386` user-space applications,
backed by an explicit NotOS syscall adapter (`user/libc/`). Upstream sources are
fetched with a pinned SHA-256 hash and compiled without modification (see
[`third_party/README.md`](../third_party/README.md)).

User-space ELF binaries (such as `build/libc-demo.elf` / `/system/libc.elf` and
`/system/curl.elf`) link statically against `libc.a` with `--gc-sections`. Each
application runs in **ring 3** with its own private PAE `CR3`, data segment,
64 KiB user stack, and `sbrk` heap. The kernel itself never links against user
libc.

## Usage & Build

```text
libc-test
libc-run test
libc-run exit
libc-run files
libc-run stdin
libc-run pipeline
ps
```

- `libc-test` and `libc-run test` launch the ring 3 libc verification suite as a
  background process.
- `libc-run exit` verifies buffered `stdio` flushing, `atexit` handlers,
  `.fini_array` destructors, and exit status `23`.
- `libc-run pipeline` exercises `SYS_SPAWN` / `posix_spawn`, `SYS_PIPE`,
  `SYS_WAIT` / `waitpid`, `SYS_KILL`, `SYS_GETPPID`, `envp` inheritance,
  orphan/zombie cleanup, and 4-slot exhaustion.
- `libc-run noentropy` verifies that `getentropy` fails closed with `EIO` when
  booted without a `fw_cfg` entropy seed.

For offline builds, point `PICOLIBC_ARCHIVE` to a verified local tarball:

```sh
PICOLIBC_ARCHIVE=/path/to/picolibc-1.8.11.tar.xz make
```

The cross-build uses GNU `ld` without host libc, `libgcc`, or semihosting.
Compiler and Meson configuration flags are hashed into a build stamp so any
configuration change triggers a clean sysroot rebuild.

## Supported Runtime & Functions

- **Startup & Termination (`crt0.S`, `start.c`)**: `_start` aligns the stack and
  invokes the C startup routine with kernel-prepared `argc`, `argv`, `envp`
  (setting `environ`), and `AT_NULL`. Zeroes BSS, runs `.init_array`
  constructors before `main`, and runs `atexit` and `.fini_array` destructors on
  `exit` before flushing `stdout`/`stderr` and invoking `_exit`.
- **Dynamic Memory (`malloc`, `calloc`, `realloc`, `free`, `strdup`)**: Uses
  Picolibc's allocator over `sbrk` / `SYS_BRK`. Each process has up to **4 MiB**
  of private heap (`0x08400000..0x08800000`) with full OOM rollback and
  automatic frame reclamation on process exit or kill.
- **Strings, Sorting & Environment**: `memcpy`, `memmove`, `memset`, `strcmp`,
  `strtol`, `strtoll` (with overflow detection), `qsort`, `bsearch`, `environ`,
  `getenv`, `setenv`, and `unsetenv`.
- **Standard I/O (`printf`, `snprintf`, `puts`, `fprintf`, `fopen`, `fread`,
  `fwrite`, `fseek`, `fclose`)**: Configured for **integer and `long long`**
  formatting (no floating-point format specifiers). Transfers chunk I/O into
  calls of up to 4096 bytes over VFS file descriptors (`tmpfs`, `NotFS v2`,
  `/system`, `/dev`, and pipes).
- **Per-Process `errno`**: Private per-process global variable in each ELF's
  data segment. The NotOS syscall adapter translates negative kernel error codes
  to Picolibc `errno` constants.
- **64-bit Time & Sleep**: 64-bit `time_t`, `time`, `gettimeofday`,
  `clock_gettime` (`CLOCK_REALTIME` and `CLOCK_MONOTONIC`), `clock_getres`, and
  `gmtime_r`. `nanosleep` and `sleep` suspend the calling process via `SYS_SLEEP`
  (1 ms resolution, up to 60 s per syscall, chunked for longer sleeps) without
  blocking the shell.
- **Process Composition, Signals & Entropy**: `getpid`, `getppid`, `isatty`,
  `pipe`, `pipe2`, `wait`, `waitpid` (`WNOHANG`, `WIFEXITED`, `WEXITSTATUS`,
  `WIFSIGNALED`, `WTERMSIG`), `kill` (terminate-only, exit status `128 + sig`),
  `notos_spawn`, `posix_spawn` (with `posix_spawn_file_actions_init`,
  `posix_spawn_file_actions_destroy`, `posix_spawn_file_actions_adddup2`, and
  `posix_spawn_file_actions_addclose`), and `getentropy` (up to 256 bytes per
  call from the `fw_cfg`-seeded HMAC-DRBG; fails closed with `EIO` if unseeded).
- **Directories & Execution**: `mkdir`, `rmdir`, `chdir`, `getcwd`,
  `notos_readdir`, and `notos_exec` (`SYS_EXEC=37`).
- **Networking & Multiplexing**: BSD socket subset (`socket`, `connect`, `send`,
  `recv`, `shutdown`, `getsockopt`, `setsockopt`, `getsockname`, `getpeername`,
  `inet_pton`, `inet_ntop`, `getaddrinfo`, `freeaddrinfo`, `gai_strerror`),
  `poll`, and `select` (up to `nfds=64`).
- **Software `binary64` Runtime (`user/float_runtime.c`)**: Implements IEEE-754
  double-precision conversions, comparisons, and `+`/`-`/`*`/`/` using pure
  integer arithmetic (no FPU/SSE or `libgcc`), differentially tested against
  host hardware across 1,000,000 pairs under ASAN/UBSAN.

## Platform Syscall Additions (`INT 0x80`)

| Number | Syscall | Arguments (`EBX`, `ECX`, `EDX`, `ESI`, `EDI`) | Result (`EAX`) |
| --- | --- | --- | --- |
| `10` | `SYS_FD_WRITE` | `fd`, `buffer`, `length` | Bytes written or negative error |
| `11` | `SYS_TIME` | `clock_id` (`0` realtime, `1` monotonic), `out_timespec_wire` | `0` or negative error |
| `12` | `SYS_ENTROPY` | `buffer`, `length` (`1..256`) | `0` or negative error |
| `13` | `SYS_PID` | — | Positive process ID |
| `43` | `SYS_SPAWN` | `path`, `argv`, `envp`, `fd_map`, `map_count \| (flags << 16)` | Child PID or negative error |
| `44` | `SYS_WAIT` | `pid` (`-1` any or `>0`), `status_out`, `flags` (`WNOHANG`) | Reaped PID, `0`, or negative error |
| `45` | `SYS_GETPPID` | — | Live parent PID or `0` |
| `46` | `SYS_PIPE` | `fds_out[2]`, `flags` (`O_NONBLOCK \| O_CLOEXEC`) | `0` or negative error |
| `47` | `SYS_KILL` | `pid`, `sig` (`0..31`) | `0` or negative error |

`SYS_TIME` writes a fixed 12-byte wire structure (`u32 seconds_low`,
`u32 seconds_high`, `u32 nanoseconds`) using bytewise copy so unaligned user
pointers are safe. Static assertions verify layout compatibility with Picolibc's
`struct timespec`.

## Explicit Limits

- Single-threaded user space only: no `pthread`, thread-local storage (`_set_tls`
  is unused), `fork`, user-space signal handlers (`SYS_KILL` is terminate-only),
  or dynamic linking.
- Hardware FPU/MMX/SSE remains disabled (`CR0.TS=1`); `stdio` uses integer
  formatting and `libm` is not claimed.
- `select` adapts descriptor bitsets (`nfds <= 64`) over `poll`, mapping
  `POLLNVAL` to `EBADF`, clearing sets on timeout, preserving sets on error, and
  converting 64-bit `timeval` ceilings without overflow. Urgent TCP data
  (`POLLPRI` / `exceptfds`) is not generated by the network stack.

## Verification

`tests/libc.py` and `tests/pipeline.py` boot the production image in 16 MiB and
64 MiB RAM and verify sequential runs, exit status `23`, 4 concurrent processes
(with 5th slot rejection), string/calendar/integer formatting, 2,500 heap
allocation/free ops, alignment, zeroed `calloc`, failed 5 MiB `realloc`
preserving existing data, per-process `errno` isolation across `nanosleep`,
non-blocking `stdin` (`EAGAIN`), invalid user pointers (`EFAULT`), `select`
readiness/timeouts/errors, year-2050 RTC dates, fail-closed missing entropy
(`build/libc-noentropy.log`), a 2-stage 16 KiB checksummed pipeline, `envp`
inheritance, zombie/orphan cleanup, `kill` of blocked processes, and exact
restoration of physical frames, descriptors, pipes, sockets, and kernel heap
state.
