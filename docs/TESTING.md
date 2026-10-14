# Verification Log and Test Suites

## Running the Test Suites

| Command | Coverage |
| --- | --- |
| `make` | Builds `build/notos.img` and test fixture images with pinned dependencies |
| `make test` | Runs all 24 native C unit tests and 27 full-system QEMU integration suites |
| `make clean test` | Full clean rebuild and complete test suite (used by GitHub Actions CI) |
| `make test-online` | Optional live internet tests against public DNS, HTTP, and HTTPS (`example.com`) |

Native host unit tests for parsers, memory allocators, VFS, NotFS v3, shell line
editing, shell command/pipeline parsing, `poll`, X.509 certificate policy, and
software `binary64` arithmetic compile with **AddressSanitizer and
UndefinedBehaviorSanitizer (`ASAN/UBSAN`)**.
In QEMU integration suites, HTTP, TCP, DNS, and TLS servers run on the host only
as external network peers, while the entire client stack, TLS cryptography,
libc, and kernel execute inside the NotOS guest.

Test logs, PPM screen captures, and optional packet captures (`QEMU_PCAP=1`) are
written to `build/` (excluded from Git). Ephemeral TLS fixture private keys are
generated inside `build/` and excluded from CI artifact uploads.

## QEMU Monitor (QMP) Synchronization

The Python test harness (`tests/common.py`) waits up to 10 seconds for the QEMU
QMP UNIX socket to accept connections, retrying `ENOENT` and `ECONNREFUSED`
until the monitor is ready rather than relying on fixed sleeps.

## Historical Milestone Verification Log

The sections below preserve the verification evidence recorded as each milestone
was completed. For current subsystem contracts and limits, see the module guides
in `docs/` and [ROADMAP.md](ROADMAP.md).

---

### Bootloader Milestone

Verified locally with GCC, GNU binutils, and QEMU (`-M pc`, TCG emulation,
32 MiB RAM):
- 512-byte BIOS boot sector with `55 aa` signature.
- Raw kernel image loaded from disk into low memory.
- Real transition to 32-bit protected mode and C kernel entry.
- Serial output on `COM1`: `BOOT: protected mode OK` and `BOOT: kernel C OK`.

### Graphics & VGA UI Milestone

`make clean test` — PASS:
- VGA Mode 13h (`320×200`) framebuffer verified pixel-by-pixel via QMP screen
  captures (`build/desktop*.ppm`): desktop background, window frame, and title
  bar.
- Commands accepted over both `COM1` serial and emulated PS/2 keystrokes
  (`echo kbd`), plus `F1` help and `F2` terminal clear.

### NotLang 0.1 Milestone

`make clean test` — PASS:
- Native unit tests for expression parsing, 32-bit signed integer bounds,
  16-variable storage, and syntax/runtime errors.
- In-kernel execution in QEMU: operator precedence, variable persistence across
  `run` commands, division by zero, signed overflow, undefined variables,
  unbalanced parentheses, and nesting depth limits, followed by `lang-reset`.
- Explicitly enforced `-mno-sse -mno-sse2 -mno-mmx -msoft-float` so host GCC
  never emits SSE instructions in freestanding code.

### Kernel Networking Milestone

`make clean test` and `make test-online` — PASS:
- Native unit tests for IPv4/UDP/TCP checksums, IPv4 address parsing, and
  bounded DNS packet parsing.
- Emulated RTL8139 PCI/DMA, ARP, IPv4, UDP/DNS, and TCP/HTTP against a
  controlled host server over QEMU user-mode NAT: three sequential connections,
  7,686-byte HTTP body, split HTTP status line, RX ring wrap-around, and fresh
  ephemeral source ports per connection.
- `make test-online`: Live DNS resolution and `HTTP/1.1 200 OK` from
  `example.com`.

### Phase 2: IDT, Exceptions, PIC/PIT, and PS/2 Keyboard

`make clean test` and `make test-online` — PASS:
- 256-entry IDT, uniform trap frame, and recoverable `INT3` preserving all
  general-purpose registers, `CF`, `DF`, and `IF` (`DF` cleared before C
  handlers).
- Real CPU exceptions (`#DE` vector 0, `#UD` vector 6, `#GP` vector 13 with
  selector error `0x30`) render the diagnostic panic screen (`build/panic-*.ppm`)
  and halt cleanly without triple-faulting.
- 8259 PIC remapped to `32..47`, PIT at ~1000 Hz (`IRQ0`), `HLT`-based `sleep`,
  spurious `IRQ7`/`IRQ15` checks, and `IRQ1` PS/2 keyboard queueing verified
  during `busy 1000`.
- Native verification of exact fractional PIT remainder math across 1,000,000
  ticks and 32-bit unsigned rollover.

### Phase 3: BIOS E820 Physical Memory, Paging, and Kernel Heap (NotOS 0.6)

