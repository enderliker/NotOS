/* Freestanding memory primitives required by the cryptographic library. */
#include <stddef.h>
void *memcpy(void *destination,const void *source,size_t bytes) {
    unsigned char *dst=destination;const unsigned char *src=source;
    while (bytes--) *dst++=*src++;
    return destination;
}
void *memmove(void *destination,const void *source,size_t bytes) {
    unsigned char *dst=destination;const unsigned char *src=source;
    if ((unsigned long)dst<(unsigned long)src) return memcpy(destination,source,bytes);
    while (bytes) {bytes--;dst[bytes]=src[bytes];}
    return destination;
}
void *memset(void *destination,int value,size_t bytes) {
    unsigned char *dst=destination;while (bytes--) *dst++=(unsigned char)value;return destination;
}
int memcmp(const void *first,const void *second,size_t bytes) {
    const unsigned char *a=first,*b=second;
    while (bytes--) {if (*a!=*b) return *a<*b?-1:1;a++;b++;}
    return 0;
}
size_t strlen(const char *text) {size_t n=0;while (text[n]) n++;return n;}
