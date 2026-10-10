#ifndef NOTOS_LWIP_CC_H
#define NOTOS_LWIP_CC_H
#include "base.h"
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_CTYPE_H 1
#define LWIP_NO_UNISTD_H 1
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "u"
void socket_stack_assert(const char *message) __attribute__((noreturn));
u32 socket_stack_random(void);
#define LWIP_PLATFORM_DIAG(message) do {} while (0)
#define LWIP_PLATFORM_ASSERT(message) socket_stack_assert(message)
#define LWIP_RAND() socket_stack_random()
#endif