`make clean test` and `make test-online` — PASS:
- Real BIOS E820 map parsing and 4 KiB frame bitmap allocator verified in QEMU
  with 16, 32, 64, and 128 MiB RAM (enforcing the 64 MiB physical cap).
- `frame-test` exhausts all free frames, writes page boundaries, rejects
  invalid/double frees, and restores the exact free frame count.
- Paging with `CR0.PG` and `CR0.WP`, read-only kernel `.text`/`.rodata`, dynamic
  supervisor virtual window (`0xd0000000`), `INVLPG` TLB invalidation, and real
  `#PF` panic tests (`null`, `readonly`, `guard`, `unmapped`, `heap-end`).
- 1 MiB kernel heap (`0xd0001000..0xd0101000`, 256 frames) with guard pages,
  verified by 20,000 randomized native operations (`tests/heap_unit.c`) and
  in-guest `heap-test`.

### Phase 4: Ring 3 Isolation and Private Address Spaces (NotOS 0.7)

`make clean test` and `make test-online` — PASS:
- Custom GDT/TSS (`0x28`), `CPL=3` entry via `IRET`, `INT 0x80` syscall gate,
  and private per-task `CR3` address spaces.
- Verified memory isolation between two simultaneous user spaces mapped at the
  same virtual address `0x08001000`.
- Recoverable ring 3 faults (`kernel-read` `#PF/5`, `code-write` `#PF/7`, `io`
  `#GP/0`, `guard` `#PF/6`, `gate` `#GP/258`, `foreign-read` `#PF/5`) terminate
  only the user task and return to the shell.
- Full user buffer range and PDE/PTE permission validation in `SYS_WRITE`, plus
  250-tick `IRQ0` budget enforcement (`status=124`).

### Phase 5: Preemptive Round-Robin Multitasking (NotOS 0.8)

`make clean test` and `make test-online` — PASS:
- Pure host-tested round-robin policy (`tests/scheduler_unit.c`) and real
  4-task preemptive batches in 16, 32, and 64 MiB RAM (`tests/multitask.py`).
- Verified register/flag/canary preservation across `IRQ0` preemptions and
  `SYS_YIELD`, fault isolation of a `#PF` task within a 3-task batch, 250-tick
  budget termination (`124`), `#NM` rejection of x87 instructions (`135`), and
  deterministic OOM rollback.

### NotOS 0.9: ATA Storage, Background Processes, Mouse UI, NotLang 0.2, and TCP Retry

`make clean test` and `make test-online` — PASS:
- ATA PIO primary-slave driver and persistent `NotFS` CRUD across QEMU reboots
  with CRC fallback (`tests/storage.py`).
- 4-slot background process manager (`spawn`, `ps`, `kill`) interleaving with
  shell commands and NAT HTTP transfers (`tests/process.py`).
- PS/2 mouse driver (`IRQ12`), 8×8 cursor, toolbar buttons (`CLR`, `FILES`,
  `TASKS`), and window minimize/restore verified via QMP (`tests/mouse.py`).
- NotLang 0.2 bytecode compiler and ring 3 VM (`run-user`, `exec`) with nested
  `repeat` loops and persistent script execution across reboots
  (`tests/notlang.py`).
- Static `/24` IPv4 reconfiguration (`net-config`) and TCP retransmission
  recovery across injected `syn`, `request`, `synack`, and `response` drops
  (`tests/network.py`).

### NotOS 0.10 & 0.11: PAE/NX and HTTPS / TLS 1.2 & 1.3

`make clean test` and `make test-online` — PASS:
- PAE + NX paging (`CR4.PAE`, `EFER.NXE`, `CR0.WP`): ring 3 execution from data
  or stack pages faults with `#PF` error code `21` without crashing the kernel.
- Freestanding MbedTLS 3.6.5 client supporting **TLS 1.2 and TLS 1.3**
  (ECDHE, RSA-PSS, ECDSA, AES-128/256-GCM, ChaCha20-Poly1305, `HelloRetryRequest`),
  seeded by `fw_cfg` via BearSSL HMAC-DRBG/SHA-256 and validated against CMOS
  RTC UTC time (including post-2038 dates).
- Sanitizer-fuzzed X.509 DER policy parser (`tests/cert_unit.c`, 10,000
  mutations).
- Controlled local HTTPS peer tests (`tests/https.py`): rejects untrusted CAs,
  wrong DNS SAN / multi-label wildcards, CN-only certs, wrong EKU, `< 2048`-bit
  RSA, SHA-1 leaf signatures, expired/not-yet-valid certs, truncated EOF without
  `close_notify`, and tampered AEAD ciphertext.

### NotOS 0.12 & 0.13: ELF32, Private VM, and Timed Process Blocking

