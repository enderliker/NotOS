# NotFS v2 and ATA PIO Storage

## ATA PIO Driver & Data Disk Safety (`kernel/disk.c`)

NotOS includes a 28-bit LBA ATA PIO driver strictly locked to the **IDE
primary-slave drive (`index=1`, I/O base `0x1F0`, drive select `0xB0`)**. The
boot drive (`index=0`, primary-master) is never selected for writes.

- `make run` automatically creates a 128 KiB `data.img` file (if absent) outside
  `build/` and Git so persistent data survives `make clean`.
- Set `NOTOS_DATA_DISK=/path/to/data.img` to attach an alternate disk image.
- An unformatted or absent disk is never formatted automatically; `mkfs` must be
  run explicitly by the user to initialize or wipe the data disk.

## Dual-Bank Transactional `NotFS v2` (`kernel/fs_core.c`, `kernel/fs.c`)

`NotFS v2` occupies two 64 KiB banks (128 sectors each: Bank 0 at sectors
`0..127` and Bank 1 at sectors `128..255`):
- **Header (512 bytes)**: Magic (`NOTFS2`), format version, monotonic generation
  counter, entry count, payload byte count, monotonic `next_inode` counter, and
  CRC32 checksums covering both the directory table and payload.
- **Directory Table**: Up to **24 nodes** (files or directories) of 84 bytes
  each (padded to 4096 bytes), storing `path[64]`, byte `size`, payload
  `offset`, node `type`, persistent `inode` (`256..65535`), and `mode`.
- **Compacted Payload Region**: Up to **60 KiB (`61,440` bytes)** of contiguous
  file data per bank (both individual files and total filesystem payload are
  bounded to 61,440 bytes).

### Transactional Commit Protocol

Every write, truncate, `mkdir`, `rmdir`, or `unlink` on `/disk` publishes a new
complete snapshot into the inactive bank while keeping the previously committed
bank intact:
1. Invalidate the target bank's header sector on disk and issue `ATA FLUSH CACHE`.
2. Write the directory table and compacted payload sectors and issue `FLUSH`.
3. Write the new valid header sector and issue `FLUSH`.
4. Read back the entire written bank from disk and verify its header magic,
   generation, layout invariants, and CRC32 before switching the active
   in-memory snapshot.

On mount, `fs_init` reads both banks, validates CRC32, node types, bounds,
parent directories, normalized paths, and unique inodes, and selects the valid
bank with the newer generation (using modular unsigned comparison). If the newer
bank was corrupted mid-write, NotFS automatically falls back to the intact
previous bank.

### Safe Migration from `NotFS v1`

If no valid `NotFS v2` bank is present on disk, `fs_init` checks the two legacy
33-sector `NotFS v1` banks at sectors `0..65`. When migrating a valid `v1`
volume, NotFS writes and verifies the new `v2` snapshot in **Bank 1 (sectors
`128..255`) first**, leaving both legacy `v1` banks (`0..65`) untouched until
the `v2` bank is verified on disk. Once a valid `v2` bank exists, it always
takes precedence over `v1`.

Persistent inode numbers (`256..65535`) increment monotonically across file
creations and are preserved across `mkfs` within a session so stale open file
descriptors cannot alias newly created files after formatting.

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
- `touch PATH`: Creates an empty file or preserves an existing file's content.
- `rm PATH` / `rmdir PATH`: Removes a file or an empty directory.
- `df`: Reports both `tmpfs` RAM page usage and `NotFS v2` persistent node/byte
  capacity.
- Paths are lexically normalized (`.`, `..`, repeated `/`), up to 63 bytes total
  and 23 characters per path component (`A-Z`, `a-z`, `0-9`, `.`, `-`, `_`).

## Verification

- `tests/fs_unit.c` (ASAN/UBSAN): Injects I/O failures at every sector write and
  flush step during `NotFS v2` commits and `v1 -> v2` migration, verifying that
  previous banks remain valid and mountable.
- `tests/storage.py`, `tests/files.py`, and `tests/continuation.py`: Verify
  `mkfs`, directory/file CRUD, persistence across QEMU reboots, CRC corruption
  recovery, user-space `/disk` writes, and unchanged boot image SHA-256.
