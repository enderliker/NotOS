#include "base.h"
#include "lang.h"
#define MAX_VARS 16
#define MAX_NAME 15
#define MAX_DEPTH 24
#define INT_MAX_VALUE 2147483647
#define INT_MIN_VALUE (-2147483647 - 1)
static struct {char name[MAX_NAME+1];int value;} vars[MAX_VARS];
static unsigned count;
static const char *pos;
static const char *error;
static void fail(const char *s) {if(!error) error=s;}
static void space(void) {while(*pos==' '||*pos=='\t') pos++;}
static int letter(char c) {return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';}
static int digit(char c) {return c>='0'&&c<='9';}
static int identifier(char *dst) {
    space(); unsigned n=0;
    if(!letter(*pos)) {fail("expected identifier");return 0;}
    while(letter(*pos)||digit(*pos)) {
        if(n<MAX_NAME) dst[n++]=*pos;
        else fail("identifier too long");
        pos++;
    }
    dst[n]=0;
    return !error;
}
static int keyword(const char *s) {
    space(); const char *p=pos;
    while(*s) {if(*p++!=*s++) return 0;}
    if(letter(*p)||digit(*p)) return 0;
    pos=p;return 1;
}
static int checked(long long v) {
    if(v<INT_MIN_VALUE || v>INT_MAX_VALUE) {fail("integer overflow");return 0;}
    return (int)v;
}
static int expression(unsigned depth);
static int factor(unsigned depth) {
    space();
    if(error) return 0;
    if(depth>MAX_DEPTH) {fail("expression too deep");return 0;}
    if(*pos=='+'||*pos=='-') {
        char op=*pos++; int v=factor(depth+1);
        return op=='-' ? checked(-(long long)v) : v;
    }
    if(*pos=='(') {
        pos++;int v=expression(depth+1);space();
        if(*pos!=')') fail("expected closing parenthesis");else pos++;
        return v;
    }
    if(digit(*pos)) {
        u32 value=0;
        while(digit(*pos)) {
            unsigned d=(unsigned)(*pos++-'0');
            if(value>((u32)INT_MAX_VALUE-d)/10) fail("integer overflow");
            else value=value*10+d;
        }
        return (int)value;
    }
    if(letter(*pos)) {
        char name[MAX_NAME+1];identifier(name);
        for(unsigned i=0;i<count;i++) if(equal(name,vars[i].name)) return vars[i].value;
        fail("undefined variable");return 0;
    }
    fail("expected expression");return 0;
}
static int term(unsigned depth) {
    int v=factor(depth);
    for(;;) {
        space();char op=*pos;
        if(error || (op!='*'&&op!='/'&&op!='%')) return v;
        pos++;int rhs=factor(depth);
        if(error) return 0;
        if(op=='*') v=checked((long long)v*rhs);
        else if(!rhs) {fail("division by zero");return 0;}
        else if(v==INT_MIN_VALUE && rhs==-1) {fail("integer overflow");return 0;}
        else v=op=='/' ? v/rhs : v%rhs;
    }
}
static int expression(unsigned depth) {
    int v=term(depth);
    for(;;) {
        space();char op=*pos;
        if(error || (op!='+'&&op!='-')) return v;
        pos++;int rhs=term(depth);
        if(error) return 0;
        v=checked(op=='+' ? (long long)v+rhs : (long long)v-rhs);
    }
}
void lang_reset(void) {count=0;}
void lang_run(const char *source) {
    pos=source;error=0;space();
    if(!*pos) fail("empty program");
    while(*pos && !error) {
        int assignment=0, printing=0;
        char name[MAX_NAME+1];
        if(keyword("let")) {
            assignment=1;identifier(name);space();
            if(*pos!='=') fail("expected equals");else pos++;
        } else if(keyword("print")) printing=1;
        else {fail("expected let or print");break;}
        int value=expression(0);space();
        if(*pos && *pos!=';') fail("unexpected token");
        if(error) break;
        if(assignment) {
            unsigned i=0;
            while(i<count && !equal(vars[i].name,name)) i++;
            if(i==count) {
                if(count==MAX_VARS) {fail("variable table full");break;}
                for(unsigned j=0;(vars[i].name[j]=name[j]);j++) {}
                count++;
            }
            vars[i].value=value;
        }
        if(printing) {console_int(value);console_putc('\n');}
        if(*pos==';') pos++;
        space();
    }
    if(error) {console_puts("LANG ERROR: ");console_puts(error);console_putc('\n');}
}
