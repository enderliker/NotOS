# NotOS

NotOS is a 32-bit x86 educational operating system prototype written from
scratch for PC BIOS and QEMU. It features its own BIOS bootloader and
freestanding kernel: it does not use Linux, another OS kernel, or a base
distribution.

Third-party components are pinned by SHA-256 and preserve their upstream
licenses without claiming original authorship:
- **TLS 1.2 / 1.3 & X.509**: MbedTLS 3.6.5 (`Apache-2.0`)
- **Entropy DRBG / SHA-256**: BearSSL 0.6 (`MIT`)
- **User-space C library**: Picolibc 1.8.11 (upstream per-file BSD/MIT/Newlib
  licenses, with a custom NotOS syscall port)
- **User-space TCP/IPv4 & DNS**: lwIP 2.2.1 (`BSD-3-Clause`, raw `NO_SYS=1`
  integration)
- **User-space HTTP/HTTPS client**: curl 8.11.1 (`curl` license)

GCC, binutils, Meson, Ninja, Python, OpenSSL, and QEMU are host build/test tools
and are not part of the guest runtime.

## Implemented & Verified Subsystems

| Milestone | Implementation | Verification |
| --- | --- | --- |
| Bootloader | Custom 512-byte BIOS boot sector, raw image, A20, GDT, protected mode, and high-memory payload copy | Real C kernel boot and signature checks in QEMU |
| Graphics & UI | VGA Mode 13h (320×200), PS/2 mouse, 8×8 cursor, toolbar, window minimize/restore, and shell line editing | Framebuffer pixel captures, QMP mouse/keyboard events, and CSI/PS/2 editing |
| Language | NotLang 0.1 kernel interpreter and 0.2 ring 3 bytecode compiler/VM with nested `repeat` and source file `exec` | Arithmetic, loops, error isolation, resource cleanup, and execution across reboots |
| Kernel Networking | RTL8139 PCI/DMA driver, ARP, IPv4, UDP/DNS, TCP/HTTP/HTTPS, exponential backoff retry, and `/24` static config | Local NAT, injected SYN/GET/SYN-ACK/response drops, public DNS/HTTP/HTTPS, TLS 1.2/1.3, X.509, AEAD tamper, and invalid certs |
| Exceptions | Custom 256-entry IDT, uniform x86 trap frame, and fatal kernel panic screen | Recoverable `INT3` and real CPU faults (`#DE`, `#UD`, `#GP`, `#PF`) |
| Interrupts | 8259 PIC remap, PIT timer on IRQ0 (~1000 Hz), and PS/2 keyboard/mouse on IRQ1/IRQ12 | `HLT` wakeups, clock progress during foreground work, spurious IRQ paths, and PS/2 queueing |
| Physical Memory | BIOS E820 memory map parser and 4 KiB frame bitmap allocator (up to 64 MiB) | Pool exhaustion, boundary writes, and full recovery in QEMU with 16/32/64/128 MiB RAM |
| Paging | PAE (64-bit descriptors), `CR0.PG`/`CR0.WP`, `EFER.NXE` (NX), and strict `W^X` | Virtual aliases, `INVLPG` TLB shootdown, and real `#PF` faults with `CR2` verification |
| Kernel Heap | `kmalloc`/`kcalloc`/`kfree`, block split/coalesce, 16-byte alignment, and guard pages | 20,000 native randomized ops (ASAN/UBSAN) and live kernel heap stress tests |
| ELF32 & User VM | `PT_LOAD` `W^X` segments, zeroed BSS, 64 KiB user stack, `brk`, private anonymous `mmap`, and `SYS_EXEC` | Sanitizer-fuzzed parser (50,000 mutations), ring 3 execution, PAE table crossings, NX faults, 4-way concurrency, and OOM rollback |
| User-space Libc | Static Picolibc 1.8.11, custom CRT, `sbrk` heap, integer `stdio`, private `errno`, 64-bit time, and `getentropy` | Ring 3 ELF suite, 4 concurrent processes, OOM preservation, `atexit`/stdio flush, and exact frame/heap recovery |
| Ring 3 Isolation | Custom GDT/TSS, per-task private `CR3`, `INT 0x80` syscall gate, and validated user buffers | Real CPL3 execution, isolated virtual addresses, recoverable user faults, and CPU tick budget enforcement |
| Process Waiting & Composition | `SYS_SLEEP`, `SYS_SPAWN` (`fd_map`/`envp`), `SYS_WAIT` (`WNOHANG`/zombies), `SYS_GETPPID`, `SYS_KILL`, and `TASK_BLOCKED` | 2-stage 16 KiB pipe pipelines, zombie/orphan reaping, kill of pipe/sleep/socket waiters, slot exhaustion (`EAGAIN`), and 32-bit clock wrap |
| VFS, Pipes & File Descriptors | Writable `tmpfs` and `NotFS v3`, immutable `/system`, 4 KiB kernel pipes (`SYS_PIPE`), per-process FDs/cwd, directories, `rename`, `dup`/`lseek`, and blocking `stdin` | Ring 3 binary stdio, pipe wrap/EOF/`EPIPE`/`poll`, atomic `rename`, `dirent`, quotas/OOM rollback, 4 concurrent writers/readers, `mkfs`/v1->v2->v3 migration, and leak-free `kill`/fault cleanup |
| User-space Sockets | lwIP TCP/IPv4, partial BSD socket API, async DNS (`getaddrinfo`), and per-process blocking | 32 KiB binary streams, HTTP-to-tmpfs downloads, 4 concurrent clients, packet drops, `TIME_WAIT` recycling, and cancellation |
| I/O Multiplexing | `poll` (up to 64 entries) and `select` adapter over sockets, pipes, files, and `stdin` | Multi-peer TCP/pipe/stdin readiness, relative/infinite timeouts, TX backpressure, 4 blocked waiters, and cancellation |
| User-space `curl` | Upstream curl 8.11.1 HTTP/HTTPS in ring 3 linked with user-space MbedTLS 3.6.5 and Picolibc | Binary HTTP/HTTPS downloads & POST uploads, X.509/SAN/EKU checks, `--cacert`, DNS, timeouts, and VFS ELF download + `exec` |
| Persistent Storage | 28-bit ATA PIO driver and dual-bank transactional `NotFS v3` on a dedicated data disk | Reboots, CRC corruption fallback, v1/v2-to-v3 migration, mid-commit fault injection, bounded inode recycling, and persistent directory/file CRUD |
| Preemptive Multitasking | IRQ0 round-robin scheduler, four task slots, 16 KiB private kernel stacks, and `SYS_YIELD` | Register/flag preservation, mixed batches, 250-tick CPL3 budget, `#NM` FPU rejection, and OOM rollback |

