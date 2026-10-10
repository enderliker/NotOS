# Hitos

Cada hito se sube después de probar la imagen real con QEMU. Formato de commit: `feat(alcance) mensaje`.

## Primer prototipo: completado

1. **Arranque:** cargador propio, modo protegido, kernel C y salida serie.
2. **Interfaz gráfica:** VGA gráfico, renderizado propio, ventana de terminal y entrada mediante teclado PS/2.
3. **Lenguaje:** intérprete NotLang con variables enteras, expresiones y errores comprobados.
4. **Red:** RTL8139 y pila propia; DNS y petición HTTP pública comprobados. No incluye TLS.

## Fase 2: interrupciones — completada en QEMU

1. IDT, stubs x86, INT3 recuperable y diagnóstico de excepciones fatales.
2. PIC 8259 remapeado, EOI y tratamiento de IRQ7/15 espurias.
3. PIT por IRQ0, reloj con fracciones y espera HLT, sin depender del sondeo del bucle principal.
4. Teclado por IRQ1, cola y procesamiento diferido fuera de los handlers.
5. Pruebas de CPU, temporizador, teclado y regresión de gráficos/NotLang/red.

Se subió en dos avances comprobados: `feat(excepciones)` y `feat(interrupciones)`. Ver [alcance y limitaciones](INTERRUPTS.md).

## Fase 3: memoria — completada en QEMU

- **Completado:** BIOS E820, reserva de memoria y asignador de frames de 4 KiB. Cargador ampliado a 64 KiB y segunda lectura comprobada.
- **Completado:** paginación, CR0.WP, mappings/TLB y pruebas de fallos de página reales.
- **Completado:** heap virtual de 1 MiB, kmalloc/kcalloc/kfree, split/coalesce, guards y pruebas de agotamiento/reutilización.

Se subió en tres avances comprobados: `feat(memoria)`, `feat(paginacion)` y `feat(heap)`. Ver [diseño, pruebas y límites](MEMORY.md).

## Fase 4: aislamiento básico — completada en QEMU

- **Completado:** GDT/TSS, CPL3 real, pila ESP0, INT 0x80 y retorno al kernel con limpieza de frames.
- **Completado:** directorios CR3 privados, datos user separados, terminación recuperable de fallos y validación completa de buffers.
- **Completado:** presupuesto de ejecución por IRQ0 y pruebas de bucles infinitos, permisos I/O/IDT y regresión de red.

Se subió en dos avances comprobados: `feat(ring3)` y `feat(aislamiento)`. Las demos son síncronas, no un sistema de procesos completo. Ver [alcance y límites](USERSPACE.md).

## Fase 5: multitarea básica — completada en QEMU

- **Completado:** modelo de tareas y separación entre runtime, syscalls y diagnóstico, conservando las regresiones.
- **Completado:** round-robin, contextos/pilas propios y cambios por IRQ0 comprobados con workers sin yield.
- **Completado:** SYS_YIELD, lotes de cuatro tareas, fallos concurrentes, presupuesto individual, FPU rechazada por #NM y rollback de creación ante OOM.

Se subió en tres avances comprobados: `feat(tareas)`, `feat(planificador)` y `feat(multitarea)`. Ver [contratos, pruebas y límites](TASKS.md).

## Próximos hitos, aún no implementados

- Gestor de procesos persistentes y ejecución en segundo plano.
- Ratón y UI interactiva más allá de la terminal.
- Almacenamiento persistente y archivos de programas.
- Retransmisión TCP, configuración de red y, posteriormente, HTTPS.
- Ampliaciones de NotLang y ejecución fuera del kernel.

Las primeras versiones son prototipos de laboratorio, no sustitutos de un sistema de escritorio. No se declara un hito completo solo por compilar, y no se afirma soporte para hardware que no se haya probado.
