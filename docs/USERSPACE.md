# User-Space Execution (Ring 3)

## Privilege Architecture (`kernel/arch.c`, `kernel/user_entry.S`, `kernel/isr.S`)

NotOS defines a custom GDT with supervisor code/data (`0x08` / `0x10`), user
code/data (`0x1b` / `0x23`, `RPL=3`), and a 32-bit Task State Segment (`0x28`).
`LTR` loads the TSS at boot, and `TSS.ESP0` points to the active task's 16 KiB
supervisor stack for privilege transitions (`CPL 3 -> CPL 0`). The I/O
permission bitmap base is set beyond the TSS limit so ring 3 tasks cannot
execute `IN`/`OUT` port instructions (`#GP`).

Only interrupt gate **`INT 0x80`** has `DPL=3`; hardware interrupt and exception
gates have `DPL=0` so user code cannot spoof hardware IRQs via software `INT n`
instructions. Entering ring 3 uses `IRET` with `IF=1`, `IOPL=0`, and zeroed
general-purpose registers.

## Private Address Spaces (`kernel/space.c`)

Every task executes under its own private PAE `CR3` (`PDPT` + page directory).
Supervisor mappings are shared with `U/S=0` (supervisor-only), while the kernel's
master page directory contains no user-accessible mappings.

### Compact Demo & NotLang VM Layout

Single-page ring 3 diagnostic demos and the NotLang 0.2 VM use five private
frames (PDPT, page directory, page table, code, data, and stack):

| Virtual Address | Permissions & Purpose |
| --- | --- |
| `0x08000000` | User code (`RX`, `U/S=1`) |
| `0x08001000` | User data (`RW/NX`, `U/S=1`) |
| `0x08002000` | Unmapped guard page |
| `0x08003000` | User stack (`RW/NX`, `U/S=1`, 4 KiB) |
| `0x08004000` | Unmapped upper guard |

Full ELF32 applications use a 16 MiB user virtual region (`0x08000000..0x09000000`)
with demand-allocated PAE page tables, a 4 MiB `brk` heap, a 4 MiB anonymous
`mmap` region, and a 64 KiB user stack (see [ELF.md](ELF.md)).

## Core `INT 0x80` Syscalls

The syscall number is passed in `EAX` and the return value (or negative error
code) is placed in `EAX`:

| Number | Operation | Arguments |
| --- | --- | --- |
| `0` | `SYS_EXIT` | `EBX` = exit status |
| `1` | `SYS_TICKS` | Returns global monotonic uptime in milliseconds |
| `2` | `SYS_PROBE` | Verifies `CPL=3`, `SS=0x23`, active `TSS.ESP0`, and private `CR3` |
| `3` | `SYS_WRITE` | `EBX` = user buffer, `ECX` = length (`0..160` bytes) |
| `4` | `SYS_REPORT` | Records isolation-demo verification values |
| `5` | `SYS_YIELD` | Yields the current quantum to the next `TASK_READY` slot; returns `0` |

Unknown syscall numbers return `-ENOSYS` (`-38`). `SYS_WRITE` validates the full
buffer address range and PAE PDE/PTE permissions before dereferencing any user
byte, returning `-EFAULT` (`-14`) for kernel, unmapped, or guard addresses and
`-EINVAL` (`-22`) for lengths above 160 bytes. Additional syscalls for ELF VM
(`6..9`), libc platform hooks (`10..13`), VFS and pipes (`14..25`, `37..42`,
`46`, `48`), sockets/DNS (`26..35`), `poll` (`36`), and process composition
(`43..45`, `47`, `49`) are documented in [ELF.md](ELF.md), [LIBC.md](LIBC.md),
[VFS.md](VFS.md), [SOCKETS.md](SOCKETS.md), [POLL.md](POLL.md), and
[PROCESSES.md](PROCESSES.md).

## Fault Isolation & Diagnostic Commands

```text
ring3-test
user-run demo
user-run write
user-run bad-buffer
user-run kernel-read
user-run code-write
user-run io
user-run guard
user-run gate
user-run foreign-read
user-run spin
```

Unlike the ring 0 `fault <kind>` commands, all `user-run` fault demos trap
cleanly from CPL 3, terminate only the user task (`#PF` error 5 on
`kernel-read` and `foreign-read`, `#PF` error 7 on `code-write`, `#GP` error 0
on `io`, `#PF` error 6 on `guard`, `#GP` error 258 on `gate` `INT 0x20`, and
exit status `124` on `spin`), restore the kernel `CR3` and all physical frames,
and return to the interactive shell.
