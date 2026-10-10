#ifndef NOTOS_FREESTANDING_STDINT_H
#define NOTOS_FREESTANDING_STDINT_H
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned int uintptr_t;
typedef int intptr_t;
#define INT32_MAX 2147483647
#define INT64_MAX 9223372036854775807LL
#define SIZE_MAX 4294967295U
#define UINT8_MAX 255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295U
#define UINT64_MAX 18446744073709551615ULL
#endif
