# NotLang 0.1 (Kernel Interpreter)

NotLang 0.1 is a custom arithmetic scripting interpreter that runs inside the
NotOS kernel for quick interactive expressions (`run`). It uses no external VM
or host library.

For the isolated **ring 3** bytecode compiler, virtual machine, loops
(`repeat`), and persistent script execution (`run-user` and `exec`), see
[NOTLANG_USER.md](NOTLANG_USER.md).

## Usage

```text
run let x = (2 + 3) * 4; print x
run print x - 7
lang-reset
```

The first two commands print `20` and `13`. Variables in the 0.1 interpreter
persist in RAM across `run` commands until `lang-reset` or a system reboot.

## Grammar

```text
program    := statement (';' statement)* ';'?
statement  := 'let' identifier '=' expression | 'print' expression
expression := term (('+' | '-') term)*
term       := factor (('*' | '/' | '%') factor)*
factor     := integer | identifier | '(' expression ')' | ('+' | '-') factor
```

## Semantics & Limits

- **32-bit signed integers (`i32`)** with checked overflow detection on `+`,
  `-`, `*`, `/`, `%`, and unary negation, plus division-by-zero traps.
- Non-negative decimal literals up to `2147483647` (`INT32_MAX`); `INT32_MIN` is
  written as `-2147483647 - 1`.
- Integer division truncates toward zero; `%` computes the remainder.
- Up to **16 variables**, identifier names up to **15 characters**, and a
  maximum expression nesting depth of **24**.
- Keywords (`let`, `print`) and identifiers are case-sensitive.
- Earlier statements separated by `;` retain their side effects if a later
  statement in the same line fails; a failed `let` expression never mutates its
  target variable.