`make clean test` and `make test-online` — PASS:
- ELF32 `ET_EXEC` loader (`tests/elf_unit.c` with 50,000 mutations;
  `tests/elf.py` in 16/64 MiB RAM): `W^X` segments, multi-page BSS, 64 KiB user
  stack, `SYS_BRK` grow/shrink, 2 MiB + 4 KiB `SYS_MMAP`/`SYS_MUNMAP` crossing
  PAE PDE boundaries, 4 concurrent ELF processes, and OOM rollback.
- Timed process blocking (`SYS_SLEEP=9`, `tests/wait.py`): `TASK_BLOCKED`
  suspends processes for 600 ms without consuming CPL3 budget while shell,
  `INT3`, networking, and sibling workers run; verified 32-bit clock wraparound
  and `EFLAGS` (`NT`/`IOPL`/`VM`) sanitization.

### NotOS 0.14: User-Space Picolibc 1.8.11

`make clean test` and `make test-online` — PASS:
- Static Picolibc 1.8.11 cross-built into `build/sysroot` and verified in
  16/64 MiB RAM (`tests/libc.py`): CRT startup (`argc`/`argv`/`envp`,
  `.init_array`, `atexit`, `.fini_array`, `stdio` flush), 2,500 heap ops, failed
  5 MiB `realloc` preserving data, private `errno`, 64-bit `time_t` (year 2050
  RTC test), bytewise unaligned `SYS_TIME`, and fail-closed `getentropy`.

### NotOS 0.15: VFS and Per-Process File Descriptors

`make clean test` and `make test-online` — PASS:
- Host ASAN/UBSAN suite (`tests/vfs_unit.c`) covering sparse holes, open-unlink
  lifetimes, shared `dup` offsets, quota enforcement, OOM rollback, and 70,000
  create/unlink/close cycles across 16-bit `tmpfs` inode wraparound.
- QEMU VFS suite (`tests/vfs.py` in 16/64 MiB RAM): 8,193-byte binary `stdio`,
  `dup2` redirection, per-PID `stdin` routing (`feed`/`feed-eof`), 4 concurrent
  writers/readers, and leak-free cleanup on `kill` and `#UD` faults.

### NotOS 0.16 & 0.17: User-Space lwIP TCP/DNS Sockets and `poll`

`make clean test` and `make test-online` — PASS:
- Ring 3 TCP/IPv4 and async DNS over lwIP 2.2.1 (`tests/sockets.py` in 16/64 MiB
  RAM): 32,768-byte binary streams, HTTP-to-`tmpfs` downloads, 4 concurrent
  processes, `SOCK_NONBLOCK`/`EINPROGRESS`, refused/RST sockets, SYN/TX/RX drop
  recovery, `TIME_WAIT` PCB recycling across >8 connections, and UDP 53530 DNS
  fixture tests (`A`, `NXDOMAIN`, cancellation, late callbacks).
- Native (`tests/poll_unit.c`, ASAN/UBSAN) and QEMU (`tests/poll.py`,
  `tests/poll_online.py`) verification of `SYS_POLL` (`36`): up to 64 entries,
  multi-peer delayed readiness, non-consuming `SO_ERROR` and `MSG_PEEK`, TX
  backpressure (`EAGAIN` -> `POLLOUT`), `stdin` wakeups, and 4 blocked waiters.

### Ring 3 `curl` 8.11.1 (HTTP/HTTPS), `SYS_EXEC`, `NotFS v2`, and Shell QoL

