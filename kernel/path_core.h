#ifndef NOTOS_PATH_CORE_H
#define NOTOS_PATH_CORE_H
#include "file_abi.h"
/* Normalize a trusted path relative to cwd; output is unchanged on failure. */
int path_resolve(const char *cwd, const char *input, char output[NOTOS_PATH_BYTES]);
/* Recognize built-in mount roots; dynamic directories are validated by VFS. */
int path_directory(const char *path);
#endif
