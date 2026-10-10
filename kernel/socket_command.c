#include "socket_command.h"
#include "process.h"
extern const u8 _binary_build_socket_app_elf_start[],_binary_build_socket_app_elf_end[];
void socket_command(const char *argument) {
    if (length(argument)>=64) {console_puts("SOCKET ERROR: argument too long\n");return;}
    u32 bytes=(u32)(_binary_build_socket_app_elf_end-_binary_build_socket_app_elf_start);
    u32 pid=process_spawn_elf_args("socket",_binary_build_socket_app_elf_start,bytes,argument);
    if (!pid) console_puts("SOCKET ERROR: invalid ELF, no slot or memory\n");
    else {console_puts("SOCKET: started pid=");console_uint(pid);console_putc('\n');}
}
