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

## Fase 3: memoria (en curso)

- **Completado:** BIOS E820, reserva de memoria y asignador de frames de 4 KiB. Cargador ampliado a 64 KiB y segunda lectura comprobada.
- **Completado:** paginación, CR0.WP, mappings/TLB y pruebas de fallos de página reales.
- **Pendiente:** heap dinámico.

El aislamiento completo y la ejecución en ring 3 requieren pasos adicionales; todavía no están implementados.

## Próximos hitos, aún no implementados

- Memoria paginada, asignación dinámica y aislamiento.
- Ratón y UI interactiva más allá de la terminal.
- Almacenamiento persistente y archivos de programas.
- Retransmisión TCP, configuración de red y, posteriormente, HTTPS.
- Ampliaciones de NotLang y ejecución fuera del kernel.

Las primeras versiones son prototipos de laboratorio, no sustitutos de un sistema de escritorio. No se declara un hito completo solo por compilar, y no se afirma soporte para hardware que no se haya probado.
