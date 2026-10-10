# Port real de curl: estado y gates

Objetivo: compilar y ejecutar el cliente curl **upstream** como ELF de usuario, sin renombrar `https` ni usar TLS/proxy del anfitrión.
La arquitectura sigue siendo x86 de 32 bits: INT 0x80 es la entrada de syscall adecuada; no requiere syscall/sysret de x86_64.

## Comprobado

- PAE, páginas de 4 KiB, CR0.WP y EFER.NXE, texto RX, datos/pilas RW/NX y aislamiento ring3.
- IRQ0, reloj, procesos básicos, cambio de contexto y SYS_SLEEP bloqueante para procesos gestionados; todavía sin bloqueo de sockets.
- RTL8139, ARP, IPv4, DNS UDP y un transporte TCP educativo de una conexión. No es API BSD sockets.
- TLS1.2/1.3, CA/RTC/DRBG, SNI/SAN/EKU/firmas y pruebas negativas dentro del invitado.
- NotFS propio persistente; montaje read-only `/disk` y tmpfs `/tmp`. FDs privados, stdio binario, dup/seek y stdin bloqueante en ring3. Ver VFS.md; no es POSIX completo.

## Base ELF y VM implementada

ELF32 estático validado, BSS multipágina, pila de 64 KiB, heap/brk y mmap anónimo RW/NX privados. El parser y la ejecución tienen pruebas específicas; ver ELF.md.
Picolibc 1.8.11 ya ejecuta malloc/free, stdio entero, strings, errno privado, tiempo64, espera y entropía en un ELF ring3. CRT, argc/argv, envp vacío, AT_NULL, constructores y exit/atexit/destructores están comprobados. Ver LIBC.md: no es ABI POSIX completa ni TLS de threads.

## Pendiente (no declarar curl disponible antes de los gates finales)

1. Extender runtime/argumentos según lo que requiera curl y cargar ejecutables desde VFS. Sin TLS de threads; solo proceso single-thread por ahora.
2. La base FDs/tmpfs/stdin ya está comprobada. Para el port final de curl, completar los hooks adicionales que necesite, validar sus rutas/config y ampliar cuotas para descargas/CA bundle si corresponde. `/disk` es read-only; no hay escritura POSIX persistente.
3. Transporte TCP general y API sockets, connect/send/recv/options, fcntl y getaddrinfo DNS; no basta la conexión educativa del kernel.
4. Extender TASK_BLOCKED/SYS_SLEEP a eventos de sockets/FDs y poll/select, además de timeouts ya comprobados.
5. Completar los hooks y funciones de libc adicionales que demande la compilación de curl; el port inicial no prueba toda la biblioteca upstream ni float/libm.
6. curl upstream estático HTTP-only usando el sysroot reproducible, adaptando su configuración y ejecutando peticiones y `-o` dentro del invitado.
7. Backend TLS de curl en userspace: TLS1.3/1.2, CA/hostname/fecha, timeouts, errores y CLI verificadas. El TLS kernel de `https` no satisface este gate por sí solo.

Pruebas finales: `curl --version`, HTTP/HTTPS reales, peer solo 1.2 y solo 1.3, salida a archivo, DNS/timeout/error, rechazo de certificados y recursos restaurados.
No se añade un comando `curl` simulado para aparentar cumplimiento.
