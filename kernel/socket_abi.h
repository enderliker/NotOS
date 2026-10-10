#ifndef NOTOS_SOCKET_ABI_H
#define NOTOS_SOCKET_ABI_H
#include "base.h"
#define NOTOS_SOCKET_COUNT 8u
#define NOTOS_SOCKET_BUFFER 8192u
#define NOTOS_AF_INET 2u
#define NOTOS_SOCK_STREAM 1u
#define NOTOS_MSG_DONTWAIT 1u
#define NOTOS_MSG_PEEK 2u
#define NOTOS_OPT_ERROR 1u
#define NOTOS_OPT_TYPE 2u
#define NOTOS_OPT_RCVTIMEO 3u
#define NOTOS_OPT_SNDTIMEO 4u
#define NOTOS_OPT_NODELAY 5u
#define NOTOS_OPT_RCVBUF 6u
#define NOTOS_OPT_SNDBUF 7u
struct notos_socket_address {u8 ip[4];u32 port;};
_Static_assert(sizeof(struct notos_socket_address)==8,"IPv4 endpoint wire ABI");
#endif
