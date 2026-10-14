#ifndef NOTOS_FS_CORE_H
#define NOTOS_FS_CORE_H
#include "file_abi.h"
#define FS_CORE_NODES 48u
#define FS_CORE_METADATA_BYTES 8192u
#define FS_CORE_DATA_BYTES (88u * 1024u)
#define FS_CORE_MAGIC 0x5346544eu
#define FS_V2_NODES 24u
#define FS_V2_DATA_BYTES (60u * 1024u)
struct fs_v2_entry {
    char path[64];
    u32 size, offset, kind, inode, permissions;
};
struct fs_v2_snapshot {
    u32 magic, version, generation, checksum, next_inode;
    u8 header_padding[492];
    struct fs_v2_entry entries[FS_V2_NODES];
    u8 metadata_padding[4096 - 512 - sizeof(struct fs_v2_entry) * FS_V2_NODES];
    u8 data[FS_V2_DATA_BYTES];
};
_Static_assert(sizeof(struct fs_v2_snapshot) == 65536, "NotFS v2 wire layout");
struct fs_core_entry {
    char path[64];
    u64 mtime;
    u32 size, offset, kind, inode, permissions;
    u16 uid, gid;
};
_Static_assert(sizeof(struct fs_core_entry) == 96, "NotFS v3 entry wire layout");
struct fs_core_snapshot {
    u32 magic, version, generation, checksum, next_inode;
    u8 header_padding[492];
    struct fs_core_entry entries[FS_CORE_NODES];
    u8 metadata_padding[FS_CORE_METADATA_BYTES - 512 - sizeof(struct fs_core_entry) * FS_CORE_NODES];
    u8 data[FS_CORE_DATA_BYTES];
};
_Static_assert(sizeof(struct fs_core_snapshot) == 98304, "NotFS v3 wire layout");
u32 fs_core_crc(const void *data, u32 bytes);
int fs_v2_valid(const struct fs_v2_snapshot *snapshot);
int fs_core_valid(const struct fs_core_snapshot *snapshot);
int fs_core_path(const char *path);
int fs_core_find(const struct fs_core_snapshot *snapshot, const char *path);
u32 fs_core_allocate_inode(struct fs_core_snapshot *snapshot);
/* Rebuild payloads into a separate destination; failure preserves source and destination. */
int fs_core_update(const struct fs_core_snapshot *source, struct fs_core_snapshot *destination,
    const char *path, u32 inode, u32 offset, const void *data, u32 bytes, u32 size);
int fs_core_rename(const struct fs_core_snapshot *source, struct fs_core_snapshot *destination,
    const char *old_path, const char *new_path);
#endif

