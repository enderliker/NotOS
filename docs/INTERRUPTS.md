# IDT y excepciones — fase 2

## Avance implementado

Una IDT propia de 256 entradas, puertas de interrupción de 32 bits DPL 0 y selector 0x08. Cada vector tiene un stub en ensamblador que normaliza el código de error, conserva registros generales y segmentos, limpia DF para entrar a C y alinea la pila del handler. Al retornar, IRET restaura EIP, CS y EFLAGS.

La estructura C del frame se verifica con assertions de compilación sobre tamaños y offsets. Solo se trabaja en ring 0; todavía no se reciben frames con cambio de privilegio.

INT3 es una excepción recuperable para autopruebas. El resto entra en panic: vector, nombre, código de error, EIP, CS, EFLAGS y registros. Page fault imprime CR2 cuando corresponde. La CPU se detiene con CLI/HLT, sin intentar reiniciar ni continuar con estado incierto.

## Comandos de diagnóstico

```text
int3
fault divide
fault opcode
fault gp
```

**Los comandos `fault` detienen deliberadamente el sistema.** Es necesario cerrar/reiniciar QEMU. No son errores del intérprete NotLang: provocan instrucciones reales de la CPU para probar la IDT.

`int3` verifica registros y las flags CF/DF; la shell continúa funcionando después de retornar. `fault gp` intenta cargar DS con el selector 0x20, fuera de nuestra GDT, y debe producir #GP con código de error 0x20.

## Límites de este avance

Las interrupciones de hardware todavía permanecen desactivadas. PIC/IRQ/PIT son el siguiente avance de esta misma fase. No hay recuperación de procesos, pila alternativa, TSS/task gate para double fault ni protección contra una pila ya corrompida. Los tres casos fatales comprobados son #DE, #UD y #GP; la existencia de un handler no equivale a comprobar cada excepción posible.
