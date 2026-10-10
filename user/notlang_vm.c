/* Compiled separately at 0x08000000. No kernel functions or host runtime. */
#include "../kernel/notlang_bytecode.h"
#include "../kernel/syscall_numbers.h"
static u32 syscall(u32 number,u32 first,u32 second) {
    __asm__ volatile("int $0x80" : "+a"(number) : "b"(first),"c"(second) : "memory","cc");
    return number;
}
static void write(const char *text) {syscall(SYS_WRITE,(u32)text,length(text));}
static void exit_program(u32 status) __attribute__((noreturn));
static void exit_program(u32 status) {syscall(SYS_EXIT,status,0);for (;;) __asm__ volatile("ud2");}
static void error(const char *message) __attribute__((noreturn));
static void error(const char *message) {write(message);exit_program(65);}
static void print_value(int value) {
    char buffer[13]; unsigned n=0;u32 magnitude=(u32)value;
    if (value<0) magnitude=0-magnitude;
    do {buffer[n++]=(char)('0'+magnitude%10);magnitude/=10;} while (magnitude);
    if (value<0) buffer[n++]='-';
    for (unsigned i=0;i<n/2;i++) {char temp=buffer[i];buffer[i]=buffer[n-i-1];buffer[n-i-1]=temp;}
    buffer[n++]='\n'; buffer[n]=0;write(buffer);
}
void _start(void) {
    const volatile struct nl_program *program=(const volatile struct nl_program *)0x08001000;
    if (program->magic!=NL_MAGIC || !program->count || program->count>NL_INSTRUCTIONS)
        error("NotLang: invalid bytecode\n");
    int variables[NL_VARIABLES]={0}, stack[NL_STACK];
    u32 initialized=0, sp=0, pc=0, loops=0;
    struct {u32 remaining, begin;} repeat[NL_LOOPS];
    unsigned steps=0;
    while (pc<program->count) {
        if (++steps>1000000u) error("NotLang: step limit\n");
        struct nl_instruction instruction=program->code[pc++];
        u32 op=instruction.opcode;int operand=instruction.operand;
        if (op==NL_PUSH || op==NL_LOAD) {
            if (sp==NL_STACK) error("NotLang: stack overflow\n");
            if (op==NL_LOAD) {
                if ((u32)operand>=NL_VARIABLES || !(initialized&(1u<<operand)))
                    error("NotLang: undefined variable\n");
                operand=variables[operand];
            }
            stack[sp++]=operand;
        } else if (op==NL_STORE) {
            if (!sp || (u32)operand>=NL_VARIABLES) error("NotLang: invalid store\n");
            variables[operand]=stack[--sp]; initialized|=1u<<operand;
        } else if (op==NL_PRINT || op==NL_NEG) {
            if (!sp) error("NotLang: stack underflow\n");
            int value=stack[--sp];
            if (op==NL_PRINT) print_value(value);
            else {
                if (value==(-2147483647-1)) error("NotLang: integer overflow\n");
                stack[sp++]=-value;
            }
        } else if (op>=NL_ADD && op<=NL_MOD) {
            if (sp<2) error("NotLang: stack underflow\n");
            int right=stack[--sp],left=stack[--sp],result=0,overflow=0;
            if (op==NL_ADD) overflow=__builtin_add_overflow(left,right,&result);
            else if (op==NL_SUB) overflow=__builtin_sub_overflow(left,right,&result);
            else if (op==NL_MUL) overflow=__builtin_mul_overflow(left,right,&result);
            else {
                if (!right) error("NotLang: division by zero\n");
                if (left==(-2147483647-1) && right==-1) error("NotLang: integer overflow\n");
                result=op==NL_DIV?left/right:left%right;
            }
            if (overflow) error("NotLang: integer overflow\n");
            stack[sp++]=result;
        } else if (op==NL_REPEAT) {
            if (loops==NL_LOOPS || operand<1 || operand>1000) error("NotLang: invalid loop\n");
            repeat[loops].remaining=(u32)operand;repeat[loops++].begin=pc;
        } else if (op==NL_END) {
            if (!loops || repeat[loops-1].begin!=(u32)operand) error("NotLang: invalid loop end\n");
            if (--repeat[loops-1].remaining) pc=(u32)operand;else loops--;
        } else if (op==NL_HALT) {
            if (sp || loops) error("NotLang: unbalanced bytecode\n");
            exit_program(0);
        } else error("NotLang: invalid opcode\n");
    }
    error("NotLang: missing halt\n");
}
