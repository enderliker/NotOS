#ifndef NOTOS_FILE_COMMAND_H
#define NOTOS_FILE_COMMAND_H
/* Initialize shell cwd after the dedicated data disk has been probed. */
void file_command_init(void);
/* Return 1 when a filesystem command was handled, including invalid arguments. */
int file_command(const char *line);
#endif