NotOS is an educational laboratory kernel and is not a general-purpose desktop
OS or hardened for hostile production environments.

## Building, Testing, and Running

**Target CPU**: 32-bit x86 with PAE, MSR, and NX support (QEMU `-cpu max`),
tested in QEMU TCG emulation. The kernel verifies CPUID capabilities before
enabling PAE/NXE, and all guest code is compiled without FPU/MMX/SSE
instructions.

**Host Prerequisites**: GNU Make 4.3+, GCC with 32-bit code generation (`-m32`,
no 32-bit host libc required), GNU binutils, Meson, Ninja, Python 3.12+, OpenSSL
(for ephemeral TLS test fixtures), and `qemu-system-i386`. On Ubuntu/Debian:

```sh
sudo apt-get update
sudo apt-get install gcc binutils make python3 meson ninja-build openssl qemu-system-x86
make
make test
make test-online  # optional: requires outbound DNS, HTTP, and HTTPS access
make run
```

`make run` launches QEMU with a graphical window and serial console attached to
your terminal. On a headless machine, run `python3 tools/run.py -display none`.
The launcher automatically creates and preserves `data.img` as a separate
primary-slave IDE data disk; run `mkfs` inside the NotOS shell only when you
explicitly want to format and erase that data disk (see
[docs/STORAGE.md](docs/STORAGE.md)). The bootable image is `build/notos.img`.
You can override the QEMU binary via the `QEMU` environment variable.

