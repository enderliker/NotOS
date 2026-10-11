#include "file_command.h"
#include "path_core.h"
#include "fs.h"
#include "disk.h"
#include "vfs.h"
#include "syscall_numbers.h"

static char cwd[NOTOS_PATH_BYTES] = "/tmp";

static void file_error(int error) {
    console_puts("FS ERROR: ");
    if (error == -ERROR_NOT_FOUND) console_puts("file not found");
    else if (error == -ERROR_READ_ONLY) console_puts("read-only path");
    else if (error == -ERROR_NO_SPACE) console_puts("no space");
    else if (error == -ERROR_MEMORY) console_puts("out of memory");
    else if (error == -ERROR_NAME_TOO_LONG) console_puts("path too long");
    else if (error == -ERROR_IO) console_puts("not mounted or I/O failure");
    else console_puts("invalid name");
    console_putc('\n');
}

void file_command_init(void) {
    const char *initial = disk_sectors() ? "/disk" : "/tmp";
    for (u32 i = 0; (cwd[i] = initial[i]); i++) {}
}

static void file_list(const char *path) {
    if (equal(path, "/")) console_puts("tmp/\ndisk/\ndev/\n");
    else if (equal(path, "/dev")) console_puts("stdin\nstdout\nstderr\nconsole\nnull\n");
    else if (equal(path, "/disk")) {
        fs_command("ls", "");
        return;
    } else if (equal(path, "/tmp")) {
        for (u32 i = 0; i < VFS_NODE_COUNT; i++) {
            char name[24];
            u32 bytes;
            if (!vfs_entry(i, name, &bytes)) continue;
            console_puts("FILE: ");
            console_puts(name);
            console_puts(" bytes=");
            console_uint(bytes);
            console_putc('\n');
        }
    } else {
        struct notos_file_info info;
        int error = vfs_stat(path, &info);
        if (error) { file_error(error); return; }
        console_puts(path);
        console_putc('\n');
    }
    console_puts("FS: list complete\n");
}

static void file_cat(const char *path) {
    struct vfs_node *node;
    int error = vfs_open(path, NOTOS_O_RDONLY, 0, &node);
    if (error) { file_error(error); return; }
    u8 buffer[256];
    u32 offset = 0;
    int bytes;
    while ((bytes = vfs_read(node, offset, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < bytes; i++) console_putc((char)buffer[i]);
        offset += (u32)bytes;
    }
    vfs_release(node);
    if (bytes < 0) file_error(bytes);
    else console_putc('\n');
}

static void file_write(const char *path, const char *text, int touch) {
    struct vfs_node *node;
    u32 flags = NOTOS_O_WRONLY | NOTOS_O_CREAT;
    if (!touch) flags |= NOTOS_O_TRUNC;
    int error = vfs_open(path, flags, 0644, &node);
    if (error) { file_error(error); return; }
    int result = touch ? 0 : vfs_write(node, 0, text, length(text));
    vfs_release(node);
    if (result < 0) file_error(result);
    else console_puts("FS: file updated\n");
}

static void file_disk(const char *operation, const char *name, const char *text) {
    if (equal(operation, "touch")) {
        if (fs_file_size(name) >= 0) { console_puts("FS: file unchanged\n"); return; }
        fs_command("write", name);
        return;
    }
    char argument[192];
    u32 used = length(name);
    for (u32 i = 0; i <= used; i++) argument[i] = name[i];
    if (equal(operation, "write")) {
        u32 bytes = length(text);
        if (bytes >= sizeof(argument) - used - 1) { file_error(-ERROR_NAME_TOO_LONG); return; }
        argument[used++] = ' ';
        for (u32 i = 0; i <= bytes; i++) argument[used + i] = text[i];
    }
    fs_command(operation, argument);
}

static void file_stat(const char *path) {
    if (path_directory(path)) {
        console_puts("STAT: directory ");
        console_puts(path);
        console_putc('\n');
        return;
    }
    struct notos_file_info info;
    int error = vfs_stat(path, &info);
    if (error) { file_error(error); return; }
    console_puts("STAT: ");
    console_puts(path);
    console_puts(" bytes=");
    console_uint(info.size);
    console_puts(" inode=");
    console_uint(info.inode);
    console_putc('\n');
}

static void file_space(void) {
    struct vfs_counts counts = vfs_counts();
    console_puts("DF: /tmp allocated_bytes=");
    console_uint(counts.pages * 4096);
    console_puts(" capacity_bytes=");
    console_uint(VFS_PAGE_COUNT * 4096);
    console_puts(" (shared with disk snapshots)\nDF: /disk sectors=");
    console_uint(disk_sectors());
    console_puts(fs_ready() ? " mounted=1" : " mounted=0");
    console_puts(" files_capacity=8 file_bytes_max=1536\n");
}

int file_command(const char *line) {
    char operation[8];
    u32 used = 0;
    while (*line && *line != ' ' && used < sizeof(operation) - 1) operation[used++] = *line++;
    operation[used] = 0;
    if (*line && *line != ' ') return 0;
    int recognized = equal(operation, "cd") || equal(operation, "pwd")
        || equal(operation, "ls") || equal(operation, "cat")
        || equal(operation, "write") || equal(operation, "touch")
        || equal(operation, "rm") || equal(operation, "stat")
        || equal(operation, "df") || equal(operation, "mkfs");
    if (!recognized) return 0;
    while (*line == ' ') line++;
    if (equal(operation, "pwd") || equal(operation, "df") || equal(operation, "mkfs")) {
        if (*line) { file_error(-ERROR_INVALID_ARGUMENT); return 1; }
        if (equal(operation, "pwd")) { console_puts(cwd); console_putc('\n'); }
        else if (equal(operation, "df")) file_space();
        else fs_command("mkfs", "");
        return 1;
    }
    char input[NOTOS_PATH_BYTES];
    used = 0;
    while (*line && *line != ' ') {
        if (used == sizeof(input) - 1) { file_error(-ERROR_NAME_TOO_LONG); return 1; }
        input[used++] = *line++;
    }
    input[used] = 0;
    while (*line == ' ') line++;
    if ((!used && !equal(operation, "cd") && !equal(operation, "ls"))
        || (*line && !equal(operation, "write"))) {
        file_error(-ERROR_INVALID_ARGUMENT);
        return 1;
    }
    char path[NOTOS_PATH_BYTES];
    const char *target = !used && equal(operation, "cd") ? "/" : input;
    int error = path_resolve(cwd, target, path);
    if (error) { file_error(error); return 1; }
    if (equal(operation, "cd")) {
        if (!path_directory(path)) { file_error(-ERROR_NOT_FOUND); return 1; }
        for (u32 i = 0; (cwd[i] = path[i]); i++) {}
    } else if (equal(operation, "ls")) file_list(path);
    else if (equal(operation, "stat")) file_stat(path);
    else if (starts(path, "/disk/")) file_disk(operation, path + 6, line);
    else if (equal(operation, "cat")) file_cat(path);
    else if (equal(operation, "rm")) {
        error = vfs_unlink(path);
        if (error) file_error(error);
        else console_puts("FS: file removed\n");
    } else file_write(path, line, equal(operation, "touch"));
    return 1;
}
