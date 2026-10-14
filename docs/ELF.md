# ELF32 Applications, Private Virtual Memory, and `SYS_EXEC`

## Shell Commands

```text
elf-test
elf-run demo
elf-run nx-heap
elf-run nx-stack
elf-run stack-guard
elf-run readonly
elf-run sleep
spawn-elf /system/libc.elf test
ps
```

`elf-run` launches **real static 32-bit ELF (`ET_EXEC`)** binaries compiled and
linked separately from the kernel. They execute in **ring 3** with private PAE
page tables, share the four-slot background process manager with NotLang, and
are automatically reaped on exit. The fault variants (`nx-heap`, `nx-stack`,
`stack-guard`, `readonly`) are intentional negative tests that fault and
terminate only the offending user task while the kernel and shell continue
running.

`spawn-elf PATH [ARGS]` loads and spawns an ELF binary from the VFS (`/system`,
`/tmp`, or `/disk`), and `notos_exec` (`SYS_EXEC=37`) replaces the calling
process image in place. (The shell command `exec FILE` retains its original
meaning: compiling and running a NotLang source file.)

## Validation & Loading (`kernel/elf.c`, `kernel/space.c`)

`elf.c` is a pure, bounded parser with no dynamic memory allocation:
- Accepts only 32-bit little-endian System V `ET_EXEC` images for `EM_386`
  (version 1), with a 52-byte ELF header, 32-byte program headers, and at most
  16 program headers.
- Maximum image size is **2 MiB**. The entry point (`e_entry`) must lie within
  the file-backed bytes (`p_filesz`) of an executable `PT_LOAD` segment.
- Every `PT_LOAD` segment must be readable (`PF_R`), satisfy
  `p_filesz <= p_memsz`, lie completely within the file and within the user
  image region (`0x08000000..0x08400000`), and have power-of-two alignment with
  congruent file offset and virtual address modulo 4096.
- Strictly enforces **`W^X`**: rejects any segment that is both writable and
  executable, any page-level overlap between segments, `PT_INTERP`, `PT_DYNAMIC`,
  `PT_TLS`, and executable `PT_GNU_STACK`. Dynamic linking and relocations are
  not used.

Physical frames are allocated only after the entire load plan passes validation.
Segments are copied page-by-page through supervisor physical aliases, with BSS
tails and page padding zeroed. User code pages are mapped RX, rodata R/NX, and
data RW/NX. Any partial allocation failure rolls back all acquired frames and
page tables, restoring the physical frame counter exactly.

## Per-Process Virtual Memory Map

| Region | Virtual Address Range | Permissions & Purpose |
| --- | --- | --- |
| ELF Image | `0x08000000 .. 0x08400000` | `PT_LOAD` segments (`RX`, `R/NX`, `RW/NX`) |
| User Heap (`brk`) | `0x08400000 .. 0x08800000` | Demand-paged via `SYS_BRK`, `RW/NX` (up to 4 MiB) |
| Anonymous `mmap` | `0x08800000 .. 0x08c00000` | Private anonymous pages via `SYS_MMAP`, `RW/NX` (up to 4 MiB) |
| Unmapped Guard | `0x08c00000 .. 0x08ff0000` | Not present (guard gap below user stack) |
| ELF User Stack | `0x08ff0000 .. 0x09000000` | 64 KiB private stack (`RW/NX`) |

Each process owns a private PAE PDPT and page directory, with user page tables
allocated on demand. Address-space teardown walks only the user virtual range
and never frees shared supervisor mappings. Legacy single-page ring 3 demos and
the NotLang VM retain their compact `0x08000000..0x08004000` layout.

## Memory & Process Syscall ABI (`INT 0x80`)

Syscalls pass the number in `EAX` and arguments in `EBX`, `ECX`, `EDX`, `ESI`,
returning the result or negative error code in `EAX`:

- **`6` / `SYS_BRK`**: `EBX` = requested break (`0` queries current break).
  Returns the new break on success, or the unchanged current break on invalid
  range or OOM. Extending rolls back any partially allocated pages on OOM;
  shrinking unmaps and frees complete pages and invalidates the TLB.
