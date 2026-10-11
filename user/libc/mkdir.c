/* Stub: NotOS VFS is flat and lacks subdirectory support (see docs/VFS.md).
 * To accommodate tools like curl (--create-dirs), path creations under /tmp/
 * simulate success, while any other path returns ENOTSUP. */
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

int mkdir(const char *path, mode_t mode) {
    (void)mode;
    if (path && (strcmp(path, "/tmp") == 0 || strncmp(path, "/tmp/", 5) == 0)) return 0;
    errno = ENOTSUP;
    return -1;
}
