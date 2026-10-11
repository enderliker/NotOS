# Hoja de ruta

Los hitos se consideran verificados con ejecución de la imagen real, además de
las pruebas nativas aplicables. [TESTING.md](TESTING.md) conserva la evidencia
histórica; cada documento de módulo especifica sus contratos y límites.

## Base implementada

| Área | Alcance actual | Documentación |
| --- | --- | --- |
| Arranque | BIOS, E820, modo protegido y carga en memoria baja/alta | [Memoria](MEMORY.md) |
| Interrupciones | IDT, PIC, PIT, PS/2 y fallos recuperables de aplicaciones | [Interrupciones](INTERRUPTS.md) |
| Memoria | Frames, heap kernel, PAE/NX, W^X y espacios privados | [Memoria](MEMORY.md), [ELF](ELF.md) |
| Ejecución | Cuatro procesos, round-robin, bloqueo, cancelación y recolección | [Tareas](TASKS.md), [Procesos](PROCESSES.md) |
| Aplicaciones | ELF32 estático embebido y Picolibc con runtime propio | [ELF](ELF.md), [Libc](LIBC.md) |
| Archivos | tmpfs, FDs privados, NotFS separado y shell cd/ls/cat/write/touch/rm/stat/df | [VFS](VFS.md), [Almacenamiento](STORAGE.md) |
| Red | RTL8139, TCP/IPv4 y DNS de usuario; cliente kernel HTTP/HTTPS | [Sockets](SOCKETS.md), [TLS](TLS.md) |
| Multiplexación | poll y adaptador select con espera por proceso | [Poll](POLL.md) |
| curl | Cliente upstream 8.11.1 HTTP, descargas y uploads | [Port de curl](CURL_PORT.md) |
| Interfaz | VGA, teclado/ratón PS/2, controles básicos | [UI](UI.md) |
| Lenguaje | NotLang kernel y compilador/VM ring 3 con archivos fuente | [NotLang](NOTLANG.md), [VM](NOTLANG_USER.md) |

## Próximo hito: HTTPS en curl

1. Adaptar un backend TLS al ELF de usuario sin reutilizar el comando kernel
   como implementación de curl.
2. Definir almacenamiento de raíces dentro de las cuotas del VFS, con
   procedencia y actualización explícitas.
3. Verificar TLS 1.2 y 1.3, CA/hostname/fechas, errores de certificados,
   truncamiento, timeout y recuperación exacta de recursos.
4. Añadir HTTP/HTTPS públicos y salida a archivo a la validación opcional online.

HTTP-only no completa ese hito. Las limitaciones de CLI, protocolos y tiempos
DNS están en [CURL_PORT.md](CURL_PORT.md).

## Trabajo posterior sin compromiso de fecha

- Carga de ejecutables desde VFS y aplicación de FD_CLOEXEC al ejecutar.
- Directorios reales y almacenamiento escribible persistente para aplicaciones.
- Ampliación del runtime/libc según necesidades comprobadas de aplicaciones.
- Nuevas funciones de red o UI, con alcance y pruebas definidos por separado.

No se declara soporte para hardware físico, UEFI, SMP, kernel preemptivo,
FPU/SSE, threads ni POSIX completo. Estos límites requieren decisiones de diseño
propias; no se resuelven elevando cuotas o cambiando nombres de comandos.
