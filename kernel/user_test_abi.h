#ifndef NOTOS_USER_TEST_ABI_H
#define NOTOS_USER_TEST_ABI_H
/* Bundled test-program protocol, not an application ABI. Also assembler-safe. */
#define TEST_MODE_KERNEL_READ 1
#define TEST_MODE_SPIN 8
#define TEST_MODE_WORKER 10
#define TEST_MODE_YIELD 11
#define TEST_MODE_FPU 12
#define TEST_DATA_MODE 0
#define TEST_DATA_RESULT 2
#define TEST_DATA_MARKER 3
#define TEST_DATA_COUNTER 6
#define TEST_SUCCESS_STATUS 42
#define TEST_SUCCESS_MARKER 0x12345678
#endif
