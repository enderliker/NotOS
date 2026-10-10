#ifndef NOTOS_SOCKET_STACK_H
#define NOTOS_SOCKET_STACK_H
#include "socket_abi.h"
struct fd_table;
void socket_stack_init(const u8 mac[6],const u8 ip[4],const u8 gateway[4],const u8 dns[4]);
void socket_stack_configure(const u8 ip[4],const u8 gateway[4],const u8 dns[4]);
int socket_stack_input(const void *frame,u32 bytes);
void socket_stack_poll(void);
int socket_stack_busy(void);
void socket_stack_status(void);
void socket_stack_drop(const char *kind);
int socket_create(void);
void socket_release(int handle);
int socket_connect(int handle,const struct notos_socket_address *address);
int socket_connected(int handle);
int socket_read(int handle,void *buffer,u32 bytes,u32 flags);
int socket_write(int handle,const void *buffer,u32 bytes);
int socket_shutdown(int handle,u32 how);
int socket_get_option(int handle,u32 option,u32 *value);
int socket_set_option(int handle,u32 option,u32 value);
int socket_get_address(int handle,int peer,struct notos_socket_address *address);
u32 socket_timeout(int handle,int writing);
void socket_connect_timeout(int handle);
int socket_resolve_start(struct fd_table *owner,const char *name);
int socket_resolve_result(struct fd_table *owner,u32 token,u8 result[4]);
void socket_resolve_cancel(struct fd_table *owner);
#endif
