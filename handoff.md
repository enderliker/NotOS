# Handoff: Process Composition, Ring 3 HTTPS, VFS Execution, and Shell QoL

## Current State & Recent Implementation

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
  `0x1bfffc`. Embedded `curl` and CA blobs are mapped R/NX; kernel BSS spans
  `0x1c0000` to `< 0x280000` and is reserved in the physical frame allocator.
  Total bootable image size is 1.5 MiB.
- **VFS Execution & `SYS_EXEC` (`37`)**: `SYS_EXEC` validates and prepares the
  new ELF image, private PAE address space, and initial user stack before
  committing. Preserves PID, `cwd`, and open file descriptors except those
  marked `FD_CLOEXEC`. Switches `CR3` to the new address space before destroying
  the old one. `spawn-elf PATH [ARGS]` launches ELF binaries from `/system`,
  `/tmp`, or `/disk` with space-separated arguments (up to 256 bytes, 16 `argv`
  entries, empty `envp`). See [docs/ELF.md](docs/ELF.md) and
  [docs/PROCESSES.md](docs/PROCESSES.md).
- **Transactional `NotFS v2`**: Two 64 KiB banks (128 sectors each), up to 24
  nodes and 60 KiB total payload, supporting directories, persistent user-space
  writes, CRC32 validation, and `FLUSH` + readback verification before switching
  the active bank. Automatically migrates valid `NotFS v1` volumes into the high
  v2 bank first while preserving v1 sectors `0..65` until v2 is verified.
  Persistent inode IDs are monotonic (`256..65535`). See
  [docs/STORAGE.md](docs/STORAGE.md).
- **VFS Directories & Coherency**: Supports `tmpfs` and `NotFS v2` directories,
  per-process private `cwd`, and wire syscalls `mkdir`, `rmdir`, `chdir`,
  `getcwd`, and `readdir`. Open `/disk` descriptors share coherent live nodes;
  `unlink` snapshots file contents into RAM for open handles before committing
  deletion; `mkfs` invalidates stale disk handles without touching the boot
  drive. See [docs/VFS.md](docs/VFS.md).
- **Shell Line Editing**: 8-entry circular command history, cursor movement
  (`Left`, `Right`, `Home`, `End`, `Backspace`, `Delete`), and `Tab` completion
  for shell commands and VFS paths across both PS/2 keyboard and serial CSI/SS3
  sequences outside IRQ context (`line_core.*` pure logic + `line_input.*`
  adapter). See [docs/UI.md](docs/UI.md).

## Architectural Limits

- Four process slots (including unreaped `PROCESS_ZOMBIE` slots), eight TCP
  endpoints, 16 FDs per task, 64 global open file descriptions, 16 anonymous
  4 KiB kernel pipes, 64 KiB per `tmpfs` file (256 KiB global RAM quota), and
  60 KiB `NotFS v2` payload.
- Default CPU budget is 250 CPL3 IRQ0 ticks per task (5-tick quantum); `/system/curl.elf`
  receives 2,500 ticks for TLS handshakes and X.509 parsing, and `SYS_EXEC` does
  not reset consumed ticks.
- No `fork`, full POSIX compliance, threads/SMP, FPU/SSE, verified physical
  hardware, preemptive kernel, or user-space signal handlers (`SYS_KILL` is
  terminate-only).
- Strict TLS verification: `-k` / `--insecure` never bypasses certificate checks;
  no client certificates, `CApath`, or OCSP/CRL revocation.

## Validation Status

- Run `make` and `make test` for standard verification, or `make clean test` for
  a full clean rebuild matching CI.
- All 23 native host unit test binaries (including `fs-unit`, `line-unit`,
  `vfs-unit`, `poll-unit`, `elf-unit`, `cert-unit`, and `float-unit` under
  ASAN/UBSAN) and all 26 QEMU system test suites (including `tests/pipeline.py`)
  pass (`make` and `make test`).
- Upstream compiler warnings remain explicitly documented per policy: GCC warns
  on enum/bool conversions in curl's `transfer.c`, an unused function in curl's
  `url.c`, and `_set_tls` in Picolibc's `tls.c`. First-party code compiles
  cleanly under `-Wall -Wextra -Werror`.
- No active `TODO` items exist in the codebase. Next planned milestones are
  tracked in [docs/ROADMAP.md](docs/ROADMAP.md).
