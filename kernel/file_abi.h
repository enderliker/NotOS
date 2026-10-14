#ifndef NOTOS_FILE_ABI_H
#define NOTOS_FILE_ABI_H
#include "base.h"
/* Stable NotOS wire values, deliberately independent of libc's constants. */
#define NOTOS_PATH_BYTES 64u
#define NOTOS_IO_LIMIT 4096u
#define NOTOS_FD_COUNT 16u
#define NOTOS_INPUT_BYTES 256u
#define NOTOS_O_RDONLY 0u
#define NOTOS_O_WRONLY 1u
#define NOTOS_O_RDWR 2u
#define NOTOS_O_ACCMODE 3u
#define NOTOS_O_APPEND 4u
#define NOTOS_O_CREAT 8u
#define NOTOS_O_EXCL 16u
#define NOTOS_O_TRUNC 32u
#define NOTOS_O_NONBLOCK 64u
#define NOTOS_O_CLOEXEC 128u
#define NOTOS_O_ALL 255u
#define NOTOS_F_GETFL 0u
#define NOTOS_F_SETFL 1u
#define NOTOS_F_GETFD 2u
#define NOTOS_F_SETFD 3u
#define NOTOS_FD_CLOEXEC 1u
#define NOTOS_SEEK_SET 0u
#define NOTOS_SEEK_CUR 1u
#define NOTOS_SEEK_END 2u
#define NOTOS_FILE_REGULAR 1u
#define NOTOS_FILE_CHARACTER 2u
#define NOTOS_FILE_SOCKET 3u
#define NOTOS_FILE_DIRECTORY 4u
#define NOTOS_FILE_FIFO 5u
struct notos_directory_entry { u32 inode, kind, size; char name[24]; };
_Static_assert(sizeof(struct notos_directory_entry) == 36, "directory wire ABI");
struct notos_file_info { u32 inode, size, kind, links, permissions; u16 uid, gid; u64 mtime; };
_Static_assert(sizeof(struct notos_file_info) == 32, "file stat wire ABI");
#endif

