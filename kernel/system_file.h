#ifndef NOTOS_SYSTEM_FILE_H
#define NOTOS_SYSTEM_FILE_H
#include "base.h"
/* Immutable image-backed files. Pointers live for the complete boot. */
const u8 *system_file_lookup(const char *path, u32 *bytes);
u32 system_file_execution_budget(const u8 *image);
#endif
