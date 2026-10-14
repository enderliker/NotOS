#include "executable.h"
#include "system_file.h"
#include "vfs.h"
#include "heap.h"
#include "process.h"
#include "file_command.h"
#include "syscall_numbers.h"

int executable_open(const char *path, struct executable_image *image) {
    *image = (struct executable_image){0};
    image->bytes = system_file_lookup(path, &image->size);
    if (image->bytes) return 0;
    struct vfs_node *node;
    int error = vfs_open(path, NOTOS_O_RDONLY, 0, &node);
    if (error) return error;
    struct notos_file_info info;
    vfs_node_info(node, &info);
    if (info.kind != NOTOS_FILE_REGULAR || !info.size || info.size > VFS_FILE_BYTES) {
        vfs_release(node);
        return -ERROR_BAD_EXECUTABLE;
    }
    image->owned = kmalloc(info.size);
    if (!image->owned) {vfs_release(node); return -ERROR_MEMORY;}
    int bytes = vfs_read(node, 0, image->owned, info.size);
    vfs_release(node);
    if (bytes != (int)info.size) {executable_close(image); return -ERROR_IO;}
    image->bytes = image->owned;
    image->size = info.size;
    return 0;
}
void executable_close(struct executable_image *image) {
    if (image->owned) kfree(image->owned);
    *image = (struct executable_image){0};
}
void executable_spawn_command(const char *arguments) {
    char input[NOTOS_PATH_BYTES], path[NOTOS_PATH_BYTES];
    u32 used = 0;
    while (*arguments == ' ') arguments++;
    while (*arguments && *arguments != ' ') {
        if (used == sizeof(input) - 1) {console_puts("ELF ERROR: path too long\n"); return;}
        input[used++] = *arguments++;
    }
    input[used] = 0;
    while (*arguments == ' ') arguments++;
    if (!used || file_resolve(input, path)) {console_puts("ELF ERROR: invalid path\n"); return;}
    struct executable_image image;
    int error = executable_open(path, &image);
    if (error) {console_puts("ELF ERROR: unable to open executable\n"); return;}
    const char *name = path;
    for (const char *cursor = path; *cursor; cursor++) if (*cursor == '/') name = cursor + 1;
    u32 pid = process_spawn_elf_split(name, image.bytes, image.size, arguments);
    executable_close(&image);
    if (!pid) {console_puts("ELF ERROR: invalid image, arguments, slot or memory\n"); return;}
    console_puts("ELF: started pid="); console_uint(pid); console_putc('\n');
}
