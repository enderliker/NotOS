/* Bounded compiler: all parser state is local and output is validated bytecode. */
#include "notlang_bytecode.h"
#define NAME_BYTES 16u
#define EXPR_DEPTH 24u
struct compiler {
    const char *cursor;
    const char *error;
    struct nl_program *program;
    char names[NL_VARIABLES][NAME_BYTES];
    unsigned variables;
};
static void fail(struct compiler *c, const char *message) {if (!c->error) c->error=message;}
static void space(struct compiler *c) {
    while (*c->cursor==' ' || *c->cursor=='\t' || *c->cursor=='\n' || *c->cursor=='\r') c->cursor++;
}
static int letter(char ch) {return (ch>='a'&&ch<='z') || (ch>='A'&&ch<='Z') || ch=='_';}
static int digit(char ch) {return ch>='0'&&ch<='9';}
static int keyword(struct compiler *c,const char *word) {
    space(c); const char *p=c->cursor;
    while (*word) if (*p++ != *word++) return 0;
    if (letter(*p)||digit(*p)) return 0;
    c->cursor=p; return 1;
}
static unsigned emit(struct compiler *c,unsigned opcode,int operand) {
    unsigned at=c->program->count;
    if (at>=NL_INSTRUCTIONS) {fail(c,"program too large");return 0;}
    c->program->code[at]=(struct nl_instruction){opcode,operand}; c->program->count++;
    return at;
}
static int name(struct compiler *c) {
    space(c); char buffer[NAME_BYTES]; unsigned n=0;
    if (!letter(*c->cursor)) {fail(c,"expected identifier");return -1;}
    while (letter(*c->cursor)||digit(*c->cursor)) {
        if (n>=NAME_BYTES-1) {fail(c,"identifier too long");return -1;}
        buffer[n++]=*c->cursor++;
    }
    buffer[n]=0;
    for (unsigned i=0;i<c->variables;i++) if (equal(c->names[i],buffer)) return (int)i;
    if (c->variables==NL_VARIABLES) {fail(c,"variable table full");return -1;}
    unsigned index=c->variables++;
    for (unsigned i=0;(c->names[index][i]=buffer[i]);i++) {}
    return (int)index;
}
static int number(struct compiler *c) {
    space(c); u32 value=0;
    if (!digit(*c->cursor)) {fail(c,"expected integer");return 0;}
    while (digit(*c->cursor)) {
        unsigned d=(unsigned)(*c->cursor++-'0');
        if (value>(2147483647u-d)/10) {fail(c,"integer overflow");return 0;}
        value=value*10+d;
    }
    return (int)value;
}
static void expression(struct compiler *c,unsigned depth);
static void factor(struct compiler *c,unsigned depth) {
    space(c); if (c->error) return;
    if (depth>EXPR_DEPTH) {fail(c,"expression too deep");return;}
    char ch=*c->cursor;
    if (ch=='+'||ch=='-') {
        c->cursor++;factor(c,depth+1); if (ch=='-') emit(c,NL_NEG,0);
    } else if (ch=='(') {
        c->cursor++;expression(c,depth+1);space(c);
        if (*c->cursor!=')') fail(c,"expected closing parenthesis");else c->cursor++;
    } else if (digit(ch)) emit(c,NL_PUSH,number(c));
    else if (letter(ch)) {
        unsigned before=c->variables;int index=name(c);
        if (c->variables!=before) fail(c,"undefined variable");
        emit(c,NL_LOAD,index);
    } else fail(c,"expected expression");
}
static void term(struct compiler *c,unsigned depth) {
    factor(c,depth);
    while (!c->error) {
        space(c);char ch=*c->cursor;
        if (ch!='*' && ch!='/' && ch!='%') return;
        c->cursor++;factor(c,depth);
        emit(c,ch=='*'?NL_MUL:ch=='/'?NL_DIV:NL_MOD,0);
    }
}
static void expression(struct compiler *c,unsigned depth) {
    term(c,depth);
    while (!c->error) {
        space(c);char ch=*c->cursor;
        if (ch!='+' && ch!='-') return;
        c->cursor++;term(c,depth);emit(c,ch=='+'?NL_ADD:NL_SUB,0);
    }
}
static void statements(struct compiler *c,unsigned nesting) {
    space(c);
    while (*c->cursor && *c->cursor!='}' && !c->error) {
        if (keyword(c,"repeat")) {
            if (nesting>=NL_LOOPS) {fail(c,"loop nesting too deep");return;}
            int repetitions=number(c);space(c);
            if (repetitions<1 || repetitions>1000) {fail(c,"repeat count must be 1..1000");return;}
            if (*c->cursor!='{') {fail(c,"expected opening brace");return;}
            c->cursor++;
            unsigned begin=emit(c,NL_REPEAT,repetitions);
            statements(c,nesting+1);space(c);
            if (*c->cursor!='}') {fail(c,"expected closing brace");return;}
            c->cursor++;emit(c,NL_END,(int)begin+1);
        } else {
            if (keyword(c,"let")) {
                int index=name(c);space(c);
                if (*c->cursor!='=') {fail(c,"expected equals");return;}
                c->cursor++;expression(c,0);emit(c,NL_STORE,index);
            } else if (keyword(c,"print")) {expression(c,0);emit(c,NL_PRINT,0);}
            else {fail(c,"expected let, print or repeat");return;}
        }
        space(c);
        if (*c->cursor==';') c->cursor++;
        else if (*c->cursor && *c->cursor!='}') {fail(c,"expected semicolon");return;}
        space(c);
    }
}
const char *nl_compile(const char *source,struct nl_program *program) {
    if (!source || !program) return "invalid compiler arguments";
    *program=(struct nl_program){0};
    struct compiler c={.cursor=source,.program=program};
    space(&c);
    if (!*c.cursor) return "empty program";
    statements(&c,0);space(&c);
    if (*c.cursor && !c.error) fail(&c,"unexpected token");
    if (!c.error) emit(&c,NL_HALT,0);
    if (!c.error) program->magic=NL_MAGIC;
    return c.error;
}
