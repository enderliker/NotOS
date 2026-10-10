#ifndef NOTOS_USER_INET_H
#define NOTOS_USER_INET_H
#include <sys/socket.h>
typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;
struct in_addr {in_addr_t s_addr;};
struct sockaddr_in {sa_family_t sin_family;in_port_t sin_port;struct in_addr sin_addr;unsigned char sin_zero[8];};
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define INADDR_ANY 0u
#define INADDR_LOOPBACK 0x7f000001u
#define INADDR_NONE 0xffffffffu
uint16_t htons(uint16_t);
uint16_t ntohs(uint16_t);
uint32_t htonl(uint32_t);
uint32_t ntohl(uint32_t);
#endif
