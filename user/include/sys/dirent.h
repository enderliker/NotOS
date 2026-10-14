#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_REG 8
#define DT_SOCK 12

struct dirent {
    ino_t d_ino;
    unsigned char d_type;
    char d_name[24];
};

typedef struct __notos_dir DIR;

#endif
