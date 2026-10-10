# Hitos

Cada hito se sube después de probar la imagen real con QEMU. El formato de commit es `feat(alcance) mensaje`.

1. **Arranque:** cargador propio, modo protegido, kernel C y salida serie.
2. **Interfaz gráfica:** VGA en modo gráfico, renderizado propio e interacción mediante teclado; consola serie para pruebas.
3. **Lenguaje:** intérprete propio, variables enteras, expresiones y mensajes de error.
4. **Red:** controlador de tarjeta emulada y pila propia; verificar DNS y una petición HTTP. Sin TLS inicialmente.

Las primeras versiones son prototipos de laboratorio, no sustitutos de un sistema de escritorio. Antes de usarlas en hardware real hacen falta gestión de memoria, interrupciones y excepciones, aislamiento, almacenamiento, detección de hardware y pruebas adicionales. No se declara un hito completo solo por compilar.
