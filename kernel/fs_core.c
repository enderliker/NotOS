#include "fs_core.h"
#include "syscall_numbers.h"
static void copy(void *destination, const void *source, u32 bytes) {
    u8 *output = destination;
    const u8 *input = source;
    while (bytes--) *output++ = *input++;
}
u32 fs_core_crc(const void *data, u32 bytes) {
    const u8 *input = data;
    u32 value = ~0u;
    for (u32 i = 0; i < bytes; i++) {
        value ^= i >= 12 && i < 16 ? 0 : input[i];
        for (u32 bit = 0; bit < 8; bit++)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
    }
    return ~value;
}
int fs_core_path(const char *path) {
    u32 component = 0, total = 0;
    const char *start = path;
    if (!path || !*path) return 0;
    for (;;) {
        char character = *path++;
        if (!character || character == '/') {
            if (!component || component > 23 || (component == 1 && start[0] == '.')
                || (component == 2 && start[0] == '.' && start[1] == '.')) return 0;
            if (!character) return total < 58;
            component = 0;
            start = path;
        } else {
            if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9') || character == '.' || character == '_' || character == '-')) return 0;
            component++;
        }
        if (++total >= 58) return 0;
    }
}
int fs_core_find(const struct fs_core_snapshot *snapshot, const char *path) {
    for (u32 i = 0; i < FS_CORE_NODES; i++) {
        if (!snapshot->entries[i].kind) continue;
        for (u32 j = 0; j < sizeof(snapshot->entries[i].path); j++) {
            if (snapshot->entries[i].path[j] != path[j]) break;
            if (!path[j]) return (int)i;
        }
    }
    return -1;
}
static int parent_exists(const struct fs_core_snapshot *snapshot, const char *path) {
    char parent[64];
    u32 bytes = length(path), slash = 0;
    for (u32 i = 0; i < bytes; i++) if (path[i] == '/') slash = i;
    if (!slash) return 1;
    copy(parent, path, slash);
    parent[slash] = 0;
    int index = fs_core_find(snapshot, parent);
    return index >= 0 && snapshot->entries[index].kind == NOTOS_FILE_DIRECTORY;
}
int fs_core_valid(const struct fs_core_snapshot *snapshot) {
    if (snapshot->magic != FS_CORE_MAGIC || snapshot->version != 2
        || snapshot->checksum != fs_core_crc(snapshot, sizeof(*snapshot))
        || snapshot->next_inode < 256 || snapshot->next_inode > 65536) return 0;
    u32 consumed = 0;
    for (u32 i = 0; i < FS_CORE_NODES; i++) {
        const struct fs_core_entry *entry = &snapshot->entries[i];
        if (!entry->kind) {
            if (entry->size || entry->path[0] || entry->inode || entry->permissions || entry->offset > consumed) return 0;
            continue;
        }
        if (entry->path[63] || !fs_core_path(entry->path) || !parent_exists(snapshot, entry->path)
            || entry->inode < 256 || entry->inode >= snapshot->next_inode || (entry->permissions & ~0777u)) return 0;
        if (entry->kind != NOTOS_FILE_REGULAR && entry->kind != NOTOS_FILE_DIRECTORY) return 0;
        if (entry->offset != consumed || entry->size > FS_CORE_DATA_BYTES - consumed
            || (entry->kind == NOTOS_FILE_DIRECTORY && entry->size)) return 0;
        consumed += entry->size;
        for (u32 j = 0; j < i; j++) if (snapshot->entries[j].kind
            && (equal(entry->path, snapshot->entries[j].path) || entry->inode == snapshot->entries[j].inode)) return 0;
    }
    return 1;
}
int fs_core_update(const struct fs_core_snapshot *source, struct fs_core_snapshot *destination,
        const char *path, u32 inode, u32 offset, const void *data, u32 bytes, u32 size) {
    int index = fs_core_find(source, path);
    if (index < 0 || (inode && source->entries[index].inode != inode)) return -ERROR_NOT_FOUND;
    if (size > FS_CORE_DATA_BYTES || offset > size || bytes > size - offset) return -ERROR_FILE_TOO_LARGE;
    u32 total = size;
    for (u32 i = 0; i < FS_CORE_NODES; i++) if ((int)i != index) {
        if (source->entries[i].size > FS_CORE_DATA_BYTES - total) return -ERROR_NO_SPACE;
        total += source->entries[i].size;
    }
    copy(destination, source, 4096);
    u32 cursor = 0;
    for (u32 i = 0; i < FS_CORE_NODES; i++) {
        struct fs_core_entry *entry = &destination->entries[i];
        if (!entry->kind) {
            entry->offset = 0;
            continue;
        }
        u32 previous = source->entries[i].size;
        entry->offset = cursor;
        if ((int)i == index) entry->size = size;
        u32 retained = previous < entry->size ? previous : entry->size;
        copy(destination->data + cursor, source->data + source->entries[i].offset, retained);
        for (u32 j = retained; j < entry->size; j++) destination->data[cursor + j] = 0;
        if ((int)i == index && bytes) copy(destination->data + cursor + offset, data, bytes);
        cursor += entry->size;
    }
    for (u32 i = cursor; i < FS_CORE_DATA_BYTES; i++) destination->data[i] = 0;
    return 0;
}
