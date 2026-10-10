# NotLang 0.2: compilador y VM aislada

`run` conserva el intérprete kernel 0.1 y sus variables entre líneas. `run-user` y `exec` usan un compilador nuevo y una VM ejecutada realmente en **ring 3**, con código/datos/pilas privados y syscalls INT 0x80. No son una llamada al intérprete kernel desde una demo user.

```text
run-user let x = 1; repeat 3 { let x = x * 2; }; print x
mkfs
write demo.nl let x = 40; print x + 2
exec demo.nl
```

El primer programa imprime 8; el archivo imprime 42 y puede ejecutarse de nuevo después de reiniciar con el mismo disco de datos. Cada ejecución tiene variables nuevas; no comparte el estado del intérprete 0.1.

Gramática: let/print, enteros/expression de 0.1 y `repeat INTEGER { statements }`. Separar statements con `;`; se permite omitirlo antes de `}` o del fin de programa. Repeat admite 1..1000 iteraciones y ocho niveles anidados. No hay funciones, strings, comparaciones, imports ni acceso directo al filesystem desde el programa.

El compilador solamente analiza y emite bytecode; no ejecuta expresiones de usuario en el kernel. Estado local, 16 variables/nombres de 15 caracteres, expresiones de profundidad 24 y hasta 192 instrucciones. La VM valida opcodes, operands, pila de 32 valores, variable inicializada y emparejamiento de loops. Operaciones con overflow/división por cero terminan con estado 65; límite de un millón de pasos, además del presupuesto IRQ del proceso. Salida normal 0. Las asignaciones y valores se calculan dentro del espacio user.

La VM freestanding se enlaza separadamente en 0x08000000, cabe en una página de código y no tiene BSS mutable. Se incrusta en el kernel como imagen de código de solo lectura; el linker comprueba tamaño y ausencia de BSS. El archivo contiene **fuente NotLang**, no ELF ni machine code; se recompila al ejecutar.

Tests: compilador nativo, límites/gramática, ejecución ring3, loops anidados, errores aritméticos, cleanup de frames/heap, archivo persistente ejecutado tras reiniciar. Probado en QEMU; no es un lenguaje general ni un sandbox auditado.
