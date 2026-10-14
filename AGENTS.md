# Contribution & Engineering Guide

NotOS is a 32-bit x86 educational operating system kernel written from scratch
for PC BIOS and QEMU. It uses freestanding C11, GAS assembly (`.S`), native C
unit tests (with ASAN/UBSAN), and full-system Python/QEMU integration suites.

## Workflow

1. Read `handoff.md`, inspect `git status`, and review the relevant module
   documentation in `docs/`. Preserve existing local work; never revert or
   discard unrelated changes.
2. Define a concrete, bounded scope. When touching more than two files, write a
   brief plan before editing. Avoid drive-by refactors outside that scope.
3. If an open design decision alters a public API or kernel/userspace ABI, ask
   a single focused question. For internal implementation details, decide and
   state the relevant assumption explicitly.
4. Implement behavior and update its documented architectural limits together
   in the same change.
5. Run `make` and `make test` before delivering or creating a functional commit.
   If you modify the build system, linker scripts, or toolchain steps, also
   verify a clean rebuild (`make clean test`).
6. Report only commands actually executed and results observed. Explicitly
   distinguish local unit/QEMU tests, local network fixtures, public internet
   (`make test-online`), and remote CI runs.

Fix failures at their root cause. Never silence compiler warnings, remove
`-Werror` from project code, weaken assertions, or disable tests. Upstream
third-party dependencies retain their explicit warning policy; document any
warnings observed from upstream sources.

## Build & Test Commands

| Command | Purpose |
| --- | --- |
| `make` | Build `build/notos.img` and test fixture images |
| `make test` | Run native C unit tests and full-system QEMU test suites |
| `make clean test` | Clean rebuild and full test suite (matches CI) |
| `make test-online` | Optional tests against public DNS, HTTP, and HTTPS |
| `make run` | Launch QEMU with VGA display and serial console |

First-party code compiles with `-m32 -march=i486 -std=c11 -ffreestanding`,
`-mno-sse -mno-sse2 -mno-mmx -msoft-float`, and `-Wall -Wextra -Werror`.
Host libc, `libgcc`, and semihosting are never linked into the kernel or
user-space applications.

## Documentation by Topic

Module and subsystem contracts live in `docs/`. Read `docs/ARCHITECTURE.md`
before making structural changes, and read the relevant topic guides before
touching a module.

| Topic | Documents |
| --- | --- |
| Architecture and boundaries | `ARCHITECTURE.md` |
| Memory, PAE/NX paging, and heap | `MEMORY.md`, `ELF.md` |
| IDT, IRQs, PIC/PIT, and exceptions | `INTERRUPTS.md` |
| Tasks, scheduler, and processes | `TASKS.md`, `PROCESSES.md` |
| Syscalls, ring 3, ELF, and libc | `USERSPACE.md`, `ELF.md`, `LIBC.md` |
| File descriptors, VFS, and storage | `VFS.md`, `STORAGE.md` |
| Networking, sockets, poll, and TLS | `NETWORK.md`, `SOCKETS.md`, `POLL.md`, `TLS.md` |
| User-space curl client | `CURL_PORT.md`, `LIBC.md`, `TLS.md` |
| UI, shell line editing, and NotLang | `UI.md`, `NOTLANG.md`, `NOTLANG_USER.md` |
| Verification log and roadmap | `TESTING.md`, `ROADMAP.md` |

Documented limits are part of the contract. Never claim SMP, physical hardware
support, full POSIX compliance, FPU/SSE support, or other capabilities without
both implementation and tests. Never treat a clean build as runtime evidence.

## System Invariants

- **IRQ handlers**: Never allocate or free memory/resources, interpret programs,
  or print verbose diagnostics inside an IRQ handler. Always send PIC EOI
  before switching context.
- **User memory & wire safety**: Validate the full address range, overflow
  bounds, and page permissions (including 2 MiB PAE PDE boundaries) of user
  buffers before reading, writing, or consuming kernel state. Copy potentially
  unaligned wire structures safely via bytewise copy (`memcpy`). Preserve
  `EFLAGS.IF` across critical sections.
- **Resource lifecycle & rollback**: Free resources only when they are no longer
  active (for example, switch `CR3` before destroying an address space). Roll
  back partial acquisitions on error; process `exit`, `kill`, and user faults
  must release all process-owned frames, descriptors, VFS handles, and network
  registrations without leaks.
- **Fault isolation**: Strictly separate recoverable user-space application
  errors and faults from fatal kernel bugs.
- **Scheduler capacity**: Preserve the four-slot scheduler and process model
  unless a task explicitly changes it.
- **Storage safety**: Never write to the boot drive (`index=0` / primary master)
  from `disk.*` or `fs.*`; NotFS operates exclusively on the separate data disk
  (`index=1` / primary slave).
- **No fake success**: Never return dummy success (`0`) for unimplemented
  operations or unsupported hooks; always return a defined error code.
- **Third-party integrity**: Never modify upstream source archives, extracted
  trees, versions, or pinned SHA-256 hashes without explicit authorization.

