/* NotFS: bounded flat files, two CRC-checked snapshots on the dedicated ATA disk. */
#include "fs.h"
#include "disk.h"
#define BANK_SECTORS 33u
#define BANK_BYTES (BANK_SECTORS * 512u)
#define FILE_COUNT 8u
#define NAME_BYTES 24u
#define FS_MAGIC 0x5346544eu
struct file_entry { char name[NAME_BYTES]; u32 size; u8 data[FS_FILE_MAX]; };
struct snapshot {
    u32 magic, version, generation, checksum;
    u8 header_padding[496];
    struct file_entry files[FILE_COUNT];
    u8 padding[BANK_BYTES - 512 - sizeof(struct file_entry) * FILE_COUNT];
};
_Static_assert(sizeof(struct snapshot) == BANK_BYTES, "disk snapshot layout");
static struct snapshot active, staging;
static int mounted;
static unsigned bank;
static void bytes_copy(void *dst, const void *src, u32 n) {
    u8 *d = dst; const u8 *s = src; while (n--) *d++ = *s++;
}
static void bytes_clear(void *dst, u32 n) { u8 *d = dst; while (n--) *d++ = 0; }
static u32 crc(const struct snapshot *snapshot) {
    const u8 *data = (const u8 *)snapshot;
    u32 value = ~0u;
    for (unsigned i = 0; i < BANK_BYTES; ++i) {
        value ^= i >= 12 && i < 16 ? 0 : data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
    }
    return ~value;
}
static int valid_name(const char *name) {
    unsigned n = 0;
    while (*name) {
        char c = *name++;
        if (++n >= NAME_BYTES || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return 0;
    }
    return n && !equal(name - n, ".") && !equal(name - n, "..");
}
static int valid(const struct snapshot *s) {
    if (s->magic != FS_MAGIC || s->version != 1 || s->checksum != crc(s)) return 0;
    for (unsigned i = 0; i < FILE_COUNT; ++i) {
        if (!s->files[i].name[0]) continue;
        if (s->files[i].name[NAME_BYTES-1] || s->files[i].size > FS_FILE_MAX
            || !valid_name(s->files[i].name)) return 0;
        for (unsigned j = 0; j < i; ++j)
            if (equal(s->files[i].name, s->files[j].name)) return 0;
    }
    return 1;
}
static int read_bank(unsigned which, struct snapshot *s) {
    for (unsigned i = 0; i < BANK_SECTORS; ++i)
        if (!disk_read(which * BANK_SECTORS + i, (u8 *)s + i * 512)) return 0;
    return valid(s);
}
static int publish(void) {
    unsigned next = mounted ? bank ^ 1u : 0;
    u8 invalid[512] = {0};
    staging.magic = FS_MAGIC; staging.version = 1;
    staging.generation = mounted ? active.generation + 1 : 1;
    staging.checksum = crc(&staging);
    if (!disk_write(next * BANK_SECTORS, invalid) || !disk_flush()) return 0;
    for (unsigned i = 1; i < BANK_SECTORS; ++i)
        if (!disk_write(next * BANK_SECTORS + i, (u8 *)&staging + i * 512)) return 0;
    if (!disk_flush() || !disk_write(next * BANK_SECTORS, &staging) || !disk_flush()) return 0;
    bytes_copy(&active, &staging, BANK_BYTES); bank = next; mounted = 1;
    return 1;
}
void fs_init(void) {
    mounted = 0;
    if (!disk_init() || disk_sectors() < BANK_SECTORS * 2) {
        console_puts("FS: no dedicated primary-slave disk\n"); return;
    }
    int first = read_bank(0, &active), second = read_bank(1, &staging);
    if (second && (!first || (int)(staging.generation - active.generation) > 0)) {
        bytes_copy(&active, &staging, BANK_BYTES); bank = 1; mounted = 1;
    } else if (first) { bank = 0; mounted = 1; }
    console_puts(mounted ? "FS: NotFS mounted\n" : "FS: unformatted; use mkfs (data disk only)\n");
}
static int find(const char *name) {
    for (unsigned i = 0; i < FILE_COUNT; ++i)
        if (equal(active.files[i].name, name)) return (int)i;
    return -1;
}
int fs_ready(void) {return mounted;}
int fs_file_inode(const char *name) {
    if (!mounted || !valid_name(name)) return 0;
    return find(name)+1;
}
int fs_file_size(const char *name) {
    if (!mounted || !valid_name(name)) return -1;
    int index=find(name);
    return index<0?-1:(int)active.files[index].size;
}
int fs_read_file(const char *name, void *buffer, u32 capacity) {
    if (!mounted || !valid_name(name) || !buffer) return -1;
    int index = find(name);
    if (index < 0 || active.files[index].size > capacity) return -1;
    bytes_copy(buffer, active.files[index].data, active.files[index].size);
    return (int)active.files[index].size;
}
void fs_command(const char *command, const char *argument) {
    if (equal(command, "mkfs")) {
        if (disk_sectors() < BANK_SECTORS * 2) {console_puts("FS ERROR: data disk unavailable\n");return;}
        /* Format also invalidates the other bank: old deleted files cannot reappear. */
        u8 invalid[512] = {0};
        if (!disk_write(0, invalid) || !disk_write(BANK_SECTORS, invalid) || !disk_flush()) {
            console_puts("FS ERROR: format I/O failed\n"); return;
        }
        mounted = 0; bytes_clear(&staging, BANK_BYTES);
        console_puts(publish() ? "FS: formatted dedicated data disk\n" : "FS ERROR: format I/O failed\n"); return;
    }
    if (!mounted) { console_puts("FS ERROR: not mounted\n"); return; }
    if (equal(command, "ls")) {
        for (unsigned i = 0; i < FILE_COUNT; ++i) if (active.files[i].name[0]) {
            console_puts("FILE: "); console_puts(active.files[i].name);
            console_puts(" bytes="); console_uint(active.files[i].size); console_putc('\n');
        }
        console_puts("FS: list complete\n"); return;
    }
    char name[NAME_BYTES]; unsigned n = 0;
    while (*argument && *argument != ' ' && n < NAME_BYTES-1) name[n++] = *argument++;
    name[n] = 0;
    if (!valid_name(name) || (*argument && *argument != ' ')) {console_puts("FS ERROR: invalid name\n");return;}
    int index = find(name);
    if (equal(command, "cat")) {
        if (*argument || index < 0) {console_puts("FS ERROR: file not found\n");return;}
        const struct file_entry *file = &active.files[index];
        for (unsigned i = 0; i < file->size; ++i) console_putc((char)file->data[i]);
        console_putc('\n'); return;
    }
    bytes_copy(&staging, &active, BANK_BYTES);
    if (equal(command, "rm")) {
        if (*argument || index < 0) {console_puts("FS ERROR: file not found\n");return;}
        bytes_clear(&staging.files[index], sizeof(struct file_entry));
    } else if (equal(command, "write")) {
        if (*argument == ' ') argument++;
        unsigned size = length(argument);
        if (size > FS_FILE_MAX) {console_puts("FS ERROR: file too large\n");return;}
        if (index < 0) {
            unsigned i = 0; while (i < FILE_COUNT && active.files[i].name[0]) i++;
            if (i == FILE_COUNT) {console_puts("FS ERROR: directory full\n");return;}
            index = (int)i;
        }
        struct file_entry *file = &staging.files[index];
        bytes_clear(file, sizeof(*file)); bytes_copy(file->name, name, n);
        file->size = size; bytes_copy(file->data, argument, size);
    } else {console_puts("FS ERROR: unknown operation\n");return;}
    console_puts(publish() ? "FS: transaction committed\n" : "FS ERROR: I/O failed; previous snapshot retained\n");
}
