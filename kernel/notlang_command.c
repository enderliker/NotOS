#include "notlang_bytecode.h"
#include "process.h"
#include "fs.h"
extern const u8 _binary_build_notlang_vm_bin_start[], _binary_build_notlang_vm_bin_end[];
void nl_command(const char *kind,const char *source) {
    static char file[FS_FILE_MAX+1];
    if (equal(kind,"exec")) {
        int bytes=fs_read_file(source,file,FS_FILE_MAX);
        if (bytes<0) {console_puts("NotLang ERROR: file unavailable\n");return;}
        file[bytes]=0;
        if (length(file)!=(unsigned)bytes) {console_puts("NotLang ERROR: NUL in source\n");return;}
        source=file;
    }
    struct nl_program program;
    const char *error=nl_compile(source,&program);
    if (error) {console_puts("NotLang COMPILE ERROR: ");console_puts(error);console_putc('\n');return;}
    u32 pid=process_spawn_image("NotLang",_binary_build_notlang_vm_bin_start,
        (u32)(_binary_build_notlang_vm_bin_end-_binary_build_notlang_vm_bin_start),
        (const u8 *)&program,sizeof(program));
    if (!pid) {console_puts("NotLang ERROR: no slot or memory\n");return;}
    console_puts("NotLang: started ring3 pid=");console_uint(pid);console_putc('\n');
}
