#include "base.h"
#include "lang.h"
#include "net.h"
#include "clock.h"
#include "interrupt.h"
static char line[160];
static unsigned used;
static int dropped;
void console_putc(char c) {
    while(!(inb(0x3fd)&0x20)) {}
    outb(0x3f8,(u8)c);
    gui_putc(c);
}
void console_puts(const char *s) { while(*s) console_putc(*s++); }
void console_int(int value) {
    char b[12]; unsigned n=0;
    u32 v=(u32)value;
    if(value<0) {console_putc('-');v=0-v;}
    do {b[n++]=(char)('0'+v%10);v/=10;} while(v);
    while(n) console_putc(b[--n]);
}
static void command(const char *s) {
    if(equal(s,"help")) console_puts("help  about  clear  echo TEXT\nrun PROGRAM  lang-reset\nnet  dns HOST  http HOST[:PORT]\nint3  fault divide/opcode/gp (HALTS)\n");
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
    u8 status=inb(0x64);
    if(!(status&1)) return;
    u8 code=inb(0x60);
    if(status&0x20) return;
    if(code==0xe0) {extended=1;return;}
    if(extended) {extended=0;return;}
    if(code==42||code==54) {shift=1;return;}
    if(code==170||code==182) {shift=0;return;}
    if(code&128) return;
    if(code==58) {caps=!caps;return;}
    if(code==59) {console_puts("\nF1: help/about/clear/echo/run\n");return;}
    if(code==60) {gui_clear();used=0;console_puts("notos> ");return;}
    char c=normal[code];
    if(c>='a'&&c<='z') {if(shift^caps) c-=32;}
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
    clock_init();
    gui_init();
    interrupts_init();
    console_puts("NotOS 0.5-idt\nBOOT: protected mode OK\nBOOT: kernel C OK\nGUI: 320x200 OK\nType help.\n");
    console_puts("IDT: 256 gates installed\n");
    net_init();
    console_puts("notos> ");
    for(;;) {
        net_poll(clock_ms());
        if(inb(0x3fd)&1) input((char)inb(0x3f8));
        keyboard();
        __asm__ volatile("pause");
    }
}
