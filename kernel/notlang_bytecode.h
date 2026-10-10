#ifndef NOTOS_NOTLANG_BYTECODE_H
#define NOTOS_NOTLANG_BYTECODE_H
#include "base.h"
#define NL_MAGIC 0x324c544eu
#define NL_INSTRUCTIONS 192u
#define NL_VARIABLES 16u
#define NL_STACK 32u
#define NL_LOOPS 8u
enum nl_opcode { NL_PUSH, NL_LOAD, NL_STORE, NL_ADD, NL_SUB, NL_MUL,
    NL_DIV, NL_MOD, NL_NEG, NL_PRINT, NL_REPEAT, NL_END, NL_HALT };
struct nl_instruction { u32 opcode; int operand; };
struct nl_program { u32 magic, count; struct nl_instruction code[NL_INSTRUCTIONS]; };
/* Compile without executing program code. Error text is static; no heap needed. */
const char *nl_compile(const char *source, struct nl_program *program);
void nl_command(const char *kind, const char *source);
#endif
