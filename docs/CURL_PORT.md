# Port real de curl: estado y gates

Objetivo: compilar y ejecutar el cliente curl **upstream** como ELF de usuario, sin renombrar `https` ni usar TLS/proxy del anfitrión.
La arquitectura sigue siendo x86 de 32 bits: INT 0x80 es la entrada de syscall adecuada; no requiere syscall/sysret de x86_64.

## Comprobado

- PAE, páginas de 4 KiB, CR0.WP y EFER.NXE, texto RX, datos/pilas RW/NX y aislamiento ring3.
- IRQ0, reloj, procesos básicos y cambio de contexto, todavía sin bloqueo de sockets.
- RTL8139, ARP, IPv4, DNS UDP y un transporte TCP educativo de una conexión. No es API BSD sockets.
- TLS1.2/1.3, CA/RTC/DRBG, SNI/SAN/EKU/firmas y pruebas negativas dentro del invitado.
- NotFS propio persistente. No es un VFS POSIX.

## Pendiente (no declarar curl disponible antes de los gates finales)

1. ELF32 validado, segmentos multipágina W^X, BSS, pila razonable y rollback de cargas inválidas/OOM.
2. brk/mmap por proceso y errno/ABI de retornos; pruebas reales de malloc/free en ring3.
3. Planificador con estado bloqueado y despertar por timeout/evento sin congelar la shell.
4. Port de libc pequeña con syscalls: hello world y tests de asignación/stdio/strings/tiempo. Los headers freestanding de TLS no son esa libc.
5. FDs por proceso, VFS RAM/NotFS y stdin/stdout/stderr.
6. Transporte TCP general y API sockets, connect/send/recv/poll/options y getaddrinfo DNS.
7. curl upstream estático HTTP-only, toolchain/sysroot reproducible; ejecutar peticiones y `-o` dentro del invitado.
8. Backend TLS de curl en userspace: TLS1.3/1.2, CA/hostname/fecha, timeouts, errores y CLI verificadas. El TLS kernel de `https` no satisface este gate por sí solo.

Pruebas finales: `curl --version`, HTTP/HTTPS reales, peer solo 1.2 y solo 1.3, salida a archivo, DNS/timeout/error, rechazo de certificados y recursos restaurados.
No se añade un comando `curl` simulado para aparentar cumplimiento.
