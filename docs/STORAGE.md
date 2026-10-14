# NotFS v3 and ATA PIO Storage

## ATA PIO Driver & Data Disk Safety (`kernel/disk.c`)

NotOS includes a 28-bit LBA ATA PIO driver strictly locked to the **IDE
primary-slave drive (`index=1`, I/O base `0x1F0`, drive select `0xB0`)**. The
boot drive (`index=0`, primary-master) is never selected for writes.

- `make run` automatically creates (or extends) a 256 KiB `data.img` file (512
  sectors) outside `build/` and Git so persistent data survives `make clean`.
- Set `NOTOS_DATA_DISK=/path/to/data.img` to attach an alternate disk image.
- An unformatted or absent disk is never formatted automatically; `mkfs` must be
  run explicitly by the user to initialize or wipe the data disk.

## Dual-Bank Transactional `NotFS v3` (`kernel/fs_core.c`, `kernel/fs.c`)

`NotFS v3` occupies two 96 KiB banks (192 sectors each) placed at a 128 KiB
(256-sector) stride on a 256 KiB (512-sector) data disk: **Bank 0 at sectors
`0..191`** and **Bank 1 at sectors `256..447`**:
- **Header (512 bytes, sector 0 of bank)**: Magic (`NOTFS`, `0x5346544e`),
  format version (`3`), monotonic generation counter, CRC32 checksum covering
  the entire 96 KiB bank (header, directory table, and payload), and persistent
  `next_inode` allocation cursor (`256..65536`).
- **Directory Table (sectors 1..15 of bank, padded to 8192 bytes total metadata)**:
  Up to **48 nodes** (files or directories) of 96 bytes each, storing:
  - `path[64]`: Canonical relative path under `/disk` (`1..57` chars).
  - `mtime` (`u64`): Last-modification timestamp in UTC seconds since the Unix
    epoch (`1970-01-01T00:00:00Z`), populated from the CMOS RTC UTC clock (`0`
    if the RTC is unavailable).
  - `size`, `offset`, `kind`, `inode` (`256..65535`), and `permissions` (`mode`
    bits `0..0777`).
  - `uid` (`u16`) and `gid` (`u16`): Owner user and group identifiers.
- **Single-User Metadata Model**: `permissions` (`mode`), `uid`, and `gid` are
  stored persistently on disk (and in `tmpfs`) and reported through `SYS_FD_STAT`
  / `SYS_PATH_STAT` (`struct stat`), but NotOS is a single-user educational OS
  and does **not** enforce permission or ownership checks. No multi-user security
  claims are made.
- **Compacted Payload Region (sectors 16..191 of bank)**: Up to **88 KiB
  (`90,112` bytes)** of contiguous file data per bank (both individual files and
  total filesystem payload are bounded to `90,112` bytes).

### Bounded Persistent Inode Recycling

