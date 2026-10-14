# Physical Memory, PAE/NX Paging, and Kernel Heap

## Physical Memory & BIOS E820 (`kernel/physical.c`)

Before switching to protected mode, the BIOS bootloader queries the full BIOS
`INT 15h / EAX=E820h` memory map and stores the header at `0x5000` (up to 64
entries of 24 bytes, compatible with 20-byte ACPI 2.0 entries). If the BIOS does
not provide a valid E820 map, the kernel refuses to initialize dynamic frame
allocation.

The physical frame allocator manages **4096-byte (4 KiB) frames** using two
bitmaps: usable RAM frames and currently free frames.
- Overlapping non-RAM or reserved E820 regions always take precedence over
  usable RAM ranges.
- Only complete 4 KiB pages fully contained within usable E820 RAM are admitted.
- All of the first 1 MiB (`0x00000000..0x00100000`) and the high static/BSS
  kernel region (`0x00100000..__bss_end`, where `__bss_end < 0x00280000`) are
  explicitly reserved before any frame can be allocated.
- The physical manager caps managed RAM at **64 MiB** (tested in QEMU with 16,
  32, 64, and 128 MiB configurations).

```text
mem
frame-test
```

`frame-test` allocates every available free frame, writes/verifies boundary
patterns at the start and end of each page, rejects invalid and double frees,
and releases all acquired frames back to the pool. Allocated frames are returned
uninitialized; callers that require zeroed pages (such as user-space page
allocations) explicitly zero them.

## Bootloader & High-Memory Layout (`boot/boot.S`, `kernel/link.ld`)

The 512-byte BIOS boot sector loads two contiguous payload regions from the disk
image:
1. **Low kernel image (`0x10000..0x90000`, 512 KiB)**: Loaded via 16 reads of
   32 KiB (64 sectors) without crossing 64 KiB DMA boundaries, verified by the
   magic word at `0x8fffc`.
2. **High read-only payload (`0x100000..0x1c0000`, 768 KiB)**: Loaded in 24
   chunks of 32 KiB through the real-mode bounce buffer at `0x90000..0x98000`
   and copied into high memory (`1 MiB .. 1.75 MiB`) via BIOS
   `INT 15h / AH=87h`, verified by the magic word at `0x1bfffc`.

The initial supervisor stack sits at `0x9f000` (with an unmapped guard page at
`0x90000`), and kernel `.bss` starts at `0x1c0000` and is zeroed in
`kernel/entry.S`. The high payload holds read-only (`R/NX`) system blobs such as
`/system/curl.elf` and `/system/cacert.pem`.

## PAE & NX Paging (`kernel/paging.c`)

NotOS enables **Physical Address Extension (PAE)** and the **No-Execute (NX)**
bit (`CPUID.PAE`, `CPUID.NX`, `MSR EFER.NXE`, `CR4.PAE`, `CR0.PG`, and
`CR0.WP`):
- 4-entry Page Directory Pointer Table (PDPT) and 512-entry 64-bit Page
  Directories and Page Tables (4 KiB frames).
- When publishing a 64-bit PTE/PDE, the upper 32 bits (containing the `NX` bit
  and high physical address bits) are written before the lower 32-bit word with
  the `P` (Present) bit.
- **Strict `W^X` protection**:
  - Kernel `.text`: Supervisor Read/Execute (`RX`), write-protected even in ring
    0 via `CR0.WP`.
  - Kernel `.rodata` and high embedded `/system` blobs: Supervisor Read-Only /
    No-Execute (`R/NX`).
  - Kernel `.data`, `.bss`, stacks, and heap: Supervisor Read/Write /
    No-Execute (`RW/NX`).
  - Page `0x00000000` (null page) and stack guard page `0x00090000`: Not
    present (any access triggers `#PF`).
  - VGA framebuffer (`0xA0000..0xBFFFF`): Mapped uncacheable (`PWT`/`PCD`).
  - Dynamic supervisor mapping window (`0xd0000000..0xd03fffff`, 4 MiB):
    Supervisor-only virtual window used for the kernel heap and temporary frame
    aliases, with `INVLPG` TLB invalidation on map/unmap.

```text
vm
vm-test
fault null
fault readonly
fault guard
fault unmapped
```

## Dynamic Kernel Heap (`kernel/heap.c`, `kernel/heap_core.c`)

The kernel reserves a fixed **1 MiB virtual heap** at `0xd0001000..0xd0101000`
backed by 256 physical frames, bookended by unmapped guard pages at `0xd0000000`
and `0xd0101000`. If startup frame acquisition fails partway, all acquired
mappings and frames are rolled back.

- `kmalloc(bytes)`: Allocates a 16-byte aligned block; returns `NULL` if
  `bytes == 0` or out of memory.
- `kcalloc(count, bytes)`: Checks `count * bytes` for multiplication overflow
  and zeroes the allocated payload (leaving interrupts enabled while zeroing if
  they were enabled on entry).
- `kfree(pointer)`: Frees a valid allocated block (`NULL` is a safe no-op).
  Rejects out-of-range, interior, unaligned, and double-freed pointers.

The first-fit allocator (`heap_core.c`) splits blocks when sufficient space
remains for a new header and payload, coalesces adjacent free blocks on `kfree`,
and validates block header magic numbers, alignment, and neighbor links before
traversing the list. The heap is never called from IRQ handlers.

```text
heap
heap-test
alloc 128
alloc 4097
free 0
free 1
fault heap-end
```

Per-process user-space virtual memory (`brk`, `mmap`, and 64 KiB user stacks) is
documented in [ELF.md](ELF.md).
