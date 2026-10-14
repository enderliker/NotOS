# Architecture and Module Boundaries

NotOS is a freestanding 32-bit x86 educational kernel. Execution layers and
responsibilities are strictly separated:

| Module | Responsibility | Must Not Do |
| --- | --- | --- |
| `arch.*`, `user_entry.S`, `isr.S` | GDT/TSS, CPU ABI, privilege entry/return, and interrupt stubs | Interpret shell commands or application policy |
| `physical*`, `paging*`, `space*`, `heap*` | Physical frames, PAE/NX page tables, address spaces, and kernel heap | Decide task scheduling policy |
| `task.*` | Task lifecycle, resources, state transitions, statistics, and teardown | Print demo results or own shell commands |
| `scheduler_policy.*` | Pure round-robin selection logic (host-tested) | Touch hardware registers, I/O, or memory allocators |
| `scheduler.*`, `process.*` | Batch and background task rotation, quantums, blocking waits, PIDs, and reaping | Allocate or free tasks inside IRQ handlers |
| `syscall.*` | `INT 0x80` ABI, user buffer validation, and syscall dispatch | Implement shell commands |
| `vfs.*`, `fd.*`, `fs.*`, `disk.*` | VFS nodes, open file descriptions, transactional NotFS v3, and ATA PIO | Interpret unvalidated user pointers or schedule tasks |
| `net.*`, `socket_stack.*`, `poll_wait.*`, `tls.*` | RTL8139 DMA, lwIP `NO_SYS=1` integration, `poll` readiness, and kernel TLS | Run inside IRQ context or bypass user buffer checks |
| `user.*`, `multitask.*`, `*_command.*` | Shell commands, diagnostic demos, and test harnesses | Directly handle hardware interrupts |
| `syscall_numbers.h`, `*_abi.h` | Wire constants and layouts shared between kernel, assembly, and user space | Expose C-only types to assembly (`.S`) |

## Core Execution & Scheduling Model

Each task owns a private PAE `CR3` (`PDPT` + page directory) and a 16 KiB kernel
stack with a low-end stack canary. On `IRQ0`, the handler sends PIC EOI before
selecting the next context; the C dispatcher returns the trap frame pointer to
restore in assembly (`isr.S`). Context switches update both `CR3` and `TSS.ESP0`.
The kernel preserves its entry frame and restores the supervisor `CR3` whenever
a batch finishes or a background pulse yields back to the main event loop.

- **Quantum**: 5 PIT `IRQ0` ticks delivered in CPL3.
- **CPU Budget**: 250 CPL3 `IRQ0` ticks per normal task (2,500 ticks for
  `/system/curl.elf` to accommodate TLS handshakes and CA parsing). Budgets
  measure sampled CPL3 ticks, not exact wall-clock or kernel CPU time.
- **Capacity**: Up to four concurrent tasks/processes. No SMP or kernel
  preemption.
- **FPU/SIMD**: `CR0.TS` is set to trap x87/MMX/SSE instructions (`#NM`); no
  FPU/SIMD state is saved or restored.

## Subsystem Architecture

### Background Processes, Storage, UI, and NotLang VM

- `process.*`: Owns the four background process slots, monotonic 32-bit PIDs,
  timeout/I/O wakeups (`process_poll`), and deferred reaping. `scheduler_pulse`
  runs one ready background process for a single quantum/yield/block and returns
  to the kernel event loop so networking, serial, keyboard, and mouse remain
  responsive.
- `disk.*` / `fs_core.*` / `fs.*`: Separates 28-bit ATA PIO primary-slave
  transport from pure CRC32/layout/compaction logic (`fs_core.*`) and dual-bank
  transactional publishing (`fs.*`). The boot drive is never written.
- `mouse_decode.*` / `mouse.*` / `gui.*`: Separates pure 3-byte PS/2 packet
  decoding from IRQ12 queueing and deferred VGA framebuffer rendering.
- `line_core.*` / `line_input.*`: Separates pure command-line editing and
  8-entry history from PS/2 keyboard, serial CSI/SS3 escape decoding, and VFS
  `Tab` completion.
- `notlang_compile.*` / `notlang_command.*` / `user/notlang_vm.c`: Compiles
  NotLang 0.2 source into bounded bytecode (`notlang_bytecode.h`) and executes
  it inside an isolated ring 3 VM address space.
