#include "base.h"
#include "lang.h"
#include "net.h"
#include "clock.h"
#include "interrupt.h"
#include "pic.h"
#include "keyboard.h"
#include "physical.h"
#include "paging.h"
#include "heap.h"
#include "arch.h"
#include "user.h"
#include "multitask.h"
#include "fs.h"
#include "process.h"
static char line[160];
static unsigned used;
static int dropped;
void console_putc(char c) {
    while(!(inb(0x3fd)&0x20)) {}
    outb(0x3f8,(u8)c);
    gui_putc(c);
}
void console_puts(const char *s) { while(*s) console_putc(*s++); }
void console_uint(u32 value) {
    char b[10];unsigned n=0;
    do {b[n++]=(char)('0'+value%10);value/=10;} while(value);
    while(n) console_putc(b[--n]);
}
void console_int(int value) {
    u32 magnitude=(u32)value;
    if(value<0) {console_putc('-');magnitude=0-magnitude;}
    console_uint(magnitude);
}
static void clock_command(const char *kind,const char *argument) {
    unsigned duration=0;
    if(!*argument) {console_puts("ERROR: duration must be 0..5000 ms\n");return;}
    while(*argument) {
        if(*argument<'0'||*argument>'9'||duration>5000) {console_puts("ERROR: duration must be 0..5000 ms\n");return;}
        duration=duration*10+(unsigned)(*argument++-'0');
    }
    if(duration>5000) {console_puts("ERROR: duration must be 0..5000 ms\n");return;}
    unsigned before=clock_ms(),ticks=clock_ticks();
    if(equal(kind,"sleep")) {
        if(!clock_sleep(duration)) {console_puts("ERROR: sleep requires enabled interrupts\n");return;}
    } else while((unsigned)(clock_ms()-before)<duration) __asm__ volatile("pause");
    unsigned after=clock_ms(),after_ticks=clock_ticks();
    console_puts("CLOCK: ");console_puts(kind);console_puts(" elapsed=");console_uint(after-before);
    console_puts(" ms ticks=");console_uint(after_ticks-ticks);console_putc('\n');
}
static void command(const char *s) {
    if(equal(s,"help")) console_puts("help  about  clear  echo TEXT\nrun PROGRAM  lang-reset\nnet  dns HOST  http HOST[:PORT]\nint3  irqs  irq-test  uptime\nsleep MS  busy MS\nmem  frame-test  vm  vm-test\nheap  heap-test  alloc BYTES  free SLOT\nring3-test  user-run SCENARIO\nmultitask-test  multitask-run SCENARIO\nmkfs  ls  cat NAME  write NAME TEXT  rm NAME\nspawn worker/spin/fault  ps  kill PID\nfault divide/opcode/gp/null/readonly/guard/unmapped/heap-end (HALTS)\n");
    else if(equal(s,"about")) console_puts("NotOS: own bootloader and kernel.\n32-bit x86; no Linux runtime.\n");
    else if(equal(s,"clear")) gui_clear();
    else if(starts(s,"echo ")) {console_puts(s+5);console_putc('\n');}
    else if(starts(s,"run ")) lang_run(s+4);
    else if(equal(s,"lang-reset")) {lang_reset();console_puts("LANG: variables cleared\n");}
    else if(equal(s,"net")) net_status();
    else if(starts(s,"dns ")) net_command("dns",s+4);
    else if(starts(s,"http ")) net_command("http",s+5);
    else if(equal(s,"int3")) {
        unsigned before=interrupts_breakpoints();
        if(interrupt_selftest() && interrupts_breakpoints()==before+1)
            console_puts("IDT: breakpoint OK; registers and flags preserved\n");
        else console_puts("ERROR: interrupt selftest failed\n");
    }
    else if(equal(s,"fault divide")) fault_divide();
    else if(equal(s,"fault opcode")) fault_opcode();
    else if(equal(s,"fault gp")) fault_gp();
    else if(equal(s,"uptime")) {
        unsigned ms=clock_ms(),ticks=clock_ticks();
        console_puts("UPTIME: ms=");console_uint(ms);console_puts(" ticks=");console_uint(ticks);
        console_puts(" IF=");console_int(irq_enabled());console_putc('\n');
    }
    else if(equal(s,"irqs")) {
        console_puts("IRQS: timer=");console_uint(interrupts_irq_count(0));
        console_puts(" keyboard=");console_uint(interrupts_irq_count(1));
        console_puts(" stray=");console_uint(interrupts_stray_count());
        console_puts(" dropped=");console_uint(keyboard_dropped());
        console_puts("\nSPURIOUS: irq7=");console_uint(interrupts_spurious_count(7));
        console_puts(" irq15=");console_uint(interrupts_spurious_count(15));console_putc('\n');
    }
    else if(equal(s,"irq-test")) {
        unsigned s7=interrupts_spurious_count(7),s15=interrupts_spurious_count(15);
        __asm__ volatile("int $0x27; int $0x2f" ::: "memory");
        if(interrupts_spurious_count(7)==s7+1 && interrupts_spurious_count(15)==s15+1)
            console_puts("IRQ: spurious 7/15 handled\n");
        else console_puts("ERROR: spurious IRQ selftest failed\n");
    }
    else if(starts(s,"sleep ")) clock_command("sleep",s+6);
    else if(starts(s,"busy ")) clock_command("busy",s+5);
    else if(equal(s,"mem")) physical_status();
    else if(equal(s,"frame-test")) console_puts(physical_selftest()?"MEM: frame selftest PASS\n":"ERROR: frame selftest failed\n");
    else if(equal(s,"vm")) paging_status();
    else if(equal(s,"vm-test")) console_puts(paging_selftest()?"VM: map/alias/remap/unmap selftest PASS\n":"ERROR: VM selftest failed\n");
    else if(equal(s,"fault null")) paging_fault_null();
    else if(equal(s,"fault readonly")) paging_fault_readonly();
    else if(equal(s,"fault guard")) paging_fault_guard();
    else if(equal(s,"fault unmapped")) paging_fault_unmapped();
    else if(equal(s,"heap")) heap_status();
    else if(equal(s,"heap-test")) console_puts(heap_selftest()?"HEAP: split/coalesce/calloc/OOM selftest PASS\n":"HEAP ERROR: selftest failed or heap not empty\n");
    else if(starts(s,"alloc ")) heap_command("alloc",s+6);
    else if(starts(s,"free ")) heap_command("free",s+5);
    else if(equal(s,"fault heap-end")) heap_fault_guard_end();
    else if(equal(s,"ring3-test")) user_command("demo");
    else if(starts(s,"user-run ")) user_command(s+9);
    else if(equal(s,"multitask-test")) multitask_command("workers");
    else if(starts(s,"multitask-run ")) multitask_command(s+14);
    else if(equal(s,"ps")) process_command("ps", "");
    else if(starts(s,"spawn ")) process_command("spawn",s+6);
    else if(starts(s,"kill ")) process_command("kill",s+5);
    else if(equal(s,"mkfs") || equal(s,"ls")) fs_command(s, "");
    else if(starts(s,"write ")) fs_command("write",s+6);
    else if(starts(s,"cat ")) fs_command("cat",s+4);
    else if(starts(s,"rm ")) fs_command("rm",s+3);
    else if(*s) console_puts("ERROR: unknown command\n");
}
static void input(char c) {
    static int cr;
    if(c=='\n' && cr) {cr=0;return;}
    cr=(c=='\r');
    if(c=='\n'||c=='\r') {
        console_putc('\n');
        if(dropped) { console_puts("ERROR: input too long; discarded\n"); dropped=0; }
        else { line[used]=0;command(line); }
        used=0;console_puts("notos> ");
    } else if(dropped) {
        return;
    } else if(c=='\b'||c==127) {
        if(used) {used--;console_putc('\b');console_putc(' ');console_putc('\b');}
    } else if(c>=32 && c<=126) {
        if(used<sizeof(line)-1) {line[used++]=c;console_putc(c);}
        else dropped=1;
    }
}
static void keyboard(void) {
    static int shift,caps,extended;
    static const char normal[128] = {
        [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
        [12]='-',[13]='=',[14]='\b',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',
        [21]='y',[22]='u',[23]='i',[24]='o',[25]='p',[26]='[',[27]=']',[28]='\n',
        [30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',
        [39]=';',[40]='\'', [41]='`',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',
        [48]='b',[49]='n',[50]='m',[51]=',',[52]='.',[53]='/',[55]='*',[57]=' '
    };
    u8 code;
    if(!keyboard_next(&code)) return;
    if(code==0xe0) {extended=1;return;}
    if(extended) {extended=0;return;}
    if(code==42||code==54) {shift|=code==42?1:2;return;}
    if(code==170||code==182) {shift&=code==170?~1:~2;return;}
    if(code&128) return;
    if(code==58) {caps=!caps;return;}
    if(code==59) {console_puts("\nF1: help/about/clear/echo/run\n");return;}
    if(code==60) {gui_clear();used=0;dropped=0;console_puts("notos> ");return;}
    char c=normal[code];
    if(c>='a'&&c<='z') {if((shift!=0)^caps) c-=32;}
    else if(shift) {
        const char *a="1234567890-=[];',./`\\";
        const char *b="!@#$%^&*()_+{}:\"<>?~|";
        for(unsigned i=0;a[i];i++) if(c==a[i]) {c=b[i];break;}
    }
    if(c) input(c);
}
void kernel_main(void) {
    outb(0x3f9,0);outb(0x3fb,0x80);outb(0x3f8,1);outb(0x3f9,0);
    outb(0x3fb,3);outb(0x3fa,0xc7);outb(0x3fc,0x0b);
    gui_init();
    interrupts_init();
    arch_init();
    if(*(volatile u32 *)0x1fffc!=0x4e4f544f) {
        console_puts("BOOT ERROR: second kernel chunk missing\n");
        for(;;) __asm__ volatile("cli; hlt");
    }
    if(!physical_init()) {
        console_puts("MEM ERROR: invalid E820 or no usable RAM\n");
        for(;;) __asm__ volatile("cli; hlt");
    }
    if(!paging_init()) {
        console_puts("VM ERROR: failed to enable paging\n");
        for(;;) __asm__ volatile("cli; hlt");
    }
    if(!heap_init()) {
        console_puts("HEAP ERROR: initialization failed\n");
        for(;;) __asm__ volatile("cli; hlt");
    }
    pic_init();
    clock_init();
    int keyboard_ready=keyboard_init();
    pic_unmask(0);pic_unmask(2);
    if(keyboard_ready) pic_unmask(1);
    __asm__ volatile("sti" ::: "memory");
    console_puts("NotOS 0.8\nBOOT: protected mode OK\nBOOT: kernel C OK\nGUI: 320x200 OK\nType help.\n");
    console_puts("IDT: 256 gates installed\n");
    console_puts("IRQ: PIC remapped; PIT ~1000 Hz\n");
    console_puts(keyboard_ready?"KEYBOARD: IRQ1 ready\n":"KEYBOARD: unavailable; use serial\n");
    console_puts("BOOT: 64 KiB loader verified\n");
    physical_status();
    paging_status();
    heap_status();
    console_puts("ARCH: TSS selector=0x28; user gate ready\n");
    fs_init();
    net_init();
    console_puts("notos> ");
    for(;;) {
        net_poll(clock_ms());
        for (unsigned budget=0;budget<160 && (inb(0x3fd)&1);++budget)
            input((char)inb(0x3f8));
        keyboard();
        process_poll();
        __asm__ volatile("pause");
    }
}