The first build downloads BearSSL, MbedTLS, Picolibc, lwIP, and curl into
`.cache/` and verifies their pinned SHA-256 hashes before extraction. For
offline builds, pass local verified tarballs via `BEARSSL_ARCHIVE`,
`MBEDTLS_ARCHIVE`, `PICOLIBC_ARCHIVE`, `LWIP_ARCHIVE`, and `CURL_ARCHIVE`.

The GitHub Actions workflow runs `make clean test` and uploads test artifacts.

## Shell Commands

```text
help
about
clear
echo Hello NotOS
run let x = (2 + 3) * 4; print x
run print x - 7
run-user let n = 1; repeat 3 { let n = n * 2; }; print n
write demo.nl print 42
exec demo.nl
lang-reset
net
dns example.com
http example.com
https example.com
tls
tls-selftest
http 10.0.2.2:8080
net-config 10.0.2.15 10.0.2.2 10.0.2.3
uptime
sleep 200
busy 250
irqs
int3
mem
vm-test
heap
alloc 128
free 0
heap-test
ring3-test
multitask-test
libc-test
libc-run exit
libc-run files
libc-run stdin
libc-run pipeline
vfs
sockets
socket-run web 80
socket-run poll-basic 1
socket-run poll-web 80
curl --version
curl -sS -o /tmp/page.txt https://example.com/
ls /tmp
cat /tmp/page.txt
cd /tmp
pwd
touch empty.txt
mv empty.txt renamed.txt
stat renamed.txt
rm renamed.txt
df
feed 1 hello-vfs
feed-eof 1
elf-run demo
spawn-elf /system/libc.elf test
spawn worker
spawn spin
ps
kill 2
mkfs
write hello.txt Hello
cat hello.txt
ls
rm hello.txt
multitask-run four
multitask-run yield
multitask-run fault
multitask-run budget
multitask-run fpu
user-run write
user-run bad-buffer
user-run kernel-read
```

Press `F1` for help and `F2` to clear the terminal. Input is accepted via the
PS/2 keyboard or `COM1` serial port, with an 8-entry command history, cursor
movement (`Left`/`Right`/`Home`/`End`/`Delete`), and `Tab` completion for shell
commands and VFS paths.

## System Design Overview

The BIOS loads our 512-byte boot sector (`boot/boot.S`). The loader reads a
512 KiB low kernel image into `0x10000..0x90000` (16 blocks of 64 sectors) and
copies an additional 768 KiB high payload (`0x100000..0x1c0000`) through a
32 KiB bounce buffer using BIOS `INT 15h / AH=87h` before enabling A20, loading
the GDT, selecting VGA Mode 13h, and entering 32-bit protected mode. End-of-load
signatures at `0x8fffc` and `0x1bfffc` validate both regions. The kernel entry
(`kernel/entry.S`) sets the initial stack at `0x9f000` and zeroes BSS starting
at `0x1c0000` before calling `kernel_main`. The physical frame allocator parses
the BIOS E820 map, reserves the first MiB and the high static/BSS region up to
`__bss_end` (`< 0x280000`), and manages up to 64 MiB of physical RAM.

The kernel runs in ring 0 with PAE paging (`CR4.PAE`, `EFER.NXE`, `CR0.PG`,
`CR0.WP`), enforcing `W^X` permissions (RX text, R/NX rodata and embedded
system images, RW/NX data/heap/stacks, unmapped null page, and stack/heap guard
pages) and a 1 MiB virtual kernel heap at `0xd0001000`. The 8259 PIC is
remapped to vectors `32..47`, the PIT drives `IRQ0` at ~1000 Hz, and PS/2
keyboard/mouse interrupts (`IRQ1`/`IRQ12`) enqueue raw bytes for deferred
processing in the main loop. Networking uses a custom PCI/DMA driver for the
emulated RTL8139 NIC.

## Architectural Limits

- **Platform**: PC BIOS / 32-bit x86 in QEMU only; no UEFI or verified physical
  hardware support.
