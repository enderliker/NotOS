#ifndef NOTOS_USER_ARPA_H
#define NOTOS_USER_ARPA_H
#include <netinet/in.h>
#define INET_ADDRSTRLEN 16
int inet_pton(int,const char *,void *);
const char *inet_ntop(int,const void *,char *,socklen_t);
#endif
