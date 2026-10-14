#include "os.h"
#include <sys/stat.h>

int mkdir(const char *path, mode_t mode) {
    return notos_checked(notos_raw_call(SYS_MKDIR, (uint32_t)path, (uint32_t)mode, 0));
}