Persistent inode numbers on `/disk` occupy the 16-bit range `256..65535` (keeping
`0..255` distinct for root, `/system`, `/dev`, pipes, and `tmpfs` within
Picolibc's 16-bit `ino_t`). When `next_inode` reaches `65535` and wraps,
`fs_core_allocate_inode` wraps the cursor back to `256` and scans at most
`FS_CORE_NODES + 1` (`49`) candidates to select an inode not currently in use by
any active entry in the snapshot. `next_inode` is preserved across `mkfs` within
a session so stale open descriptors cannot alias newly created files.

### Transactional Commit Protocol & Atomic `rename`

Every write, truncate, `mkdir`, `rmdir`, `unlink`, or `rename` (`SYS_RENAME`) on
`/disk` publishes a new complete snapshot into the inactive bank while keeping
the previously committed bank intact:
1. Invalidate the target bank's header sector on disk and issue `ATA FLUSH CACHE`.
2. Write the directory table and compacted payload sectors (`1..191`) and issue
   `FLUSH`.
3. Write the new valid header sector (`0`) and issue `FLUSH`.
4. Read back the entire written bank from disk and verify its header magic,
   version, generation, layout invariants, and CRC32 before switching the active
   in-memory snapshot.

Same-filesystem `rename(oldpath, newpath)` updates the target entry (and any
descendant paths when renaming a directory) and atomically replaces an existing
destination file or empty directory in a single dual-bank commit. Cross-mount
renames (such as `/tmp` to `/disk`) fail without side effects with
`-ERROR_CROSS_DEVICE` (`EXDEV`).

On mount, `fs_init` reads both `v3` banks, validates CRC32, node types, bounds,
parent directories, normalized paths, and unique inodes, and selects the valid
bank with the newer generation (using modular unsigned comparison). If the newer
bank was corrupted mid-write, NotFS automatically falls back to the intact
previous bank.

### Safe Migration from `NotFS v2` and `NotFS v1`

Because `NotFS v2` occupies sectors `0..127` (Bank 0) and `128..255` (Bank 1)
and `NotFS v1` occupies sectors `0..65`, **both legacy formats reside entirely
within sectors `0..255`**, which is completely disjoint from `NotFS v3` **Bank 1
at sectors `256..447`**.

If no valid `NotFS v3` bank is present on disk during `fs_init`:
1. NotFS first inspects the two 128-sector `NotFS v2` banks at sectors `0..127`
   and `128..255` using the retained `v2` validator (`fs_v2_valid`).
2. If no valid `v2` bank is found, NotFS inspects the two 33-sector `NotFS v1`
   banks at sectors `0..65`.
3. When migrating a valid `v2` (or `v1`) volume, NotFS converts all active nodes
   and payload into a `v3` snapshot and publishes it to **`NotFS v3` Bank 1
   (sectors `256..447`) first**, leaving all `v2` (`0..255`) and `v1` (`0..65`)
   sectors untouched until the `v3` bank is flushed and read-back verified on
   disk. Once a valid `v3` bank exists, it always takes precedence.

## Shell Storage & Directory Commands

When a data disk is attached, the shell starts with its working directory set to
`/disk` (even before `mkfs`); otherwise it starts in `/tmp`.

```text
mkfs
mkdir /disk/apps
cd /disk/apps
write hello.txt Hello NotOS
cat ./hello.txt
touch empty
stat hello.txt
rm hello.txt
rm empty
cd ..
rmdir apps
df
```

- `write PATH TEXT`: Creates or replaces a file in a single transactional
  publish.
- `touch PATH`: Creates an empty file or updates an existing file's modification
  timestamp (`mtime`) while preserving its content.
- `rm PATH` / `rmdir PATH`: Removes a file or an empty directory.
- `df`: Reports both `tmpfs` RAM page usage and `NotFS v3` persistent node/byte
  capacity (`48` nodes, `90,112` payload bytes).
- Paths are lexically normalized (`.`, `..`, repeated `/`), up to 63 bytes total
  and 23 characters per path component (`A-Z`, `a-z`, `0-9`, `.`, `-`, `_`).

## Verification

- `tests/fs_unit.c` (ASAN/UBSAN): Injects I/O failures at every sector write and
  flush step during `NotFS v3` commits, atomic `rename` (including overwrite and
  directory subtree rename), bounded inode recycling across 16-bit wrap, and
  `v2 -> v3` / `v1 -> v3` migrations, verifying that previous banks remain valid
  and mountable.
- `tests/storage.py`, `tests/files.py`, `tests/vfs.py`, and
  `tests/continuation.py`: Verify `mkfs`, directory/file CRUD, atomic `rename`,
  `opendir`/`readdir`/`closedir`/`rewinddir`, `mtime`/`mode`/`uid`/`gid` stat
  reporting, `v2 -> v3` and `v1 -> v3` migration across QEMU reboots, CRC
  corruption recovery, and unchanged boot image SHA-256.
