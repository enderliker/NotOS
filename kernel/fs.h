#ifndef NOTOS_FS_H
#define NOTOS_FS_H
#include "base.h"
#define FS_FILE_MAX 1536u
void fs_init(void);
int fs_ready(void);
int fs_file_size(const char *name);
int fs_file_inode(const char *name);
void fs_command(const char *command, const char *argument);
/* Copy into a caller-owned buffer; return bytes, or -1 on failure. */
int fs_read_file(const char *name, void *buffer, u32 capacity);
#endif
