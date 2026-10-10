#ifndef NOTOS_USER_NETDB_H
#define NOTOS_USER_NETDB_H
#include <sys/socket.h>
struct addrinfo {
    int ai_flags,ai_family,ai_socktype,ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};
#define AI_PASSIVE 1
#define AI_CANONNAME 2
#define AI_NUMERICHOST 4
#define AI_NUMERICSERV 8
#define EAI_BADFLAGS -1
#define EAI_NONAME -2
#define EAI_AGAIN -3
#define EAI_FAIL -4
#define EAI_FAMILY -6
#define EAI_SOCKTYPE -7
#define EAI_SERVICE -8
#define EAI_MEMORY -10
#define EAI_SYSTEM -11
int getaddrinfo(const char *,const char *,const struct addrinfo *,struct addrinfo **);
void freeaddrinfo(struct addrinfo *);
const char *gai_strerror(int);
#endif
