#ifndef NOTOS_FD_H
#define NOTOS_FD_H
#include "file_abi.h"
#include "poll_abi.h"
/* Descriptor slots own references to shared open-file descriptions. */
struct fd_table {
    u16 slots[NOTOS_FD_COUNT];
    u8 descriptor_flags[NOTOS_FD_COUNT];
    u8 input[NOTOS_INPUT_BYTES];
    u32 input_head,input_count,input_eof;
};
int fd_table_init(struct fd_table *table);
void fd_table_destroy(struct fd_table *table);
int fd_open(struct fd_table *table,const char *path,u32 flags,u32 mode);
int fd_close(struct fd_table *table,int fd);
int fd_read(struct fd_table *table,int fd,void *buffer,u32 bytes);
int fd_write(struct fd_table *table,int fd,const void *buffer,u32 bytes);
int fd_seek(struct fd_table *table,int fd,int offset,u32 origin);
int fd_truncate(struct fd_table *table,int fd,u32 bytes);
int fd_stat(struct fd_table *table,int fd,struct notos_file_info *result);
int fd_path_stat(const char *path,struct notos_file_info *result);
int fd_dup(struct fd_table *table,int fd,int destination);
int fd_control(struct fd_table *table,int fd,u32 command,u32 argument);
int fd_is_tty(struct fd_table *table,int fd);
int fd_blocking_input(struct fd_table *table,int fd);
int fd_feed(struct fd_table *table,const void *buffer,u32 bytes,int eof);
u32 fd_description_count(void);
int fd_socket_open(struct fd_table *,u32 flags);
int fd_socket_handle(struct fd_table *,int fd);
int fd_nonblocking(struct fd_table *,int fd);
/* Trusted vectors only. Invalid fds report NVAL, negative entries are ignored. */
int fd_poll(struct fd_table *,struct notos_pollfd *,u32 count);
#endif
