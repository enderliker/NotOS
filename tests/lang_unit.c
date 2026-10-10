#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/lang.h"
static char output[4096];
static unsigned used;
void console_putc(char c) {assert(used+1<sizeof(output));output[used++]=c;output[used]=0;}
void console_puts(const char *s) {while(*s) console_putc(*s++);}
void console_int(int v) {char b[16];snprintf(b,sizeof(b),"%d",v);console_puts(b);}
static void check(const char *source,const char *expected) {
    used=0;output[0]=0;lang_run(source);
    if(strcmp(output,expected)) {fprintf(stderr,"FAIL %s: [%s] != [%s]\n",source,output,expected);assert(0);}
}
int main(void) {
    lang_reset();
    check("print 2 + 3 * 4", "14\n");
    check("let x = (2 + 3) * 4; print x", "20\n");
    check("let x = x - 7; print x / 2; print x % 2", "6\n1\n");
    check("print -2147483647 - 1", "-2147483648\n");
    check("print 2147483647 + 1", "LANG ERROR: integer overflow\n");
    check("print 100000 * 100000", "LANG ERROR: integer overflow\n");
    check("print 2147483648", "LANG ERROR: integer overflow\n");
    check("print 1 / 0", "LANG ERROR: division by zero\n");
    check("print missing", "LANG ERROR: undefined variable\n");
    check("let x = 1 / 0", "LANG ERROR: division by zero\n");
    check("print x", "13\n");
    check("print (2 + 3", "LANG ERROR: expected closing parenthesis\n");
    check("print 2 xyz", "LANG ERROR: unexpected token\n");
    check("print ------------------------------1", "LANG ERROR: expression too deep\n");
    check("", "LANG ERROR: empty program\n");
    check("let abcdefghijklmnop = 1", "LANG ERROR: identifier too long\n");
    lang_reset();
    for(int i=0;i<16;i++) {char src[64];snprintf(src,sizeof(src),"let v%d = %d",i,i);check(src,"");}
    check("let v16 = 16", "LANG ERROR: variable table full\n");
    check("let v0 = 100; print v0", "100\n");
    puts("PASS: NotLang arithmetic, variables, boundaries and parser errors");
}
