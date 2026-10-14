# Ratón y controles gráficos

PS/2 estándar de tres bytes, IRQ12. Inicialización i8042 con PIC enmascarado, configuración del segundo puerto y comandos F6/F4 comprobados por ACK. IRQ1/12 comparten el drenaje del controlador y separan bytes AUX de scancodes: el handler solo encola. El decodificador puro interpreta signo y botones, rechaza overflow y resincroniza el primer byte por su bit 3.

El event loop procesa hasta 128 bytes por vuelta. Cursor de 8×8 con respaldo del framebuffer, coordenadas recortadas a 320×200. La consola restaura el fondo antes de dibujar y vuelve a superponer el cursor para no dejar rastros.

- **CLR** limpia la terminal y descarta su entrada parcial.
- **FILES** lista el directorio actual de la shell; **TASKS** muestra procesos activos.
- Click en la barra de título minimiza/restaura la ventana. La consola conserva sus celdas y sigue recibiendo salida cuando está minimizada.
- `mouse` informa coordenadas, botones, paquetes y descartes.

Las acciones de UI se ejecutan fuera de IRQ. No hay ratón USB, rueda, ventanas arbitrarias, arrastre, resolución configurable ni escritorio completo. El soporte comprobado es el PS/2 emulado por QEMU; no se afirma compatibilidad con hardware real.

Tests nativos: sincronización, signo, botones y overflow. Test QMP: movimiento y clicks reales, toolbar, minimizar/restaurar, captura del framebuffer y convivencia con teclado/IRQ0/INT3.

## Edición de shell

Historial circular de ocho líneas, con borrador al navegar Up/Down; se omiten
repeticiones consecutivas. Left/Right, Home/End, Backspace y Delete editan con
cursor sin consumir datos de procesos. Tab completa comandos o componentes de
rutas por prefijo común; muestra candidatos cuando no puede avanzar y añade
`/` para un directorio único. Sin globbing, comillas ni escapes.

`line_core.*` es lógica pura con tests ASAN/UBSAN. `line_input.*` adapta VGA,
serie CSI/SS3 y listado VFS; las secuencias de control se interpretan en el
loop, y IRQ solo encola. Límite de línea: 159 caracteres para comandos comunes,
320 para curl/spawn-elf; sus argumentos permanecen limitados a 256 bytes.
El renderizado conserva el texto pero no implementa una terminal ANSI general
ni reconstruye líneas desplazadas fuera de la pantalla por salida background.
