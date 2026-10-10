# Multitarea básica — NotOS 0.8

## Contratos y separación

- `task.*`: estado, contexto inicial, recursos y resultado. Objetos inicialmente a cero; rollback al fallar creación; nunca liberar una tarea activa.
- `scheduler_policy.*`: función pura round-robin, sin hardware, consola ni asignación. Solo selecciona READY; ignora UNUSED/FINISHED.
- `scheduler.*`: lote síncrono de 1..4 tareas, quantum, estadísticas y traza acotada. Rechaza tamaños inválidos, punteros repetidos, IDs repetidos y estados no READY antes de cambiar el runtime.
- `syscall.*`: ABI y rangos user. `SYS_YIELD` devuelve 0 cuando la tarea vuelve a ejecutarse.
- `multitask.*`: escenarios de laboratorio, validación y reportes, fuera de IRQ. El protocolo de la imagen de prueba está separado en `user_test_abi.h`.
- `arch.*`, `user_entry.S`, `isr.S`: TSS y restauración de frames x86.

Estado: `UNUSED -> READY -> RUNNING -> READY` por quantum/cesión, o `RUNNING -> FINISHED` por exit/fallo/presupuesto. Los resultados se inspeccionan antes de liberar los recursos; una tarea FINISHED no puede ser elegida otra vez.

## Contextos y tiempo

Cada tarea conserva sus cinco frames privados de memoria y una pila supervisor de 16 KiB del heap. El canario detecta corrupción en su extremo bajo; no es una página guard ni una solución general de overflow. La TSS recibe ESP0 de la siguiente tarea. Se selecciona su CR3 y se restaura su frame: registros generales, segmentos, EIP, flags y ESP/SS user. El frame inicial mide 76 bytes, comprobados en compilación.

IRQ0 envía EOI **antes** de transferir a otro contexto. El dispatcher devuelve el puntero al frame elegido; assembly cambia ESP y ejecuta IRET. El frame del llamador kernel se conserva por separado y se recupera, con su CR3, cuando no quedan tareas READY. El lote no permite anidamiento.

Quantum: cinco IRQ0 recibidas en CPL3. Presupuesto: 250 IRQ0 recibidas en CPL3 por tarea; al agotarse termina con estado 124. No es tiempo de pared ni contabilidad exacta de CPU: no cuenta trabajo kernel y puede perder muestras con IRQ desactivadas. No se ofrece una garantía contra todos los patrones hostiles de syscalls/yield. `SYS_TICKS` consulta el reloj global en ms, no el consumo de una tarea.

Las fallas user normales terminan solo la tarea afectada. NMI, double fault, machine check y errores originados en kernel siguen siendo fatales. CR0.TS rechaza uso de x87/MMX; SIMD tampoco está habilitado. El test x87 comprueba #NM/vector 7. No hay guardado/restauración de estado FPU/SIMD.

## Escenarios

```text
multitask-test          # dos workers: ninguna cesión voluntaria
multitask-run workers
multitask-run four      # cuatro workers y wrap de round-robin
multitask-run yield     # dos workers con SYS_YIELD
multitask-run fault     # dos workers sobreviven al #PF de la tercera tarea
multitask-run budget    # worker normal + bucle infinito, termina con 124
multitask-run fpu       # uso x87 rechazado; otro worker termina normalmente
```

Workers mantienen registros EBX/EDX/ESI/EDI/EBP y DF como canarios, contador y marcador en su página privada. Comprueban esos valores al reanudarse y salen con 42. Cada TASK reporta CR3 real verificado, dirección de pila, ticks, despachos y preemptions.

Trazas: causas 0 inicio, 1 timer, 2 exit, 3 fallo, 4 presupuesto, 5 yield; ID 0 representa al llamador kernel. Se conservan solo los primeros 32 eventos, sin imprimir dentro del handler. Los contadores cubren el lote completo; una traza truncada no es un historial exhaustivo.

## Pruebas y límites

`make test` prueba la política nativamente y arranca lotes reales con 16/32/64 MiB. Comprueba contextos privados, todos los escenarios, rechazo de lotes inválidos, exclusión de terminadas, rollback cuando no cabe ninguna pila o solo cabe una parte del lote, y restauración exacta de frames/heap. Repite escenarios en 32 MiB y verifica INT3/IRQ/heap después. Los tests HTTP locales prueban red después de fallos y presupuesto en la misma VM; `make test-online` prueba DNS/HTTP externo después de lotes de cuatro tareas y del rechazo de x87.

No hay SMP, kernel preemptivo, bloqueo/wakeup, prioridades, IPC ni ELF. Los lotes de este módulo son síncronos; shell y procesamiento de red esperan a que termine el lote y IRQ0/IRQ1/IRQ12 siguen activos. Desde 0.9, el gestor [PROCESSES.md](PROCESSES.md) ejecuta pulsos background entre iteraciones del loop; NotLang 0.2 puede ejecutarse en VM ring3 (NOTLANG_USER.md). Es un hito de laboratorio, no un sistema operativo terminado ni un sandbox auditado.
