# Ejecución de usuario — NotOS 0.8

## Arquitectura

GDT propia: código/datos supervisor (0x08/0x10), código/datos usuario (0x1b/0x23) y TSS de 32 bits (0x28). LTR carga la TSS; ESP0 selecciona la pila supervisor de 16 KiB de la tarea activa para cambios de privilegio. El mapa I/O queda fuera del límite de la TSS: ring 3 no puede acceder a puertos. La prueba `fault gp` ahora usa el selector inválido 0x30, ya que 0x20 pertenece a la nueva GDT.

Solo la puerta INT 0x80 tiene DPL 3. La entrada user usa IRET con IF=1 e IOPL=0 y limpia los registros generales. Los stubs conservan segmentos/registros y limpian DF antes de C. Un cambio de privilegio agrega ESP/SS user al frame; estos campos solo se leen si CS indica CPL 3.

## Espacios privados

Cada espacio tiene cinco frames propios: directorio de páginas, tabla user, código, datos y pila. Las páginas se limpian antes de reutilizarlas. El directorio comparte las tablas del kernel únicamente como supervisor; el directorio kernel original no tiene mappings user. La zona user es:

| Dirección | Permiso |
| --- | --- |
| 0x08000000 | código user de solo lectura |
| 0x08001000 | datos user de lectura/escritura |
| 0x08002000 | guard no presente |
| 0x08003000 | pila user de lectura/escritura |
| 0x08004000 | no presente |

Se carga CR3 privado al entrar y se restaura CR3 del kernel al salir. El handler de probe lee el CR3 real. La demo crea dos espacios simultáneamente, los ejecuta secuencialmente con distintos datos en la misma dirección virtual y comprueba que sus escrituras no modifican los datos del otro. Una lectura del alias físico supervisor del espacio ajeno se rechaza por #PF.

Al terminar se restauran pila/registros/flags/CR3 del llamador y se liberan los cinco frames. Nunca se destruye el directorio activo. Los lotes multitarea se describen en [TASKS.md](TASKS.md), y el gestor background en [PROCESSES.md](PROCESSES.md). Cada tarea agrega su pila kernel del heap; se libera solo después de retornar al contexto del llamador.

## Syscalls INT 0x80

Número en EAX; resultado en EAX:

| Número | Operación | Argumentos |
| --- | --- | --- |
| 0 | exit | EBX = estado |
| 1 | ticks | retorna reloj en ms |
| 2 | probe | verifica CPL/SS, pila TSS y CR3 |
| 3 | write | EBX = buffer user, ECX = longitud, máximo 160 bytes |
| 4 | report | verificación interna de la demo de aislamiento |
| 5 | yield | cede al siguiente slot READY; retorna 0 al reanudarse |

Una syscall desconocida devuelve -38. Write valida todo el rango y los permisos PDE/PTE antes de tocar un puntero. Rechaza kernel, guards, cruce hacia una página ausente y overflow con -14; longitud mayor de 160 con -22. Longitud cero es un no-op con resultado cero. No toma un terminador NUL como autoridad para salir del rango. La salida se prefija `USER OUT:` y los bytes no imprimibles, salvo newline, se reemplazan por `?`.

## Fallos y presupuesto

Un fallo de programa desde CPL 3 termina esa tarea y conserva vector/error/CR2. Continúan las demás tareas READY; se vuelve a la shell cuando termina el lote. Un fallo originado dentro del kernel sigue siendo fatal; tampoco se presenta NMI/double fault/machine check como un error recuperable de programa.

IRQ0 aplica un presupuesto de 250 ticks entregados a CPL3 por tarea. Un bucle infinito se termina con estado 124; la IRQ envía EOI antes de cambiar el contexto. El planificador round-robin rota después de cinco ticks user o una cesión explícita. El presupuesto es muestreo de CPU user, no una medida exacta de tiempo ni un límite estricto de tiempo de pared. Mientras se ejecuta el lote, siguen avanzando timer y cola de teclado, pero la shell y la red esperan al retorno.

## Comandos de prueba

```text
ring3-test
user-run demo
user-run write
user-run bad-buffer
user-run kernel-read
user-run code-write
user-run io
user-run guard
user-run gate
user-run foreign-read
user-run spin
```

A diferencia de `fault ...`, estas demos de fallo user deben regresar a la shell. Los resultados esperados incluyen #PF/error 5 para leer kernel, #PF/error 7 para escribir código, #GP/error 0 para puertos, #PF/error 6 para escribir un guard y #GP/error 258 para intentar INT 0x20. `bad-buffer` comprueba los códigos de error de write sin detener el kernel. `write` imprime `hello from ring3`.

## Alcance comprobado y límites

Pruebas de CPL/TSS/CR3 reales en QEMU, aislamiento de datos, fault recovery, buffers inválidos, salida normal, timeout y restauración exacta de frames. Las pruebas principales usan 32 MiB; se añaden arranque/roots privados/fault recovery con 16 y 64 MiB. La red HTTP se prueba en el mismo invitado después de una ejecución user y de un #GP recuperado.

Hay multitarea preemptiva básica por lotes y gestor background, NotFS y exec de fuente NotLang en VM ring3. No hay ELF, señales, IPC, heap de aplicación, SMP, NX, ASLR ni pila alternativa para double fault. La demo usa un programa assembly incluido en el kernel, de hasta una página; `run` conserva el intérprete kernel y `run-user`/`exec` usan la VM aislada. No es una auditoría de seguridad, un sandbox de producción ni una promesa de recuperación ante cualquier fallo de hardware o del kernel.