Verified locally (`make` and `make test` with GCC 15.2.0, binutils 2.46, and
QEMU 10.2.1/TCG) and on GitHub Actions CI (`make clean test`):
- Remote CI runs (`make clean test` — **success**):
  - [Build & QMP synchronization (`38106625143`)](https://github.com/enderliker/NotOS/actions/runs/38106625143)
  - [Libc `select`/`mkdir` & software `binary64` (`38107037072`)](https://github.com/enderliker/NotOS/actions/runs/38107037072)
  - [Upstream `curl` HTTP integration (`38107623839`)](https://github.com/enderliker/NotOS/actions/runs/38107623839)
  - [Ring 3 `curl` HTTPS, `SYS_EXEC`, `NotFS v2`, and kernel audit fixes (`38500119048`)](https://github.com/enderliker/NotOS/actions/runs/38500119048)
- `tests/fs_unit.c` and `tests/line_unit.c` (ASAN/UBSAN): Verify transactional
  `NotFS v2` commit/migration fault injection and shell line editing/history.
- `tests/curl.py` and `tests/curl_https.py`: Verify HTTP and HTTPS (TLS 1.2 /
  1.3) 32,768-byte binary downloads and POST uploads, `--cacert`, X.509
  SAN/EKU/key/signature/expiry enforcement, `-k` rejection, and truncated TLS
  EOF detection in 16 MiB and 64 MiB RAM.
- `tests/continuation.py`: Verifies `spawn-elf`, `SYS_EXEC` (`37`) preserving
  PID/`cwd`/open FDs while closing `FD_CLOEXEC`, PS/2 and serial CSI line
  editing and `Tab` completion, and downloading a real ELF via `curl` to `/tmp`
  and `/disk` and executing it across a reboot.
- `tests/curl_online.py` (`make test-online`): Verifies live public HTTP and
  HTTPS downloads from `example.com` into `/tmp` with complete resource
  restoration.

### Milestone 1: Process Composition & Anonymous Pipes

Verified locally (`make` and `make test` with GCC 15.2.0, binutils 2.46, and
QEMU 10.2.1/TCG):
- `tests/vfs_unit.c` (ASAN/UBSAN): Verifies anonymous 4 KiB kernel pipe ring
  buffer wraparound, partial read/write (`ERROR_AGAIN`), writer close (`EOF` /
  `POLLHUP`), reader close (`ERROR_PIPE` / `POLLERR`), `fd_map` child descriptor
  placement (`NOTOS_SPAWN_FD_CLOSE`, `NOTOS_SPAWN_FD_CLOEXEC`, `dup` sharing),
  and pool exhaustion rollback.
- `tests/pipeline.py` (16 MiB and 64 MiB RAM): Verifies 2-stage ring 3 pipeline
  (`SYS_SPAWN`, `SYS_PIPE`, `SYS_WAIT`) streaming 16,384 bytes (4× the 4 KiB
  pipe buffer) with FNV-1a checksum verification, `envp` inheritance (`getenv` /
  `setenv` / `unsetenv`), `WNOHANG` and zombie reaping, orphan auto-reaping on
  parent exit, `SYS_KILL` (`128 + sig` exit status) waking processes blocked on
  a pipe read, `sleep`, or socket read, 4-slot exhaustion returning `EAGAIN`,
  and exact physical frame, kernel heap, FD, pipe, and socket recovery.

### Milestone 2: NotFS v3, Atomic `rename`, `dirent` Wrappers, and Inode Recycling

Verified locally (`make` and `make test` with GCC 15.2.0, binutils 2.46, and
QEMU 10.2.1/TCG):
- `tests/fs_unit.c` and `tests/vfs_unit.c` (ASAN/UBSAN): Verify dual-bank
  `NotFS v3` (48 nodes, 88 KiB payload, 96 KiB banks at 256-sector stride,
  `mtime`, `mode`/`uid`/`gid`), `NotFS v2` and `v1` read paths, `v2->v3` and
  `v1->v3` bank-1 migration preserving old banks under injected write/flush
  faults, atomic `rename` with mid-commit fault rollback on `/disk`, cross-mount
  `ERROR_CROSS_DEVICE` (`EXDEV`), cycle/ancestor rejection, open-FD snapshot
  preservation when overwriting a destination file, and bounded persistent inode
  recycling across `[256, 65535]`.
- `tests/storage.py`, `tests/files.py`, and `tests/vfs.py` (16 MiB and 64 MiB
  RAM): Verify `NotFS v2` to `NotFS v3` migration across QEMU reboots, 48-node
  capacity, `mtime` and `mode` propagation in `stat`, kernel `mv` and ring 3
  `rename()`/`EXDEV`, and Picolibc `opendir`/`readdir`/`rewinddir`/`closedir`
  with concurrent writers/readers and leak-free fault/kill recovery.

### Milestone 3: User-Space Shell (`/system/sh.elf`), Raw TTY Mode, and Shell Parser

Verified locally (`make clean test` with GCC 15.2.0, binutils 2.46, and QEMU
10.2.1/TCG):
- `tests/shell_parse_unit.c` and `tests/vfs_unit.c` (ASAN/UBSAN): Verify single
  and double quoting, backslash escapes, `$VAR`/`${VAR}` and `$?` expansion,
  elision of unquoted empty variables, pipelines (`|`), input/output/append
  redirections (`<`, `>`, `>>`), background (`&`) and sequential (`;`) job lists,
  syntax/overflow rejection, and `SYS_FCNTL` `NOTOS_F_GETTTY`/`NOTOS_F_SETTTY`
  validation on `stdin`.
- `tests/shell.py`: Launches `/system/sh.elf` in the foreground via `sh`,
  verifies per-job `$VAR`/`$?` expansion, builtins (`cd`, `export`, `unset`,
  `jobs`, `wait`, `exit`), `<`/`>`/`>>` redirections to `/tmp` and `/disk`,
  multi-stage pipelines and exit-status propagation, background job `stdin`
  closure (`NOTOS_SPAWN_FD_CLOSE`), 4-slot exhaustion recovery, and interactive
  raw-mode `line_core` editing (`Tab` completion, cursor movement, and `Up`
  history recall) with exact frame/heap/VFS/FD restoration.