- `tcp_retry.*`: Pure sequence/ACK/exponential backoff policy separated from the
  RTL8139 driver and legacy TCP state machine.

### PAE/NX Memory & TLS 1.2 / 1.3

- **PAE + NX**: Uses a 4-entry PDPT and 512-entry 64-bit page directories/tables
  with 4 KiB frames. Kernel text is RX, rodata and embedded system blobs are
  R/NX, and data/heap/stacks are RW/NX. 64-bit page descriptors write the upper
  32 bits before publishing the lower present word.
- **TLS (`tls.*`, `tls_profile.*`, `certificate_policy.*`)**: Configures
  MbedTLS 3.6.5 for TLS 1.2 and TLS 1.3 over non-blocking BIOs, enforces strict
  X.509 SAN/EKU/key-size policies via the sanitizer-tested DER parser
  (`certificate_policy.*`), and releases all contexts and PSA key slots on
  teardown. `entropy.*`, `rtc.*`, and `utc.*` provide fail-closed platform
  primitives; `integer_runtime.*` provides verified 64-bit division for `i386`.

### ELF32, Private VM, and Process Replacement (`SYS_EXEC`)

- `elf.*` validates ELF32 `ET_EXEC` images without allocating memory.
- `space.*` loads `PT_LOAD` segments with strict `W^X` permissions, manages
  private frames/PTEs, `brk` heap (`0x08400000..0x08800000`), and private
  anonymous `mmap` (`0x08800000..0x08c00000`). Buffer validation walks 2 MiB PAE
  PDE boundaries, and teardown reclaims user frames by walking page tables.
- `system_file.*` exposes immutable embedded images (`/system/curl.elf`,
  `/system/libc.elf`, `/system/cacert.pem`), and `executable.*` loads VFS
  binaries without duplicating `/system` blobs on the kernel heap.
- `SYS_EXEC` (`37`) prepares and validates the new ELF image, address space, and
  argument stack before committing, switches `CR3` before destroying the old
  address space, closes `FD_CLOEXEC` descriptors via `fd.*`, and preserves PID,
  `cwd`, and open file descriptions.

### User-Space Libc (Picolibc 1.8.11)

- `user/libc/crt0.S` and `start.c` provide the C runtime startup; `port.c`,
  `socket.c`, and `poll.c` translate wire ABIs (`platform_abi.h`, `file_abi.h`,
  `socket_abi.h`, `poll_abi.h`) and map kernel error codes to Picolibc `errno`.
- `argument_core.*` splits space-delimited command strings (up to 256 bytes and
  16 `argv` elements) without dynamic allocation.
- `user/float_runtime.c` implements software `binary64` helpers using pure
  integer arithmetic so `curl` runs without hardware FPU/SSE or `libgcc`.

### VFS, File Descriptors, Sockets, and `poll`

- `vfs.*` owns `tmpfs`, writable `NotFS v3`, `/system`, and `/dev` nodes; `fd.*`
  owns open file descriptions, reference counts, offsets/flags, and per-task
  256-byte `stdin` queues.
- `socket_stack.*` wraps raw lwIP (`NO_SYS=1`) with 8 TCP endpoints and 4
  concurrent DNS queries, sharing the RTL8139 NIC with the legacy kernel stack
  via port range demultiplexing.
- `poll_wait.*` and `socket_events.h` evaluate non-consuming readiness across up
  to 64 descriptors per `poll` call. Blocked reads, socket calls, sleeps
  (`wait_policy.h`), and `poll` waits suspend only the calling process
  (`TASK_BLOCKED` / `TASK_WAIT_*`) and complete via supervisor physical aliases
  in `process_poll` without switching `CR3` inside IRQ context.

## Architectural Rules

- Guard all headers (`NOTOS_<NAME>_H`), keep internal symbols `static`, and
  share ABI constants cleanly between C and assembly.
- Never allocate memory, print lengthy diagnostics, or interpret user programs
  inside IRQ handlers.
- Validate the entire range and page permissions of user buffers before reading,
  writing, or mutating kernel state; preserve `EFLAGS.IF` across critical
  sections.
- Destroy resources only when they are no longer active, and roll back partial
  allocations on failure.
- Keep pure algorithms native-testable under ASAN/UBSAN and verify full-system
  behavior by booting real disk images in QEMU.
