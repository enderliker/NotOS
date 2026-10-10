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

int strcmp(const char *a,const char *b) {
    while (*a && *a==*b) {a++;b++;}return (unsigned char)*a-(unsigned char)*b;
}
int strncmp(const char *a,const char *b,size_t n) {
    while (n--) {if (*a!=*b || !*a) return (unsigned char)*a-(unsigned char)*b;a++;b++;}return 0;
}
char *strchr(const char *text,int ch) {
    do {if ((unsigned char)*text==(unsigned char)ch) return (char *)text;} while (*text++);return 0;
}
char *strstr(const char *text,const char *needle) {
    size_t n=strlen(needle);for (;*text;text++) if (!strncmp(text,needle,n)) return (char *)text;
    return n?0:(char *)text;
}

int atoi(const char *text) {
    while (*text==' ' || *text=='\t' || *text=='\n' || *text=='\r') text++;
    int negative=*text=='-';if (*text=='-' || *text=='+') text++;
    unsigned limit=negative?2147483648u:2147483647u,value=0;
    while (*text>='0' && *text<='9') {
        unsigned digit=(unsigned)(*text++-'0');
        if (value>(limit-digit)/10) return negative?(int)(0u-limit):(int)limit;
        value=value*10+digit;
    }
    return negative?(int)(0u-value):(int)value;
}