- **Processes & Memory**: Up to four concurrent tasks/processes (both for
  synchronous diagnostic batches and background managed processes, including
  unreaped `PROCESS_ZOMBIE` slots), private PAE address spaces and 16 KiB kernel
  stacks, a fixed 1 MiB kernel heap, and a 64 MiB physical memory cap. No `fork`,
  swap, ASLR, SMP, threads, or kernel preemption. FPU/MMX/SSE instructions are
  disabled (`CR0.TS=1`).
- **Fault Handling**: User-space exceptions terminate only the faulting process
  and return cleanly to the shell; kernel-mode exceptions halt the CPU with a
  diagnostic panic screen. No separate TSS for double faults.
- **VFS & Storage**: 16 FDs per process, 64 global open file descriptions, 16
  anonymous 4 KiB kernel pipes, `tmpfs` (up to 64 KiB per file, 256 KiB total
  RAM quota), and dual-bank transactional `NotFS v3` on the secondary IDE disk
  (up to 48 nodes and 88 KiB total payload, single-user stored `mode`/`uid`/`gid`
  without permission enforcement).
- **Networking & TLS**: Static `/24` IPv4 configuration, one active request at a
  time on the legacy kernel client, and up to eight user-space TCP endpoints and
  four concurrent DNS queries via lwIP. Strict TLS 1.2/1.3 and X.509 validation
  requiring a fresh `fw_cfg` entropy seed and valid CMOS UTC clock (`-k` does
  not bypass certificate validation).
- **User-space Libc & `curl`**: Single-threaded static Picolibc 1.8.11 port with
  integer `stdio`, `sbrk` heap (up to 4 MiB per process), software `binary64`
  helpers, `poll`/`select`, directory syscalls and `dirent` (`opendir`/`readdir`/
  `closedir`/`rewinddir`), `rename` (`SYS_RENAME`), `notos_exec` (`SYS_EXEC`),
  `notos_spawn`/`posix_spawn` (`SYS_SPAWN`), `wait`/`waitpid` (`SYS_WAIT`),
  `pipe`/`pipe2` (`SYS_PIPE`), `kill` (`SYS_KILL`), and `environ`/`getenv`/`setenv`.
  Upstream `curl` 8.11.1 supports HTTP and HTTPS in ring 3 with default trust
  bundle `/system/cacert.pem`.

## Documentation Index

- [Architecture and Module Boundaries](docs/ARCHITECTURE.md)
- [User-Space Execution (Ring 3)](docs/USERSPACE.md)
- [Preemptive Multitasking and Scheduler](docs/TASKS.md)
- [Background Processes and Blocking Waits](docs/PROCESSES.md)
- [ELF32 Loader, Private VM, and `SYS_EXEC`](docs/ELF.md)
- [User-Space C Library (Picolibc Port)](docs/LIBC.md)
- [VFS, File Descriptors, and Directories](docs/VFS.md)
- [Persistent Storage and NotFS v3](docs/STORAGE.md)
- [User-Space TCP/IPv4 Sockets and DNS](docs/SOCKETS.md)
- [I/O Multiplexing (`poll` and `select`)](docs/POLL.md)
- [Physical Memory, PAE/NX Paging, and Heap](docs/MEMORY.md)
- [Interrupts, PIC/PIT, and Exceptions](docs/INTERRUPTS.md)
- [Kernel Networking (RTL8139, TCP/IP, HTTP)](docs/NETWORK.md)
- [HTTPS / TLS 1.2 & 1.3 and X.509 Policy](docs/TLS.md)
- [Upstream `curl` 8.11.1 Port](docs/CURL_PORT.md)
- [Mouse, VGA UI, and Shell Line Editing](docs/UI.md)
- [NotLang 0.1 Kernel Interpreter](docs/NOTLANG.md)
- [NotLang 0.2 Ring 3 Compiler and VM](docs/NOTLANG_USER.md)
- [Verification Log and Test Suites](docs/TESTING.md)
- [Roadmap and Next Milestones](docs/ROADMAP.md)
