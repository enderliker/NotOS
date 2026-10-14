# NotLang 0.2: Ring 3 Bytecode Compiler and Isolated VM

While `run` invokes the stateful NotLang 0.1 kernel interpreter, `run-user` and
`exec` compile NotLang 0.2 source into bounded bytecode (`kernel/notlang_compile.c`)
and execute it inside an isolated **ring 3** virtual machine (`user/notlang_vm.c`)
with private code, data, and stack pages and `INT 0x80` syscalls.

## Usage

```text
run-user let x = 1; repeat 3 { let x = x * 2; }; print x
mkfs
write demo.nl let x = 40; print x + 2
exec demo.nl
```

The first command prints `8`. The second sequence writes a NotLang source file
to persistent storage (`NotFS v2` or `tmpfs`) and runs it in ring 3, printing
`42`. Each `run-user` or `exec` invocation starts with a clean variable state
and runs as a managed background process.

## Grammar & Bytecode VM Limits

- Supports `let`, `print`, 32-bit signed integer expressions (`+`, `-`, `*`,
  `/`, `%`, unary `+`/`-`, parentheses), and bounded loops:
  `repeat INTEGER { statements }`.
- Statements are separated by `;` (optional before `}` or end of input).
- `repeat` accepts `1..1000` iterations and up to **8 nested loop levels**.
- The compiler (`notlang_compile.c`) only parses and emits bytecode (`notlang_bytecode.h`);
  it never evaluates user expressions in ring 0.
- Limits: up to **16 variables** (15-character names), expression depth **24**,
  and at most **192 bytecode instructions**.
- The ring 3 VM validates all opcodes, operand indices, variable initialization,
  loop pairing, and a 32-entry evaluation stack. Arithmetic overflow or division
  by zero exits cleanly with status `65`; normal completion exits with `0`.
- Execution is bounded by a **1,000,000-step VM limit** in addition to the
  kernel's 250-tick CPL3 process budget.

## Build & Memory Isolation

The standalone VM (`user/notlang_vm.c`) is linked at `0x08000000`, fits within a
single 4 KiB code page with zero mutable BSS, and is embedded as a read-only
image in the kernel. Source files executed by `exec FILE` contain plain NotLang
source text (up to 60 KiB) and are compiled fresh on each run.
