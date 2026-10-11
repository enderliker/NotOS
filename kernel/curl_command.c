#include "curl_command.h"
#include "process.h"

extern const u8 _binary_build_curl_app_elf_start[], _binary_build_curl_app_elf_end[];

void curl_command(const char *arguments) {
    u32 bytes = (u32)(_binary_build_curl_app_elf_end - _binary_build_curl_app_elf_start);
    u32 pid = process_spawn_elf_split("curl", _binary_build_curl_app_elf_start, bytes, arguments);
    if (!pid) {
        console_puts("CURL ERROR: invalid arguments or ELF, no slot or memory\n");
        return;
    }
    process_quiet(pid);
    if (!process_trace_enabled()) return;
    console_puts("CURL: started pid=");
    console_uint(pid);
    console_putc('\n');
}
