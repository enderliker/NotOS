#ifndef NOTOS_EXECUTABLE_H
#define NOTOS_EXECUTABLE_H
#include "base.h"
/* Returns zero or a negative ABI error; close releases the acquired image.
 * Immutable system pointers remain valid for the complete boot. */
struct executable_image { const u8 *bytes; u32 size; void *owned; };
int executable_open(const char *path, struct executable_image *image);
void executable_close(struct executable_image *image);
void executable_spawn_command(const char *arguments);
#endif
