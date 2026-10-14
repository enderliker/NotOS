#include "system_file.h"
#include "task.h"
extern const u8 _binary_trust_roots_pem_start[], _binary_trust_roots_pem_end[];
extern const u8 _binary_build_libc_app_elf_start[], _binary_build_libc_app_elf_end[];
extern const u8 _binary_build_curl_app_elf_start[], _binary_build_curl_app_elf_end[];
const u8 *system_file_lookup(const char *path, u32 *bytes) {
    const u8 *start, *end;
    if (equal(path, "/system/cacert.pem")) {
        start = _binary_trust_roots_pem_start;
        end = _binary_trust_roots_pem_end;
    } else if (equal(path, "/system/libc.elf")) {
        start = _binary_build_libc_app_elf_start;
        end = _binary_build_libc_app_elf_end;
    } else if (equal(path, "/system/curl.elf")) {
        start = _binary_build_curl_app_elf_start;
        end = _binary_build_curl_app_elf_end;
    } else return 0;
    *bytes = (u32)(end - start);
    return start;
}

u32 system_file_execution_budget(const u8 *image) {
    return image == _binary_build_curl_app_elf_start ? TASK_TLS_EXECUTION_BUDGET : TASK_EXECUTION_BUDGET;
}
