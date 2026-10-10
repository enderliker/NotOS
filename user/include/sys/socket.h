#ifndef NOTOS_USER_SOCKET_H
#define NOTOS_USER_SOCKET_H
#include <sys/types.h>
#include <stdint.h>
typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;
struct sockaddr {sa_family_t sa_family;char sa_data[14];};
#define AF_UNSPEC 0
#define AF_INET 2
#define AF_INET6 10
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_NONBLOCK 0x100
#define SOCK_CLOEXEC 0x200
#define SOL_SOCKET 1
#define SO_ERROR 1
#define SO_TYPE 2
#define SO_RCVTIMEO 3
#define SO_SNDTIMEO 4
#define SO_RCVBUF 6
#define SO_SNDBUF 7
#define SO_KEEPALIVE 8
#define SO_REUSEADDR 9
#define MSG_DONTWAIT 1
#define MSG_PEEK 2
#define MSG_NOSIGNAL 4
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
int socket(int,int,int);
int connect(int,const struct sockaddr *,socklen_t);
ssize_t send(int,const void *,size_t,int);
ssize_t recv(int,void *,size_t,int);
int shutdown(int,int);
int getsockopt(int,int,int,void *,socklen_t *);
int setsockopt(int,int,int,const void *,socklen_t);
int getsockname(int,struct sockaddr *,socklen_t *);
int getpeername(int,struct sockaddr *,socklen_t *);
#endif
