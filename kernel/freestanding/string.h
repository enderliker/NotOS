#ifndef NOTOS_FREESTANDING_STRING_H
#define NOTOS_FREESTANDING_STRING_H
#include <stddef.h>
void *memcpy(void *destination,const void *source,size_t bytes);
void *memmove(void *destination,const void *source,size_t bytes);
void *memset(void *destination,int value,size_t bytes);
int memcmp(const void *first,const void *second,size_t bytes);
int strcmp(const char *a,const char *b);
int strncmp(const char *a,const char *b,size_t bytes);
char *strchr(const char *text,int ch);
char *strstr(const char *text,const char *needle);
size_t strlen(const char *text);
#endif
