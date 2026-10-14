# User-Space Sockets and DNS

## Scope

NotOS provides a **real ring 3 TCP/IPv4 client and DNS resolver** via Picolibc
and custom `INT 0x80` syscalls (`26..35`). Upstream **lwIP 2.2.1** (`BSD-3-Clause`)
supplies TCP, internal UDP for DNS, ARP, and ICMP over NotOS's custom RTL8139
PCI/DMA driver. No host sockets, Linux kernel code, semihosting, or TLS proxies
are used.

- `user/libc/socket.c` and public headers in `user/include/` translate the BSD
  socket API and `errno` values to the fixed wire ABI in `kernel/socket_abi.h`.
- `kernel/socket_stack.c` wraps lwIP's raw callback API (`NO_SYS=1`); `kernel/fd.c`
  owns open file description reference counts; `kernel/syscall.c` validates user
  buffers and registers blocking waits; and `process_poll` (`kernel/process.c`)
  completes blocked operations through physical frame aliases of the waiting
  process's address space. lwIP is never entered from an IRQ handler.

## Supported Socket & DNS Operations

- `socket(AF_INET, SOCK_STREAM, 0 / IPPROTO_TCP)` with `SOCK_NONBLOCK` and
  `SOCK_CLOEXEC`.
- `connect`, `send`, `recv`, `shutdown`, `getsockname`, `getpeername`, `close`.
- Standard descriptor operations on sockets: `read`, `write`, `dup`, `dup2`,
  `fcntl` (`F_GETFD`, `F_SETFD`, `F_GETFL`, `F_SETFL`), `fstat` (`S_IFSOCK`),
  `poll`, and `select`.
- Per-call flags: `MSG_DONTWAIT`, `MSG_PEEK`, and `MSG_NOSIGNAL` (no `SIGPIPE`
  exists in this runtime).
- Socket options (`getsockopt` / `setsockopt`): `SO_ERROR` (consumed once),
  `SO_TYPE`, `SO_RCVTIMEO`, `SO_SNDTIMEO`, `TCP_NODELAY`, and read-only buffer
  capacity queries `SO_RCVBUF` / `SO_SNDBUF`.
- Address & DNS helpers: `inet_pton`, `inet_ntop`, byte-order conversions
  (`htons`, `htonl`, `ntohs`, `ntohl`), and `getaddrinfo` / `freeaddrinfo` /
  `gai_strerror` supporting IPv4 `SOCK_STREAM`, numeric services, DNS `A`
  queries, `NXDOMAIN`, timeouts, and `AI_PASSIVE` / `AI_NUMERICHOST` /
  `AI_NUMERICSERV`.

Blocking `connect`, `send`, and `recv` suspend only the **calling process**
(`TASK_WAIT_CONNECT`, `TASK_WAIT_SEND`, `TASK_WAIT_RECEIVE`), leaving the shell,
NIC, and other processes responsive. Non-blocking sockets return `EINPROGRESS`
or `EAGAIN`. Blocking `connect` defaults to a 10 s timeout; `SO_RCVTIMEO` and
`SO_SNDTIMEO` accept `0` (infinite) or `1..60000` ms and fail with `EAGAIN` on
expiration. `getaddrinfo` polls asynchronous kernel DNS tokens using 10 ms
`SYS_SLEEP` intervals rather than spinning.

## Resource Quotas & Lifecycle

- **8 global socket endpoints** and **8 lwIP TCP PCBs** across the 4 managed
  process slots (16 FDs per process, 64 global open file descriptions).
- **8192-byte RX/TX windows**, MSS `1460`, 96 TCP segments, 64 KiB static lwIP
  heap, and 32 `pbuf`s of 1600 bytes. Individual `send`/`recv` syscalls transfer
  up to 4096 bytes and may return partial counts.
