# Hitos

Cada hito se sube después de probar la imagen real con QEMU. Formato de commit: `feat(alcance) mensaje`.

## Primer prototipo: completado

1. **Arranque:** cargador propio, modo protegido, kernel C y salida serie.
2. **Interfaz gráfica:** VGA gráfico, renderizado propio, ventana de terminal y entrada mediante teclado PS/2.
3. **Lenguaje:** intérprete NotLang con variables enteras, expresiones y errores comprobados.
4. **Red:** RTL8139 y pila propia; DNS y petición HTTP pública comprobados. No incluye TLS.

## Próximos hitos, aún no implementados

- IDT, excepciones, interrupciones y temporizador robusto.
- Memoria paginada, asignación dinámica y aislamiento.
- Ratón y UI interactiva más allá de la terminal.
- Almacenamiento persistente y archivos de programas.
- Retransmisión TCP, configuración de red y, posteriormente, HTTPS.
- Ampliaciones de NotLang y ejecución fuera del kernel.

Las primeras versiones son prototipos de laboratorio, no sustitutos de un sistema de escritorio. No se declara un hito completo solo por compilar, y no se afirma soporte para hardware que no se haya probado.
