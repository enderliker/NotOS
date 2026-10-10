/* Kernel has copied PT_LOAD, zeroed BSS and constructed argc/argv/envp. */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
typedef void (*initializer)(void);
extern initializer __init_array_start[],__init_array_end[],__fini_array_start[],__fini_array_end[];
extern char **environ;
extern int main(int argc,char **argv);
static void flush_console(void) {fflush(stdout);fflush(stderr);}
static void run_destructors(void) {
    initializer *end=__fini_array_end;
    while (end>__fini_array_start) (*--end)();
}
void notos_start(int argc,char **argv) __attribute__((noreturn));
void notos_start(int argc,char **argv) {
    environ=argv+argc+1;
    if (atexit(flush_console) || atexit(run_destructors)) _exit(127);
    for (initializer *p=__init_array_start;p<__init_array_end;p++) (*p)();
    exit(main(argc,argv));
}
