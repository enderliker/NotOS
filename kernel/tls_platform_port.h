#ifndef NOTOS_TLS_PLATFORM_PORT_H
#define NOTOS_TLS_PLATFORM_PORT_H
#include <stddef.h>
void *notos_tls_calloc(size_t count,size_t bytes);
void notos_tls_free(void *pointer);
long long notos_tls_time(long long *output);
/* Diagnostic formatting is not used by our client; unsupported requests fail. */
int notos_tls_snprintf(char *buffer,size_t bytes,const char *format,...);
int notos_tls_printf(const char *format,...);
#endif