- **4 concurrent DNS queries** (at most one active query per process), hostnames
  up to 95 bytes, and an 8-second application deadline. Monotonic 31-bit tokens
  are never reused, and process exit/`kill`/`SYS_EXEC` cancels ownership so late
  DNS callbacks never touch a reused process slot.
- **Explicit PCB reclamation**: If all 8 TCP PCBs are occupied when opening a
  new socket, the stack reclaims only an unowned `TIME_WAIT` PCB; active
  connections are never evicted (`ENOMEM`).
- **Half-close & `TIME_WAIT` safety**: Remote `FIN` preserves pending RX bytes
  and EOF while still allowing TX until `SHUT_WR`. `MSG_PEEK` neither consumes
  bytes nor advances the TCP window. When both sides close and the PCB enters a
  recyclable state, lwIP callbacks are detached while endpoint address and
  `TCP_NODELAY` metadata remain cached on the endpoint until the last FD
  reference is closed.

## Coexistence with Kernel Networking

The legacy kernel client (`dns`, `http`, `https`) and user-space lwIP share the
RTL8139 driver via separate ephemeral port ranges:
- Kernel TCP: `49153..51999`
- User-space lwIP TCP: `52000..60999`
- User-space lwIP DNS: random source ports in `61000..61999`

Incoming frames are demultiplexed by protocol and destination port, while ARP
frames are delivered to both stacks. `net-config` only changes the `/24` IPv4
settings when both stacks are completely idle. Both stacks require a valid
`fw_cfg` entropy seed for ISNs and DNS source ports and fail closed without one.

## Syscall Wire ABI (`INT 0x80`)

Arguments are passed in `EBX`, `ECX`, `EDX`, and `ESI` (for 4-argument
`SYS_SOCKET_SEND` and `SYS_SOCKET_RECV`):

| Number | Syscall | `EBX` / `ECX` / `EDX` / `ESI` |
| --- | --- | --- |
| `26` | `SYS_SOCKET_OPEN` | `domain` / `type` / `notos_flags` / — |
| `27` | `SYS_SOCKET_CONNECT` | `fd` / `notos_socket_address *` (8 bytes) / — / — |
| `28` | `SYS_SOCKET_SEND` | `fd` / `buffer` / `length` / `flags` |
| `29` | `SYS_SOCKET_RECV` | `fd` / `buffer` / `length` / `flags` |
| `30` | `SYS_SOCKET_GETOPT` | `fd` / `option` / `u32 *out_value` / — |
| `31` | `SYS_SOCKET_SETOPT` | `fd` / `option` / `u32 value` / — |
| `32` | `SYS_SOCKET_SHUTDOWN` | `fd` / `how` (`0..2`) / — / — |
| `33` | `SYS_SOCKET_ADDRESS` | `fd` / `notos_socket_address *out` / `peer` (`0..1`) / — |
| `34` | `SYS_NET_RESOLVE` | `const char *hostname` / — / — / — |
| `35` | `SYS_NET_RESOLVE_RESULT` | `token` / `u8 out_ipv4[4]` / — / — |

`struct notos_socket_address` is an 8-byte wire structure (`u8 ipv4[4]` followed
by native-endian `u32 port`).

## Diagnostics & Verification

```text
sockets
socket-run echo PORT
socket-run http PORT
socket-run dns PORT
socket-run web 80
socket-drop syn
socket-drop tx
socket-drop rx
```

`tests/sockets.py` tests 16 MiB and 64 MiB configurations: 32,768-byte binary
echo streams with partial transfers, `MSG_PEEK`, half-close/EOF, HTTP-to-`tmpfs`
downloads, 4 concurrent clients, `SOCK_NONBLOCK`/`EINPROGRESS`, refused and RST
connections with pending data, timeouts, `kill` cleanup, SYN/TX/RX packet loss
recovery, >8 sequential connections with `TIME_WAIT` recycling, and DNS
`A`/`NXDOMAIN`/cancellation against a local UDP 53530 fixture (`notos-socket-test.img`).
