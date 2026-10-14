# Roadmap and Next Milestones

## Recently Completed Milestones

- **System Power Control (`SYS_POWER = 50`, `reboot`, `poweroff`, `halt`) (M5)**:
  Added `SYS_POWER` (`50`), NotFS `fs_sync` barrier, 8042/triple-fault `reboot`,
  QEMU/Bochs ACPI `poweroff` (`0x604` / `0xB004`), and IRQ-masked `halt` with
  multi-boot and clean-exit QEMU verification (`tests/power.py`).
- **Multi-Call User Utilities (`/system/box.elf`), `ed`, and `SYS_PROCESS_LIST` (M4)**:
  Added `/system/box.elf` providing 24 user-space applets (`echo`, `cat`, `ls`,
  `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `touch`, `head`, `tail`, `wc`, `grep` with
  `-v`/`-n`/`-i`, `env`, `kill`, `ps`, `sleep`, `date`, `uptime`, `true`,
  `false`, `reboot`, `poweroff`, and the `ed` line editor supporting `a`, `i`,
  `c`, `d`, `p`, `n`, `=`, `w`, `e`, `q`, and line addresses), plus
  `SYS_PROCESS_LIST` (`49`) for read-only process inspection and cross-filesystem
  `mv` copy+unlink fallback.
- **User-Space Shell (`/system/sh.elf`), Raw TTY Mode, and Shell Parser (M3)**:
  Added raw/no-echo mode on `stdin` via `SYS_FCNTL` (`NOTOS_F_GETTTY` /
  `NOTOS_F_SETTTY`), pure shell command/pipeline parser (`shell_parse_core.*`,
  tested under ASAN/UBSAN) supporting quoting, backslash escapes, `$VAR`/`${VAR}`
  and `$?`, pipelines (`|`), redirections (`<`, `>`, `>>`), background jobs
  (`&`), and `;`, and `/system/sh.elf` with `line_core.*` interactive editing,
  `Tab` completion, and builtins (`cd`, `exit`, `export`, `unset`, `jobs`,
  `wait`). Launched via kernel shell `sh` (`USER_SHELL_DEFAULT ?= 0`).
- **NotFS v3, Atomic `rename`, `dirent` Wrappers, and Inode Recycling (M2)**:
  Upgraded persistent disk storage to dual-bank `NotFS v3` (48 nodes, 88 KiB
  payload, 96 KiB banks at 256-sector stride on a 256 KiB data disk, `mtime` via
  UTC clock, and single-user stored `mode`/`uid`/`gid` without permission
  enforcement) while retaining `v2` and `v1` read paths and safe `v2->v3` /
  `v1->v3` bank-1 migration. Added `SYS_RENAME` (`48`, atomic within `/tmp` or
  `/disk`, returning `ERROR_CROSS_DEVICE` / `EXDEV` across mounts), kernel shell
  `mv`, bounded persistent inode recycling across `[256, 65535]`, and Picolibc
  `opendir`/`fdopendir`/`readdir`/`closedir`/`rewinddir`/`dirfd` and `rename`.
- **Process Composition & Anonymous Pipes (M1)**: Added `SYS_SPAWN` (`43`, with
  `argv`, `envp`, and explicit child `fd_map`), `SYS_WAIT` (`44`, blocking and
  `WNOHANG` with zombie reaping and orphan auto-reaping), `SYS_GETPPID` (`45`),
  `SYS_PIPE` (`46`, 4 KiB kernel ring buffer with blocking read/write, EOF,
  `EPIPE`, and `poll()` integration), `SYS_KILL` (`47`, terminate-only with
  `128 + sig` exit status), and Picolibc wrappers (`notos_spawn`, `posix_spawn`,
  `waitpid`, `pipe`, `pipe2`, `kill`, `getppid`, `environ`/`getenv`/`setenv`).
- **Ring 3 `curl` HTTP/HTTPS**: Upstream `curl` 8.11.1 running in ring 3 with a
  dedicated Picolibc-linked build of MbedTLS 3.6.5, default trust store
  `/system/cacert.pem`, `--cacert` override, and TLS 1.2 / 1.3 verification.
  Supported by the 512 KiB low + 768 KiB high BIOS bootloader layout.
- **VFS Execution & `SYS_EXEC` (`37`)**: Transactional image/space/stack
  preparation before commit, preserving PID, `cwd`, and open descriptors except
  `FD_CLOEXEC`. `spawn-elf PATH [ARGS]` launches ELF binaries from `/system`,
  `/tmp`, and `/disk`.
- **Shell Line Editing & Completion**: 8-entry circular command history, cursor
  editing (`Left`/`Right`/`Home`/`End`/`Backspace`/`Delete`), and `Tab`
  completion for commands and VFS paths across PS/2 and serial CSI/SS3 outside
  IRQ context.
- **Comprehensive Verification**: Native ASAN/UBSAN unit suites (`fs-unit`,
  `line-unit`, `shell-parse-unit`, `vfs-unit`, `poll-unit`, `elf-unit`,
  `cert-unit`, `float-unit`) and 29 full-system QEMU test suites (see
  [TESTING.md](TESTING.md)).

## Next Planned Milestones

1. **DHCP Network Configuration**:
   - Enable lwIP DHCP client with 3-second boot discovery and deterministic
     static fallback (`10.0.2.15/24`), shared network configuration state, and
     configurable hostname (option 12).
2. **Configurable CPU Budgets & Extended TLS Capabilities**:
   - Refine per-process CPU budget policies and support client certificates,
     explicit trust-store updates, and `CApath`.

## Current Architectural Boundaries

NotOS intentionally retains bounded laboratory limits: 4 concurrent process
slots, 8 TCP endpoints, 16 FDs per task, 64 global open file descriptions,
256 KiB global `tmpfs` RAM quota, and 88 KiB `NotFS v3` disk payload (48 nodes).
SMP, threads, hardware FPU/SSE, UEFI, verified physical hardware, kernel
preemption, and full POSIX compliance are out of scope for the current
architecture. There are no open `TODO` placeholders for incomplete functionality
in the codebase.
