# NotLang 0.1

Intérprete propio que se ejecuta dentro del kernel de NotOS. No usa Python, JavaScript, una VM externa ni libc.

## Uso

Escribe en la terminal:

```text
run let x = (2 + 3) * 4; print x
run print x - 7
lang-reset
```

Los dos primeros comandos imprimen `20` y `13`. Las variables sobreviven entre comandos hasta `lang-reset` o un reinicio; no se guardan en disco.

## Gramática

```text
program   := statement (';' statement)* ';'?
statement := 'let' identifier '=' expression | 'print' expression
expression := term (('+' | '-') term)*
term      := factor (('*' | '/' | '%') factor)*
factor    := integer | identifier | '(' expression ')' | ('+' | '-') factor
```

- Enteros con signo de 32 bits; detección de desbordamiento y división por cero.
- Literales decimales no negativos hasta 2147483647; el mínimo entero se expresa como `-2147483647 - 1`.
- División entera truncada hacia cero. `%` devuelve el resto.
- Máximo de 16 variables, nombres de hasta 15 caracteres y profundidad de expresión de 24.
- Cada línea de shell admite hasta 159 caracteres. Los comandos demasiado largos se descartan al pulsar Enter.
- Palabras clave y variables distinguen mayúsculas/minúsculas. La fuente gráfica inicial dibuja las minúsculas como mayúsculas; la entrada y el intérprete conservan el carácter original.
- Las sentencias ya completadas conservan sus efectos si una posterior falla. Una asignación fallida no modifica su variable.

No hay todavía funciones, bucles, cadenas, archivos, compilador ni programas aislados del kernel.
