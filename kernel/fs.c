/* Atomic snapshot publication on the dedicated data drive, including v1 migration. */
#include "fs.h"
#include "fs_core.h"
#include "disk.h"
#include "syscall_numbers.h"
static struct fs_core_snapshot active, staging, verified;
static int mounted;
static u32 bank;
static void copy(void *destination, const void *source, u32 bytes) {
    u8 *output = destination;
    const u8 *input = source;
    while (bytes--) *output++ = *input++;
}
static int read_bank(u32 which, struct fs_core_snapshot *snapshot) {
    for (u32 i = 0; i < FS_BANK_SECTORS; i++)
        if (!disk_read(which * FS_BANK_SECTORS + i, (u8 *)snapshot + i * 512)) return 0;
    return fs_core_valid(snapshot);
}
static int publish(void) {
    u32 next = mounted ? bank ^ 1u : 0;
    u8 invalid[512] = {0};
    staging.magic = FS_CORE_MAGIC;
    staging.version = 2;
    staging.generation = mounted ? active.generation + 1 : 1;
    staging.checksum = fs_core_crc(&staging, sizeof(staging));
    if (!fs_core_valid(&staging)) return 0;
    if (!disk_write(next * FS_BANK_SECTORS, invalid) || !disk_flush()) return 0;
    for (u32 i = 1; i < FS_BANK_SECTORS; i++)
        if (!disk_write(next * FS_BANK_SECTORS + i, (u8 *)&staging + i * 512)) return 0;
    if (!disk_flush() || !disk_write(next * FS_BANK_SECTORS, &staging) || !disk_flush()) return 0;
    if (!read_bank(next, &verified) || verified.generation != staging.generation) return 0;
    copy(&active, &verified, sizeof(active));
    bank = next;
    mounted = 1;
    return 1;
}
struct legacy_entry { char name[24]; u32 size; u8 data[1536]; };
struct legacy_snapshot {
    u32 magic, version, generation, checksum;
    u8 padding[496];
    struct legacy_entry entries[8];
    u8 tail[33 * 512 - 512 - sizeof(struct legacy_entry) * 8];
};
static int legacy_valid(const struct legacy_snapshot *snapshot) {
    if (snapshot->magic != FS_CORE_MAGIC || snapshot->version != 1
        || snapshot->checksum != fs_core_crc(snapshot, sizeof(*snapshot))) return 0;
    for (u32 i = 0; i < 8; i++) {
        const struct legacy_entry *entry = &snapshot->entries[i];
        if (!entry->name[0]) continue;
        if (entry->name[23] || !fs_core_path(entry->name) || entry->size > 1536) return 0;
        for (const char *name = entry->name; *name; name++) if (*name == '/') return 0;
        for (u32 j = 0; j < i; j++) if (equal(entry->name, snapshot->entries[j].name)) return 0;
    }
    return 1;
}
static int migrate(void) {
    struct legacy_snapshot *first = (void *)&staging, *second = (void *)&verified;
    for (u32 i = 0; i < 33; i++) {
        if (!disk_read(i, (u8 *)first + i * 512) || !disk_read(33 + i, (u8 *)second + i * 512)) return 0;
    }
    int first_valid = legacy_valid(first), second_valid = legacy_valid(second);
    if (!first_valid && !second_valid) return 0;
    const struct legacy_snapshot *source = second_valid && (!first_valid
        || (int)(second->generation - first->generation) > 0) ? second : first;
    active = (struct fs_core_snapshot){.magic=FS_CORE_MAGIC,.version=2,.next_inode=256};
    u32 offset = 0;
    for (u32 i = 0; i < 8; i++) if (source->entries[i].name[0]) {
        struct fs_core_entry *entry = &active.entries[i];
        copy(entry->path, source->entries[i].name, 24);
        entry->kind = NOTOS_FILE_REGULAR;
        entry->size = source->entries[i].size;
        entry->offset = offset;
        entry->inode = active.next_inode++;
        entry->permissions = 0644;
        copy(active.data + offset, source->entries[i].data, entry->size);
        offset += entry->size;
    }
    /* Both old banks occupy sectors 0..65. First publish v2 in sectors 128..255. */
    active.generation = source->generation;
    copy(&staging, &active, sizeof(staging));
    mounted = 1;
    bank = 0;
    if (!publish()) { mounted = 0; return 0; }
    console_puts("FS: migrated NotFS v1 to v2\n");
    return 1;
}
void fs_init(void) {
    mounted = 0;
    if (!disk_init() || disk_sectors() < 2 * FS_BANK_SECTORS) {
        console_puts("FS: no dedicated primary-slave disk\n");
        return;
    }
    int first = read_bank(0, &active), second = read_bank(1, &staging);
    if (second && (!first || (int)(staging.generation - active.generation) > 0)) {
        copy(&active, &staging, sizeof(active));
        mounted = 1;
        bank = 1;
    } else if (first) { mounted = 1; bank = 0; }
    else migrate();
    console_puts(mounted ? "FS: NotFS mounted\n" : "FS: unformatted; use mkfs (data disk only)\n");
}
int fs_ready(void) { return mounted; }
int fs_file_inode(const char *name) {
    int index = mounted ? fs_core_find(&active, name) : -1;
    return index < 0 ? 0 : (int)active.entries[index].inode;
}
int fs_info(const char *name, struct notos_file_info *info) {
    if (!mounted) return -ERROR_IO;
    int index = fs_core_find(&active, name);
    if (index < 0) return -ERROR_NOT_FOUND;
    const struct fs_core_entry *entry = &active.entries[index];
    *info = (struct notos_file_info){entry->inode,entry->size,entry->kind,1,entry->permissions};
    return 0;
}
int fs_file_size(const char *name) {
    struct notos_file_info info;
    return fs_info(name, &info) || info.kind != NOTOS_FILE_REGULAR ? -1 : (int)info.size;
}
int fs_read_range(const char *name, u32 offset, void *buffer, u32 bytes) {
    int index = mounted ? fs_core_find(&active, name) : -1;
    if (index < 0) return -ERROR_NOT_FOUND;
    const struct fs_core_entry *entry = &active.entries[index];
    if (entry->kind != NOTOS_FILE_REGULAR) return -ERROR_IS_DIRECTORY;
    if (offset >= entry->size) return 0;
    if (bytes > entry->size - offset) bytes = entry->size - offset;
    copy(buffer, active.data + entry->offset + offset, bytes);
    return (int)bytes;
}
int fs_read_file(const char *name, void *buffer, u32 capacity) {
    int size = fs_file_size(name);
    return size < 0 || (u32)size > capacity ? -1 : fs_read_range(name, 0, buffer, (u32)size);
}
static int create(const char *name, u32 kind, u32 mode) {
    if (!mounted) return -ERROR_IO;
    if (!fs_core_path(name)) return -ERROR_INVALID_ARGUMENT;
    if (fs_core_find(&active, name) >= 0) return -ERROR_EXISTS;
    char parent[64];
    u32 slash = 0;
    for (u32 i = 0; name[i]; i++) if (name[i] == '/') slash = i;
    if (slash) {
        copy(parent, name, slash);
        parent[slash] = 0;
        struct notos_file_info info;
        int error = fs_info(parent, &info);
        if (error) return error;
        if (info.kind != NOTOS_FILE_DIRECTORY) return -ERROR_NOT_DIRECTORY;
    }
    u32 index = 0;
    while (index < FS_NODE_COUNT && active.entries[index].kind) index++;
    if (index == FS_NODE_COUNT || active.next_inode > 65535) return -ERROR_NO_SPACE;
    copy(&staging, &active, sizeof(staging));
    struct fs_core_entry *entry = &staging.entries[index];
    *entry = (struct fs_core_entry){.kind=kind,.inode=staging.next_inode++,.permissions=mode&0777};
    copy(entry->path, name, length(name) + 1);
    /* Empty entries still carry the compact payload offset of preceding entries. */
    for (u32 i = 0; i < index; i++) entry->offset += staging.entries[i].size;
    return publish() ? 0 : -ERROR_IO;
}
int fs_mkdir(const char *name, u32 mode) { return create(name, NOTOS_FILE_DIRECTORY, mode); }
int fs_truncate_file(const char *name, u32 inode, u32 bytes) {
    struct notos_file_info info;
    int error = fs_info(name, &info);
    if (error) return error;
    if (info.kind != NOTOS_FILE_REGULAR) return -ERROR_IS_DIRECTORY;
    error = fs_core_update(&active, &staging, name, inode, 0, 0, 0, bytes);
    return error ? error : publish() ? 0 : -ERROR_IO;
}
int fs_write_range(const char *name, u32 inode, u32 offset, const void *buffer, u32 bytes) {
    struct notos_file_info info;
    int error = fs_info(name, &info);
    if (error) return error;
    if (info.kind != NOTOS_FILE_REGULAR) return -ERROR_IS_DIRECTORY;
    if (offset > FS_FILE_MAX || bytes > FS_FILE_MAX - offset) return -ERROR_FILE_TOO_LARGE;
    if (!bytes) return 0;
    u32 size = info.size > offset + bytes ? info.size : offset + bytes;
    error = fs_core_update(&active, &staging, name, inode, offset, buffer, bytes, size);
    return error ? error : publish() ? (int)bytes : -ERROR_IO;
}
int fs_open_file(const char *name, u32 flags, u32 mode) {
    struct notos_file_info info;
    int error = fs_info(name, &info);
    if (error == -ERROR_NOT_FOUND && (flags & NOTOS_O_CREAT)) error = create(name, NOTOS_FILE_REGULAR, mode);
    else if (!error && (flags & (NOTOS_O_CREAT | NOTOS_O_EXCL)) == (NOTOS_O_CREAT | NOTOS_O_EXCL)) return -ERROR_EXISTS;
    if (error) return error;
    error = fs_info(name, &info);
    if (error) return error;
    if (info.kind != NOTOS_FILE_REGULAR) return -ERROR_IS_DIRECTORY;
    return (flags & NOTOS_O_TRUNC) && info.size ? fs_truncate_file(name, info.inode, 0) : 0;
}
int fs_remove(const char *name, int directory) {
    struct notos_file_info info;
    int error = fs_info(name, &info);
    if (error) return error;
    if (directory != (info.kind == NOTOS_FILE_DIRECTORY)) return directory ? -ERROR_NOT_DIRECTORY : -ERROR_IS_DIRECTORY;
    u32 bytes = length(name);
    for (u32 i = 0; i < FS_NODE_COUNT; i++) if (active.entries[i].kind
        && starts(active.entries[i].path, name) && active.entries[i].path[bytes] == '/') return -ERROR_NOT_EMPTY;
    int index = fs_core_find(&active, name);
    error = fs_core_update(&active, &staging, name, info.inode, 0, 0, 0, 0);
    if (error) return error;
    staging.entries[index] = (struct fs_core_entry){0};
    return publish() ? 0 : -ERROR_IO;
}
int fs_entry(const char *directory, u32 index, char name[24], struct notos_file_info *info) {
    if (!mounted) return -ERROR_IO;
    u32 position = 0;
    for (u32 i = 0; i < FS_NODE_COUNT; i++) {
        const struct fs_core_entry *entry = &active.entries[i];
        if (!entry->kind) continue;
        const char *child = entry->path;
        if (*directory) {
            u32 prefix = length(directory);
            if (!starts(child, directory) || child[prefix] != '/') continue;
            child += prefix + 1;
        }
        int nested = 0;
        for (const char *cursor = child; *cursor; cursor++) if (*cursor == '/') nested = 1;
        if (nested || position++ != index) continue;
        copy(name, child, length(child) + 1);
        fs_info(entry->path, info);
        return 1;
    }
    return 0;
}
void fs_command(const char *command, const char *argument) {
    if (equal(command, "mkfs")) {
        if (disk_sectors() < FS_BANK_SECTORS * 2) { console_puts("FS ERROR: data disk unavailable\n"); return; }
        u8 invalid[512] = {0};
        if (!disk_write(0, invalid) || !disk_write(FS_BANK_SECTORS, invalid) || !disk_flush()) {
            console_puts("FS ERROR: format I/O failed\n"); return;
        }
        u32 next_inode = mounted ? active.next_inode : 256;
        mounted = 0;
        staging = (struct fs_core_snapshot){.next_inode=next_inode};
        console_puts(publish() ? "FS: formatted dedicated data disk\n" : "FS ERROR: format I/O failed\n");
        return;
    }
    if (!mounted) { console_puts("FS ERROR: not mounted\n"); return; }
    if (equal(command, "ls")) {
        for (u32 i = 0; i < FS_NODE_COUNT; i++) {
            char name[24];
            struct notos_file_info info;
            int result = fs_entry(argument, i, name, &info);
            if (result <= 0) break;
            console_puts("FILE: "); console_puts(name); console_puts(" bytes="); console_uint(info.size); console_putc('\n');
        }
        console_puts("FS: list complete\n");
        return;
    }
    char name[64];
    u32 bytes = 0;
    while (*argument && *argument != ' ' && bytes < sizeof(name) - 1) name[bytes++] = *argument++;
    name[bytes] = 0;
    if (*argument && *argument != ' ') { console_puts("FS ERROR: invalid name\n"); return; }
    int error;
    if (equal(command, "write")) {
        if (*argument == ' ') argument++;
        error = fs_open_file(name, NOTOS_O_CREAT | NOTOS_O_WRONLY | NOTOS_O_TRUNC, 0644);
        if (!error) error = fs_write_range(name, (u32)fs_file_inode(name), 0, argument, length(argument));
    } else if (equal(command, "rm")) error = fs_remove(name, 0);
    else { console_puts("FS ERROR: unknown operation\n"); return; }
    console_puts(error < 0 ? "FS ERROR: I/O or capacity failure\n" : "FS: transaction committed\n");
}

int fs_replace(const char *name, const void *data, u32 bytes) {
    if (bytes > FS_FILE_MAX) return -ERROR_FILE_TOO_LARGE;
    int error = fs_open_file(name, NOTOS_O_CREAT | NOTOS_O_WRONLY, 0644);
    if (error) return error;
    error = fs_core_update(&active, &staging, name, (u32)fs_file_inode(name), 0, data, bytes, bytes);
    return error ? error : publish() ? 0 : -ERROR_IO;
}
