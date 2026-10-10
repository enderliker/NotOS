#include <assert.h>
#include <stdio.h>
#include "../kernel/notlang_bytecode.h"
int main(void) {
    struct nl_program p;
    assert(!nl_compile("let x = 1; repeat 3 { let x = x * 2; }; print x",&p));
    assert(p.magic==NL_MAGIC && p.code[p.count-1].opcode==NL_HALT);
    assert(nl_compile("print missing",&p) && !p.magic);
    assert(nl_compile("repeat 0 { print 1 }",&p));
    assert(nl_compile("repeat 1001 { print 1 }",&p));
    assert(nl_compile("repeat 2 { print 1",&p));
    assert(nl_compile("print 2147483648",&p));
    assert(nl_compile("print ((((((((((((((((((((((((((1",&p));
    char large[4096];unsigned n=0;
    for(unsigned i=0;i<300;i++) {const char *s="print 1;";while(*s) large[n++]=*s++;}
    large[n]=0;assert(nl_compile(large,&p) && !p.magic);
    assert(!nl_compile("repeat 2 { repeat 2 { print -3; }; }; print (3+2)*4",&p));
    puts("PASS: bounded compiler, loop grammar, names, overflow and instruction capacity");
}
