#ifndef NOTOS_FILE_COMMAND_H
#define NOTOS_FILE_COMMAND_H
#include "file_abi.h"
/* Initialize shell cwd after the dedicated data disk has been probed. */
void file_command_init(void);
int file_resolve(const char *input, char path[NOTOS_PATH_BYTES]);
const char *file_cwd(void);
/* Return 1 when a filesystem command was handled, including invalid arguments. */
int file_command(const char *line);
#endif
