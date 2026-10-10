/* Launch a separately linked upstream-libc application through the ELF runtime. */
#include "libc_command.h"
#include "process.h"
extern const u8 _binary_build_libc_app_elf_start[],_binary_build_libc_app_elf_end[];
void libc_command(const char *mode) {
    if (!equal(mode,"test") && !equal(mode,"exit") && !equal(mode,"noentropy") && !equal(mode,"files") && !equal(mode,"stdin")
        && !equal(mode,"disk") && !equal(mode,"hold") && !equal(mode,"fault")) {
        console_puts("LIBC ERROR: choose test/exit/noentropy/files/stdin/disk/hold/fault\n");return;
    }
    u32 bytes=(u32)(_binary_build_libc_app_elf_end-_binary_build_libc_app_elf_start);
    u32 pid=process_spawn_elf_args("libc",_binary_build_libc_app_elf_start,bytes,mode);
    if (!pid) console_puts("LIBC ERROR: invalid ELF, no slot or memory\n");
    else {console_puts("LIBC: started pid=");console_uint(pid);console_putc('\n');}
}
