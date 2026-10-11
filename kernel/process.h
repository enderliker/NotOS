#ifndef NOTOS_PROCESS_H
#define NOTOS_PROCESS_H
#include "base.h"
u32 process_spawn_elf_args(const char *name,const u8 *image,u32 bytes,const char *argument);
/* Return a managed PID, or zero after rolling back failed creation. */
u32 process_spawn_elf_split(const char *name,const u8 *image,u32 bytes,const char *arguments);
u32 process_spawn_elf(const char *name,const u8 *image,u32 bytes);
void process_poll(void);
void process_input_command(const char *argument,int eof);
void process_command(const char *command, const char *argument);
/* Creates a managed ring-3 task; image copied into private code/data pages. */
u32 process_spawn_image(const char *name, const u8 *code, u32 code_bytes,
    const u8 *data, u32 data_bytes);
#endif
