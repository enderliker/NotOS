# VFS, File Descriptors, and Directories

## Overview

NotOS provides a bounded Virtual File System (`kernel/vfs.c`) and per-process
file descriptor tables (`kernel/fd.c`) with **16 file descriptors per task** and
**64 global open file descriptions**. `dup` and `dup2` share the underlying open
file description (offset and status flags), while `FD_CLOEXEC` is stored per
descriptor slot.

| Mount Path | Backend Contract |
| --- | --- |
| `/tmp` | In-memory `tmpfs` directories and files: up to 16 named entries, 24 total nodes, and 64 KiB per file (256 KiB / 64-frame global RAM quota) |
| `/disk` | Persistent transactional `NotFS v2` on the secondary IDE disk: up to 24 nodes and 60 KiB (`61,440` bytes) total payload |
| `/system` | Immutable embedded read-only/NX blobs: `curl.elf`, `libc.elf`, and `cacert.pem` |
| `/dev` | Per-process private `stdin`, shared `stdout`/`stderr`/`console`, and `null` |

## Path Resolution & Directories

Absolute and relative paths are normalized lexically against the calling
process's private current working directory (`cwd`), resolving `.`, `..`, and
repeated `/` separators up to **63 bytes** total. Path components in `/tmp` and
`/disk` accept `1..23` characters from `[A-Za-z0-9._-]`. Every new task starts
with `cwd` set to `/`, and `SYS_EXEC` preserves `cwd` (the interactive shell
maintains its own separate `cwd`).

Supported directory operations:
- `mkdir`, `rmdir` (requires an empty directory), `chdir`, `getcwd`, `stat`, and
  read-only directory `open`.
- `notos_readdir` (`SYS_READDIR=42`) reads one 36-byte
  `struct notos_directory_entry` (`inode`, `type`, `size`, `name[24]`) and
  advances the open directory description's index, returning `1` per entry and
  `0` at EOF.

## Coherency, `unlink`, `mkfs`, and Inodes

- Opening the same file path on `/disk` multiple times shares a single coherent
  in-memory VFS node so shell and user-space writes are immediately visible to
  subsequent reads.
- **`unlink` with open handles**: Before removing a `/disk` file entry and
  publishing the new `NotFS v2` snapshot, the VFS reserves a RAM copy of the
  file's payload (charged against the 64-page RAM quota) so existing open file
  descriptors continue reading/writing the unlinked file's content until closed.
- **`mkfs` invalidation**: Running `mkfs` explicitly formats `/disk` and marks
  any pre-existing open `/disk` nodes stale (`ENOENT` on subsequent I/O).
- **Inode numbers**: `tmpfs` reuses inode IDs in `16..255` while avoiding
  collisions with any live node; `NotFS v2` assigns monotonic persistent inodes
  in `256..65535` (compatible with 16-bit/32-bit `ino_t` on `i386`).

## File & Console I/O Semantics

- Supported file syscalls and Picolibc wrappers: `open`, `read`, `write`,
  `close`, `lseek`, `stat`, `fstat`, `unlink`, `ftruncate`, `dup`, `dup2`,
  `fcntl`, and `isatty`.
- Supports `O_CREAT`, `O_EXCL`, `O_TRUNC`, `O_APPEND`, `O_NONBLOCK`, and
  `O_CLOEXEC`.
- Single `read`/`write` syscalls transfer up to **4096 bytes**. `O_APPEND`
  positions at EOF before each write. Seeking past EOF and writing creates
  zero-filled sparse gaps; `ftruncate` zeroes discarded ranges. Partial page
  allocations in `tmpfs` roll back cleanly on `ENOSPC` / `ENOMEM`.
- Every `write` or `ftruncate` on `/disk` publishes a transactional `NotFS v2`
  snapshot (see [STORAGE.md](STORAGE.md)).
- **Per-process `stdin`**: Each process has a private 256-byte input queue fed
  by `feed PID TEXT` (which appends `\n`) and closed by `feed-eof PID`. Blocking
  reads suspend only the calling process (`TASK_WAIT_INPUT`); `O_NONBLOCK`
  returns `-EAGAIN` when empty. Process `exit`, `kill`, and user faults close
  all open descriptors and release any unlinked nodes.
- **Unidirectional Kernel Pipes (`SYS_PIPE = 46`)**: Up to **16 active pipes**
  globally (`FILE_PIPE_MAX = 16`), each backed by a 4 KiB physical page
  (`FILE_PIPE_BYTES = 4096`) allocated from `physical_alloc()` and freed as soon
  as both `readers == 0` and `writers == 0`. `read` blocks (`TASK_WAIT_INPUT`)
  when empty while `writers > 0` and returns `0` (EOF) when empty and
  `writers == 0`; `write` returns `-EPIPE` when `readers == 0` and blocks
  (`TASK_WAIT_OUTPUT`) when full while `readers > 0` (or returns `-EAGAIN` with
  `O_NONBLOCK`). `fstat` reports `NOTOS_FILE_FIFO` (`S_IFIFO`) and queued bytes.

## VFS Syscall ABI (`INT 0x80`)

| Number | Operation | Arguments (`EBX`, `ECX`, `EDX`) |
| --- | --- | --- |
| `14` | `SYS_OPEN` | `path`, `flags`, `mode` |
| `15` | `SYS_CLOSE` | `fd` |
| `16` | `SYS_READ` | `fd`, `buffer`, `length` |
| `17` | `SYS_LSEEK` | `fd`, `offset`, `whence` |
| `18` | `SYS_STAT` | `path`, `struct notos_file_info *out` (20 bytes) |
| `19` | `SYS_FSTAT` | `fd`, `struct notos_file_info *out` (20 bytes) |
| `20` | `SYS_UNLINK` | `path` |
| `21` | `SYS_FTRUNCATE` | `fd`, `length` |
| `22` | `SYS_DUP` | `oldfd` |
| `23` | `SYS_DUP2` | `oldfd`, `newfd` |
| `24` | `SYS_FCNTL` | `fd`, `command`, `argument` |
| `25` | `SYS_ISATTY` | `fd` |
| `37` | `SYS_EXEC` | `path`, `arguments` |
| `38` | `SYS_MKDIR` | `path`, `mode` |
| `39` | `SYS_RMDIR` | `path` |
| `40` | `SYS_CHDIR` | `path` |
| `41` | `SYS_GETCWD` | `buffer`, `capacity` (returns `0` on success) |
| `42` | `SYS_READDIR` | `fd`, `struct notos_directory_entry *out` (36 bytes; `1` entry, `0` EOF) |
| `46` | `SYS_PIPE` | `int fds_out[2]`, `flags` (`O_NONBLOCK \| O_CLOEXEC`) |

All user buffers and permissions are validated before mutating descriptor offsets
or VFS state, and wire structures (`notos_file_info`, `notos_directory_entry`)
are copied bytewise to tolerate unaligned user addresses.