- **`7` / `SYS_MMAP`**: `EBX` = byte length. Allocates zeroed, private anonymous
  `RW/NX` pages in `0x08800000..0x08c00000` and returns the base virtual address
  or `-ENOMEM` / `-EINVAL`. Does not take protection/flags/file/hint arguments.
- **`8` / `SYS_MUNMAP`**: `EBX` = page-aligned virtual address, `ECX` = length.
  Unmaps pages in the anonymous region, frees empty page tables, and returns `0`
  or `-EINVAL`.
- **`9` / `SYS_SLEEP`**: `EBX` = `0..60000` ms (`0` yields). Suspends a managed
  process (`TASK_BLOCKED`) until its relative deadline expires without consuming
  CPL3 tick budget.

User buffer validation walks every PAE PDE and PTE across the buffer span,
including 2 MiB PDE boundaries.

## Startup Stack Layout & Arguments

`process_spawn_elf_args` and `process_spawn_elf_split` construct a 16-byte
aligned initial user stack containing bounded argument strings, the `argc`
count, the `argv` pointer array terminated by `NULL`, an empty `envp` (`NULL`),
and an `AT_NULL` auxiliary vector pair. `process_spawn_elf_split` accepts up to
256 bytes of space-separated arguments and up to 16 `argv` entries (including
`argv[0]`). Overlong inputs are rejected before allocation, and any OOM rolls
back all resources without advancing the monotonic PID counter.

## VFS Executables & Process Replacement (`SYS_EXEC = 37`)

`spawn-elf PATH [ARGS]` loads an ELF binary from `/system`, `/tmp`, or `/disk`:
- `/system` provides immutable embedded ELF images (`/system/curl.elf`,
  `/system/libc.elf`), parsed and mapped directly from their read-only memory
  region without duplicating the image on the kernel heap.
- `/tmp` and `/disk` support user-written or downloaded ELF binaries up to
  64 KiB (`NotFS v2` payload limit is 60 KiB).

`notos_exec(path, arguments)` invokes **`SYS_EXEC` (`37`)**:
- Validates and prepares the new VFS ELF image, fresh PAE address space, and
  argument stack (`argv[0]` set to the path basename, up to 256 bytes / 16
  arguments) **before** committing.
- If any check or allocation fails, the calling process retains its existing
  PID, address space, `cwd`, file descriptors, and execution state and receives
  a negative error code.
- On commit, the kernel cancels any pending async DNS tokens for the task,
  switches `CR3` to the new address space before destroying the old one, closes
  only descriptors marked `FD_CLOEXEC`, resets the user trap frame on the task's
  kernel stack, and resumes at the new entry point.
- Standard tasks keep a 250-tick CPL3 budget; `/system/curl.elf` receives a
  2,500-tick budget for TLS/X.509 processing. `SYS_EXEC` never resets `user_ticks`
  and can only raise the budget ceiling when executing `/system/curl.elf`.

## Verification

- **Native parser tests (`tests/elf_unit.c`, ASAN/UBSAN)**: Header/program
  header bounds, `W^X` violations, segment overlaps, alignment/congruence, entry
  point checks, all truncated prefixes, and 50,000 randomized mutations.
- **QEMU ELF suite (`tests/elf.py`)**: Multi-page BSS, initialized data, >12 KiB
  stack usage inside the 64 KiB stack, `brk` grow/shrink/reuse, 2 MiB + 4 KiB
  `mmap` crossing PAE PDE boundaries, `munmap` invalidation, 4 concurrent ELF
  processes in 16/64 MiB RAM, recoverable NX/guard/read-only `#PF` faults, and
  deterministic OOM rollback.
- **VFS execution & `SYS_EXEC` (`tests/continuation.py`)**: `spawn-elf`,
  in-place `SYS_EXEC` preserving PID/`cwd`/open FDs while closing `FD_CLOEXEC`,
  failed `SYS_EXEC` rollback, and downloading an ELF via `curl` to `/tmp` and
  `/disk` and executing it across a reboot.