## Module Organization & Coding Style

Each module owns a single responsibility. Pure logic without I/O, dynamic
allocation, or CPU register access belongs in `*_policy.*` or `*_core.*` with
native unit tests where applicable. Keep pure algorithms, platform adapters, and
shell diagnostic commands separated.

- **Headers**: Use `NOTOS_<NAME>_H` include guards, explicit public APIs, and
  `static` for module-internal functions and state.
- **ABI definitions**: Keep wire constants and structures in `*_abi.h` or
  `syscall_numbers.h`. Headers shared with assembly (`.S`) must not expose C
  types or declarations to the assembler (`#ifndef __ASSEMBLER__`).
- **Naming**:
  - Functions, variables, structs, and enums use `snake_case`, prefixed with the
    module name for public APIs.
  - Macros and enum constants use `UPPER_SNAKE` with the module prefix.
  - Identifier names must express both purpose and unit (`timeout_ms`,
    `frame_count`, `sector_offset`). Use single-letter names only for short loop
    indices; standard technical abbreviations (`fd`, `va`, `pa`, `cr3`, `pcb`)
    are permitted.
- **Types**: Use project fixed-width integer types (`u8`, `u16`, `u32`, `u64`)
  in kernel and standalone code. In libc adapters, use the standard public types
  required by the C/POSIX interface (`size_t`, `ssize_t`, `off_t`, etc.).
- **Control flow & formatting**: Keep functions at a single level of abstraction
  with early returns on error (~40 lines as a guideline, not a reason to
  artificially fragment cohesive logic). For new code: four-space indentation,
  one statement per line, and opening braces on the same line. In existing
  files, respect surrounding style without reformatting unrelated code.

## Code Comments & `TODO` Policy

Write all comments in English. Use `/* ... */` placed above the code in C and
GAS assembly (`.S`) files; use native `#` comments in Python and Makefiles.

### What to Comment

- **Public headers (`.h`)**: Document the contract, preconditions, ownership and
  lifetime rules, units, return values, and error semantics of public functions
  and structures.
- **Assembly routines (`.S`)**: Document register conventions, stack layout,
  privilege transitions (CPL0/CPL3), `EFLAGS` (`DF`, `IF`), and ABI invariants.
- **Security, hardware & ordering invariants**: Explain non-obvious constraints
  such as PAE 64-bit PDE/PTE high-word-first publication, TLB invalidation
  (`INVLPG` / `CR3` reload), PIC EOI ordering before context switches, `CR3`
  switching before freeing address spaces, ATA status polling delays, and
  validating user buffers before mutating kernel state.
- **ABI & wire layouts**: Document exact byte sizes, alignment rules, and
  field ordering for structures shared across syscall, disk, or network
  boundaries.

### What Not to Comment

- Do not restate obvious operations (`/* increment counter */`), leave dead or
  commented-out code, or record git changelog history in source files.
- Keep comments synchronized with code: whenever you change behavior, update or
  remove the corresponding comment in the same edit.

### `TODO` Policy

`TODO` comments are exceptional and must use the exact format:

```c
/* TODO(module): concrete action, milestone, or trigger condition */
```

- **Permitted use**: Only for genuine, bounded future work that lies outside the
  current task's scope (such as a planned milestone in `docs/ROADMAP.md`).
- **Forbidden use**: Never use a `TODO` for an incomplete implementation,
  missing error handling, unvalidated user pointer/buffer, resource leak, or
  unverified security check.
- **Tracking**: Every new `TODO` introduced in the codebase must be listed in
  the final delivery summary and, if it represents a user-visible or
  architectural milestone, documented in `docs/ROADMAP.md`.

## Commits & Delivery

- Use standard **Conventional Commits** in English:
  `<type>(<scope>): <concise summary>` (imperative mood, lowercase summary, no
  trailing period).
- Standard commit types:
  - `feat(scope): ...` — new feature or subsystem capability
  - `fix(scope): ...` — bug fix, safety/validation fix, or regression fix
  - `refactor(scope): ...` — internal restructuring without behavior change
  - `perf(scope): ...` — measurable performance or resource optimization
  - `docs(scope): ...` — documentation, comments, or roadmap updates
  - `test(scope): ...` — new or updated unit, fixture, or QEMU tests
  - `build(scope): ...` — Makefile, linker script, packaging, or toolchain changes
  - `ci(scope): ...` — CI workflow or runner configuration changes
  - `chore(scope): ...` — maintenance changes that do not alter runtime behavior
- Create one logical change per commit after compiling cleanly and passing all
  applicable tests.
- Stage files explicitly. Never commit `build/`, `.cache/`, `data.img`, fixture
  private keys, credentials, or temporary files.
- Never `git push` or force-push unless explicitly requested by the user. A
  handoff document never authorizes pushing or claiming unverified CI results.
- Keep the final delivery response concise: summarize changes made, exact
  commands run and results observed, commits created, and any new documented
  limits or `TODO` items.
