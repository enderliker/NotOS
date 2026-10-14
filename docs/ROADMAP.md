# Roadmap and Next Milestones

## Recently Completed Milestones

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
- **Directories & Writable `NotFS v2`**: Real directories, per-process private
  `cwd`, wire syscalls `mkdir`, `rmdir`, `chdir`, `getcwd`, and `readdir`,
  writable dual-bank `NotFS v2` (24 nodes, 60 KiB payload), and safe `NotFS v1`
  to `v2` migration preserving v1 banks until v2 is verified on disk.
- **Shell Line Editing & Completion**: 8-entry circular command history, cursor
  editing (`Left`/`Right`/`Home`/`End`/`Backspace`/`Delete`), and `Tab`
  completion for commands and VFS paths across PS/2 and serial CSI/SS3 outside
  IRQ context.
- **Comprehensive Verification**: Native ASAN/UBSAN unit suites (`fs-unit`,
  `line-unit`, `vfs-unit`, `poll-unit`, `elf-unit`, `cert-unit`, `float-unit`)
  and 26 full-system QEMU test suites (see [TESTING.md](TESTING.md)).

## Next Planned Milestones

1. **POSIX `dirent` Wrapper, Transactional `rename`, and Inode Recycling**:
   - Layer standard `opendir`/`readdir`/`closedir` wrappers in libc over
     `SYS_READDIR`, add atomic `rename` across `tmpfs` and `NotFS v2`, and add
     bounded persistent inode recycling.
2. **Configurable CPU Budgets & Storage Latency**:
   - Refine per-process CPU budget policies across `SYS_EXEC` chains and reduce
     timer/network latency during multi-sector ATA PIO flushes.
3. **Quoted Shell Argument Parser & Job Control**:
   - Add opt-in quoting/escaping and explicit foreground/background terminal
     control without silently breaking the existing space-split argument
     contract.
4. **Extended TLS & HTTP Capabilities**:
   - Support client certificates, explicit trust-store updates, `CApath`, and
     optional HTTP/2 framing where needed by user-space applications.
5. **Installable Disk Packages**:
   - Package small standalone ring 3 ELF utilities for installation and execution
     from `/disk`.

## Current Architectural Boundaries

NotOS intentionally retains bounded laboratory limits: 4 concurrent process
slots, 8 TCP endpoints, 16 FDs per task, 64 global open file descriptions,
256 KiB global `tmpfs` RAM quota, and 60 KiB `NotFS v2` disk payload. SMP,
threads, hardware FPU/SSE, UEFI, verified physical hardware, kernel preemption,
and full POSIX compliance are out of scope for the current architecture. There
are no open `TODO` placeholders for incomplete functionality in the codebase.
