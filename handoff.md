# Handoff: Power Control (`SYS_POWER`), Multi-Call Utilities (`/system/box.elf`), User-Space Shell (`/system/sh.elf`), NotFS v3, and Process Composition

## Current State & Recent Implementation

- **System Power Control (`SYS_POWER = 50`, `reboot`, `poweroff`, `halt`) (M5)**:
  Added `SYS_POWER` (`50`), libc `notos_power`, and kernel shell commands
  `reboot`, `poweroff`, and `halt` backed by `arch_power`. Waits for any
  in-flight NotFS commit (`fs_sync()`), masks PIC IRQs `0..15`, and performs
  8042 reset (`0xFE` to port `0x64`) with a deliberate zero-IDT triple-fault
  fallback on `NOTOS_POWER_REBOOT` (`1`), QEMU/Bochs ACPI shutdown (`0x2000` to
  ports `0x604` then `0xB004`) with `cli; hlt` fallback on `NOTOS_POWER_POWEROFF`
  (`2`), or immediate `cli; hlt` on `NOTOS_POWER_HALT` (`3`).
- **Multi-Call User Utilities (`/system/box.elf`), `ed`, and `SYS_PROCESS_LIST` (M4)**:
  Added `/system/box.elf` providing 24 user-space applets (`echo`, `cat`, `ls`,
  `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `touch`, `head`, `tail`, `wc`, `grep` with
  `-v`/`-n`/`-i`, `env`, `kill`, `ps`, `sleep`, `date`, `uptime`, `true`,
  `false`, `reboot`, `poweroff`, and the `ed` line editor supporting `a`, `i`,
  `c`, `d`, `p`, `n`, `=`, `w`, `e`, `q`, and line addresses), plus
  `SYS_PROCESS_LIST` (`49`, read-only snapshot of active and zombie process
  slots) and cross-filesystem `mv` copy+unlink fallback.
- **User-Space Shell (`/system/sh.elf`), Raw TTY Mode, and Shell Parser (M3)**:
  Added raw/no-echo mode on `stdin` via `SYS_FCNTL` (`NOTOS_F_GETTTY` /
  `NOTOS_F_SETTTY`), pure shell command/pipeline parser (`shell_parse_core.*`,
  tested under ASAN/UBSAN) supporting single/double quotes, backslash escapes,
  per-job `$VAR`/`${VAR}` and `$?` expansion, pipelines (`|`), redirections
  (`<`, `>`, `>>`), background jobs (`&`), and `;`, and `/system/sh.elf` with
  `line_core.*` interactive editing, `Tab` completion, and builtins (`cd`,
  `exit`, `export`, `unset`, `jobs`, `wait`). The built-in kernel shell stays
  the default boot path (`USER_SHELL_DEFAULT ?= 0`), and `sh` launches
  `/system/sh.elf` in the foreground. See [docs/UI.md](docs/UI.md) and
  [docs/VFS.md](docs/VFS.md).
- **NotFS v3, Atomic `rename`, `dirent` Wrappers, and Inode Recycling (M2)**:
  Upgraded persistent disk storage to dual-bank `NotFS v3` (48 nodes, 88 KiB
  payload, 96 KiB banks at 256-sector stride on a 256 KiB data disk, `mtime` via
  UTC clock, and single-user stored `mode`/`uid`/`gid` without permission
  enforcement) while retaining `v2` and `v1` read paths and safe `v2->v3` /
  `v1->v3` bank-1 migration. Added `SYS_RENAME` (`48`, atomic within `/tmp` or
  `/disk`, returning `ERROR_CROSS_DEVICE` / `EXDEV` across mounts), kernel shell
  `mv`, bounded persistent inode recycling across `[256, 65535]`, and Picolibc
  `opendir`/`fdopendir`/`readdir`/`closedir`/`rewinddir`/`dirfd` and `rename`.
  See [docs/STORAGE.md](docs/STORAGE.md), [docs/VFS.md](docs/VFS.md), and
  [docs/LIBC.md](docs/LIBC.md).
- **Process Composition & Anonymous Pipes (M1)**: Added `SYS_SPAWN` (`43`,
  launching ELF binaries from `/system`, `/tmp`, or `/disk` with `argv`, `envp`,
  and an up to 16-entry `fd_map`), `SYS_WAIT` (`44`, blocking and `WNOHANG` wait
  for a specific child or `-1` any child, zombie reaping, and orphan auto-reaping),
  `SYS_GETPPID` (`45`), `SYS_PIPE` (`46`, 16 global 4 KiB kernel ring buffers
  with blocking read/write, EOF, `EPIPE`, and `poll()` integration), and
  `SYS_KILL` (`47`, terminate-only signals `1..31` exiting with `128 + sig`).
  Added Picolibc wrappers (`notos_spawn`, `posix_spawn`, `wait`/`waitpid`,
  `pipe`/`pipe2`, `kill`, `getppid`, and dynamic `environ`/`getenv`/`setenv`
  initialised from the initial ring 3 stack). See
  [docs/PROCESSES.md](docs/PROCESSES.md), [docs/VFS.md](docs/VFS.md), and
  [docs/LIBC.md](docs/LIBC.md).
- **User-space `curl` 8.11.1 (HTTP/HTTPS)**: Runs in ring 3 linked against
  Picolibc 1.8.11 and a dedicated user-space build of MbedTLS 3.6.5 (separate
  from the kernel TLS backend). Default trust store is the immutable
  `/system/cacert.pem` image; `--cacert PATH` selects an alternate guest bundle.
  All third-party archives, versions, and SHA-256 hashes remain unmodified. See
  [docs/CURL_PORT.md](docs/CURL_PORT.md) and [docs/TLS.md](docs/TLS.md).
- **Bootloader & Memory Layout**: Loads 512 KiB low (`0x10000..0x90000`) plus
  768 KiB high (`0x100000..0x1c0000`) verified by signatures at `0x8fffc` and
  `0x1bfffc`. Embedded `curl`, `sh`, `box`, and CA blobs are mapped R/NX; kernel
  BSS spans `0x1c0000` to `< 0x280000` and is reserved in the physical frame
  allocator. Total bootable image size is 1.5 MiB.
- **VFS Execution & `SYS_EXEC` (`37`)**: `SYS_EXEC` validates and prepares the
  new ELF image, private PAE address space, and initial user stack before
  committing. Preserves PID, `cwd`, and open file descriptors except those
  marked `FD_CLOEXEC`. Switches `CR3` to the new address space before destroying
  the old one. `spawn-elf PATH [ARGS]` launches ELF binaries from `/system`,
  `/tmp`, or `/disk` with space-separated arguments (up to 256 bytes, 16 `argv`
  entries, empty `envp`). See [docs/ELF.md](docs/ELF.md) and
  [docs/PROCESSES.md](docs/PROCESSES.md).
- **Transactional `NotFS v3`**: Two 96 KiB banks (192 sectors each at a
  256-sector stride on a 256 KiB data disk), up to 48 nodes and 88 KiB total
  payload, supporting directories, persistent user-space writes, atomic
  `rename`, `mtime`, stored single-user `mode`/`uid`/`gid`, CRC32 validation,
  and `FLUSH` + readback verification before switching the active bank.
  Automatically migrates valid `NotFS v2` and `NotFS v1` volumes into Bank 1
  (`LBA 256..447`) first while preserving legacy banks until v3 is verified.
  Persistent inode IDs wrap and recycle across `[256, 65535]`. See
  [docs/STORAGE.md](docs/STORAGE.md).
- **VFS Directories & Coherency**: Supports `tmpfs` and `NotFS v3` directories,
  per-process private `cwd`, and wire syscalls `mkdir`, `rmdir`, `chdir`,
  `getcwd`, `readdir`, and `rename`. Open `/disk` descriptors share coherent live
  nodes; `unlink` and overwriting `rename` snapshot file contents into RAM for
  open handles before committing; `mkfs` invalidates stale disk handles without
  touching the boot drive. See [docs/VFS.md](docs/VFS.md).
- **Shell Line Editing**: 8-entry circular command history, cursor movement
  (`Left`, `Right`, `Home`, `End`, `Backspace`, `Delete`), and `Tab` completion
  for shell commands and VFS paths across both PS/2 keyboard and serial CSI/SS3
  sequences outside IRQ context (`line_core.*` pure logic + `line_input.*`
  adapter and `/system/sh.elf`). See [docs/UI.md](docs/UI.md).

## Architectural Limits

- Four process slots (including unreaped `PROCESS_ZOMBIE` slots), eight TCP
  endpoints, 16 FDs per task, 64 global open file descriptions, 16 anonymous
  4 KiB kernel pipes, 64 KiB per `tmpfs` file (256 KiB global RAM quota), and
  88 KiB `NotFS v3` payload (48 nodes).
- Default CPU budget is 250 CPL3 IRQ0 ticks per task (5-tick quantum); `/system/curl.elf`
  and `/system/sh.elf` receive 2,500 ticks, and `SYS_EXEC` does not reset
  consumed ticks.
- No `fork`, full POSIX compliance, threads/SMP, FPU/SSE, verified physical
  hardware, preemptive kernel, or user-space signal handlers (`SYS_KILL` is
  terminate-only).
- Strict TLS verification: `-k` / `--insecure` never bypasses certificate checks;
  no client certificates, `CApath`, or OCSP/CRL revocation.

## Validation Status

- Run `make` and `make test` for standard verification, or `make clean test` for
  a full clean rebuild matching CI.
- All 24 native host unit test binaries (including `fs-unit`, `line-unit`,
  `shell-parse-unit`, `vfs-unit`, `poll-unit`, `elf-unit`, `cert-unit`, and
  `float-unit` under ASAN/UBSAN) and all 28 QEMU system test suites (including
  `tests/utilities.py`, `tests/shell.py`, `tests/pipeline.py`,
  `tests/storage.py`, `tests/files.py`, and `tests/vfs.py`) pass (`make clean test`).
- Upstream compiler warnings remain explicitly documented per policy: GCC warns
  on enum/bool conversions in curl's `transfer.c`, an unused function in curl's
  `url.c`, and `_set_tls` in Picolibc's `tls.c`. First-party code compiles
  cleanly under `-Wall -Wextra -Werror`.
- No active `TODO` items exist in the codebase. Next planned milestones are
  tracked in [docs/ROADMAP.md](docs/ROADMAP.md).
