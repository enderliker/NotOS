# VFS, File Descriptors, and Directories

## Overview

NotOS provides a bounded Virtual File System (`kernel/vfs.c`) and per-process
file descriptor tables (`kernel/fd.c`) with **16 file descriptors per task** and
**64 global open file descriptions**. `dup` and `dup2` share the underlying open
file description (offset and status flags), while `FD_CLOEXEC` is stored per
descriptor slot.

| Mount Path | Backend Contract |
| --- | --- |
| `/tmp` | In-memory `tmpfs` directories and files: up to 16 named entries, 24 total nodes, and 64 KiB per file (256 KiB / 64-frame global RAM quota; physical-frame-backed, not kernel heap) |
| `/disk` | Persistent transactional `NotFS v3` on the secondary IDE disk: up to 48 nodes and 88 KiB (`90,112` bytes) total payload |
| `/system` | Immutable embedded read-only/NX blobs: `curl.elf`, `libc.elf`, `sh.elf`, and `cacert.pem` |
| `/dev` | Per-process private `stdin`, shared `stdout`/`stderr`/`console`, and `null` |

## Path Resolution & Directories

Absolute and relative paths are normalized lexically against the calling
process's private current working directory (`cwd`), resolving `.`, `..`, and
repeated `/` separators up to **63 bytes** total. Path components in `/tmp` and
`/disk` accept `1..23` characters from `[A-Za-z0-9._-]`. Every new task starts
with `cwd` set to `/`, `SYS_SPAWN` inherits the parent's `cwd`, and `SYS_EXEC`
preserves `cwd` (the interactive shell maintains its own separate `cwd`).

Supported directory and metadata operations:
- `mkdir`, `rmdir` (requires an empty directory), `rename` (`SYS_RENAME=48`,
  same-filesystem atomic rename; cross-filesystem returns `-ERROR_CROSS_DEVICE`
  / `EXDEV`), `chdir`, `getcwd`, `stat`, and read-only directory `open`.
- `notos_readdir` (`SYS_READDIR=42`) reads one 36-byte
  `struct notos_directory_entry` (`inode`, `type`, `size`, `name[24]`) and
  advances the open directory description's index, returning `1` per entry and
  `0` at EOF. Picolibc layers `opendir`, `fdopendir`, `readdir`, `rewinddir`
  (via `lseek(fd, 0, SEEK_SET)`), `closedir`, and `dirfd` over `SYS_READDIR`.
- **Single-user metadata (`mtime`, `mode`, `uid`, `gid`)**: `struct notos_file_info`
  (32 bytes) reports 64-bit UTC epoch `mtime` (updated on create, write,
  truncate, and `touch`), plus `permissions` (`mode`), `uid`, and `gid` (`0` by
  default). NotOS is a single-user system: `mode`, `uid`, and `gid` are stored
  and reported in `stat`/`fstat` without enforcing access-control checks or
  making security claims.

## Coherency, `unlink`, `rename`, `mkfs`, and Inodes

- Opening the same file path on `/disk` multiple times shares a single coherent
  in-memory VFS node so shell and user-space writes are immediately visible to
  subsequent reads.
- **`unlink` and `rename` overwrite with open handles**: Before removing or
  overwriting a `/disk` file entry and publishing the new `NotFS v3` snapshot,
  the VFS reserves a RAM copy of the file's payload (charged against the 64-page
  RAM quota) so existing open file descriptors continue reading/writing the
  unlinked/overwritten file's content until closed.
- **`rename` semantics**: Supports files and directories (updating child paths
  atomically across subtrees), rejects renaming a directory into its own
  descendant (`-ERROR_INVALID_ARGUMENT` / `EINVAL`), rejects file-to-directory
  (`EISDIR`), directory-to-file (`ENOTDIR`), and non-empty directory overwrite
  (`ENOTEMPTY`), and rejects cross-filesystem renames between `/tmp`, `/disk`,
  `/system`, and `/dev` with `-ERROR_CROSS_DEVICE` (`EXDEV`).
- **`mkfs` invalidation**: Running `mkfs` explicitly formats `/disk` and marks
  any pre-existing open `/disk` nodes stale (`ENOENT` on subsequent I/O).
- **Bounded persistent & transient inode recycling**: `tmpfs` recycles inode IDs
  in `16..65535` while avoiding collisions with any live or open-unlinked VFS
  node; `NotFS v3` recycles persistent inodes across `[256, 65535]` using a
  bounded collision scan against the 48 snapshot slots so wraparound never
  collides with a live on-disk inode.

## File & Console I/O Semantics

- Supported file syscalls and Picolibc wrappers: `open`, `read`, `write`,
  `close`, `lseek`, `stat`, `fstat`, `unlink`, `rename`, `ftruncate`, `dup`,
  `dup2`, `fcntl`, `isatty`, `opendir`, `fdopendir`, `readdir`, `rewinddir`,
  `closedir`, and `dirfd`.
- Supports `O_CREAT`, `O_EXCL`, `O_TRUNC`, `O_APPEND`, `O_NONBLOCK`, and
  `O_CLOEXEC`.
- Single `read`/`write` syscalls transfer up to **4096 bytes**. `O_APPEND`
  positions at EOF before each write. Seeking past EOF and writing creates
  zero-filled sparse gaps up to the backend maximum (`64 KiB` on `tmpfs`,
  `88 KiB` on `NotFS v3`); `ftruncate` zeroes discarded ranges. Partial page
  allocations in `tmpfs` roll back cleanly on `ENOSPC` / `ENOMEM`.
- Every `write`, `ftruncate`, `mkdir`, `rmdir`, `unlink`, or `rename` on `/disk`
  publishes a transactional `NotFS v3` snapshot (see [STORAGE.md](STORAGE.md)).
- **Per-process `stdin`**: Each process has a private 256-byte input queue fed
  by `feed PID TEXT` (which appends `\n`), closed by `feed-eof PID`, or fed
  directly from PS/2 and `COM1` serial when running as the foreground shell
  process. `SYS_FCNTL` (`24`) supports `NOTOS_F_GETTTY` (`5`) and
  `NOTOS_F_SETTTY` (`6`) on `stdin` descriptors (`NOTOS_TTY_CANON` and
  `NOTOS_TTY_ECHO`, default `NOTOS_TTY_DEFAULT = 3`). Blocking reads suspend
  only the calling process (`TASK_WAIT_INPUT`); `O_NONBLOCK` returns `-EAGAIN`
  when empty. Process `exit`, `kill`, and user faults close all open descriptors
  and release any unlinked nodes.
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
| `18` | `SYS_STAT` | `path`, `struct notos_file_info *out` (32 bytes) |
| `19` | `SYS_FSTAT` | `fd`, `struct notos_file_info *out` (32 bytes) |
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
| `48` | `SYS_RENAME` | `old_path`, `new_path` |

All user buffers and permissions are validated before mutating descriptor offsets
or VFS state, and wire structures (`notos_file_info`, `notos_directory_entry`)
are copied bytewise to tolerate unaligned user addresses.
