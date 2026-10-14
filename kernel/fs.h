#ifndef NOTOS_FS_H
#define NOTOS_FS_H
#include "file_abi.h"
#define FS_FILE_MAX (60u * 1024u)
#define FS_NODE_COUNT 24u
#define FS_BANK_SECTORS 128u
void fs_init(void);
int fs_ready(void);
int fs_file_size(const char *name);
int fs_file_inode(const char *name);
void fs_command(const char *command, const char *argument);
/* Canonical paths relative to /disk; no user pointers. Mutations return zero
 * or negative ABI errors; write returns its byte count. Publication preserves
 * the previous snapshot on failure; reads may be short at EOF. */
int fs_read_file(const char *name, void *buffer, u32 capacity);
int fs_read_range(const char *name, u32 offset, void *buffer, u32 bytes);
int fs_info(const char *name, struct notos_file_info *info);
int fs_open_file(const char *name, u32 flags, u32 mode);
int fs_write_range(const char *name, u32 inode, u32 offset, const void *buffer, u32 bytes);
int fs_truncate_file(const char *name, u32 inode, u32 bytes);
int fs_remove(const char *name, int directory);
int fs_replace(const char *name, const void *data, u32 bytes);
int fs_mkdir(const char *name, u32 mode);
int fs_entry(const char *directory, u32 index, char name[24], struct notos_file_info *info);
#endif
