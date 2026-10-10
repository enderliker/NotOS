# IDT, IRQ y temporizador — NotOS 0.5

## Fase 2 completada para el PC emulado

- IDT propia con 256 puertas de interrupción de 32 bits, DPL 0 y selector de código 0x08.
- Stubs x86 que normalizan los códigos de error, conservan registros y segmentos, limpian DF y alinean la pila antes de entrar a C.
- IRET restaura EIP, CS y EFLAGS. Los offsets del frame C se verifican al compilar.
- PIC 8259 remapeado: IRQ0..7 en vectores 32..39 y IRQ8..15 en 40..47. Se habilitan IRQ0, IRQ1 y la cascada IRQ2; las demás líneas permanecen enmascaradas.
- EOI específicos y tratamiento de IRQ7/15 espurias consultando los registros ISR del PIC.
- PIT canal 0, modo 2, divisor 1193: aproximadamente 1000 IRQ por segundo.
- Teclado PS/2 por IRQ1, con una cola circular de scancodes y procesamiento diferido en el bucle principal.

## Excepciones

INT3 es recuperable y se usa para autopruebas. Las otras excepciones entran en panic: vector, nombre, código de error, EIP, CS, EFLAGS y registros. Page fault imprime CR2 cuando corresponde. La CPU se detiene con CLI/HLT; no se intenta continuar con estado incierto.

```text
int3
fault divide
fault opcode
fault gp
```

**Los comandos `fault` detienen deliberadamente el sistema; hay que reiniciar QEMU.** No son errores de NotLang. Ejecutan una división por cero, UD2 y una carga de DS con el selector inválido 0x20. Esta última debe producir #GP con código 0x30.

`int3` comprueba registros generales, CF, DF y conservación de IF. El handler C siempre entra con DF limpio aunque la excepción se origine con DF activo.

## Reloj y espera

```text
uptime
sleep 200
busy 250
irqs
irq-test
```

- `uptime`: milisegundos acumulados, cantidad de IRQ0 e IF.
- `sleep MS`: espera mediante HLT; el par STI/HLT se ejecuta después de comprobar la condición con IF desactivado para evitar la carrera de despertar perdido.
- `busy MS`: ocupa el primer plano; las IRQ siguen actualizando el reloj y encolando el teclado.
- `irqs`: contadores de timer, teclado, IRQ inesperadas, scancodes descartados y IRQ espurias.
- `irq-test`: simula entradas a los vectores IRQ7/15 sin bits ISR activos para comprobar las rutas espurias. No es una prueba de que el hardware haya generado una IRQ espuria real.

`busy` y `sleep` aceptan 0..5000 ms. Mientras ejecutan la espera no se procesan comandos ni se sondea la red; no hay preemption del kernel. Los handlers no dibujan la UI ni ejecutan NotLang. Actualizan el reloj, encolan datos, contabilizan IRQ y envían EOI; desde NotOS 0.8, IRQ0 también selecciona el siguiente frame user, sin asignar memoria ni imprimir trazas en el handler.

El reloj usa los IRQ recibidos, no lecturas del contador PIT desde el bucle principal. La aritmética de fracciones corrige el redondeo del divisor 1193 respecto de la frecuencia nominal 1193182 Hz. Los contadores de 32 bits y las diferencias sin signo toleran rollover; esto se prueba nativamente usando la misma función que llama el handler.

## Teclado

La inicialización del controlador i8042 se hace con interrupciones desactivadas y PIC enmascarado. Se habilitan IRQ1, traducción a scancodes set 1 y escaneo; el segundo puerto queda desactivado. El handler descarta bytes AUX, ACK y errores de paridad/timeout, y entrega scancodes a una cola de 255 elementos útiles. El consumidor usa una sección crítica corta y conserva el estado previo de IF. Si la cola se llena se contabilizan descartes; no hay memoria dinámica en el handler.

El decodificador y la shell se ejecutan fuera de la IRQ. Izquierda/derecha Shift se contabilizan por separado. La prueba escribe una orden mediante PS/2 durante `busy 1000` y comprueba que se ejecuta después sin pérdida de teclas.

## Pruebas y alcance

`make test` incluye fallos de CPU reales, registro/flags tras IRET, despertar HLT repetido, progreso de IRQ0 durante trabajo en primer plano y entrada por IRQ1. Incluye también las regresiones de gráficos, NotLang y HTTP local. `make test-online` comprueba DNS/HTTP externos con IRQ activas.

No se ha probado hardware real, APIC, SMP, cambios de privilegio ni todos los tipos de excepción. El kernel permanece en ring 0; hay una demo síncrona en ring 3, pero aún no un gestor de procesos general y tiene una TSS para cambio de privilegio, pero no TSS/pila alternativa para double fault, procesos aislados ni recuperación de un stack supervisor corrupto. Las demos de usuario pueden terminar por #PF/#GP sin detener el kernel; véase USERSPACE.md. Un handler general no equivale a haber probado cada excepción posible.

Este reloj no es de tiempo real duro: si se mantienen interrupciones desactivadas demasiado tiempo, el PIC puede fusionar ticks y perder resolución. El rollover se verifica en una prueba de la aritmética, no dejando QEMU encendido durante semanas. La consola serie y RTL8139 continúan sondeadas. El bucle principal aún hace espera activa fuera del comando `sleep`.
