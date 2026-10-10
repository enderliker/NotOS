# NotOS

Sistema operativo educativo escrito desde cero para x86 de 32 bits. No usa Linux, otro kernel ni una distribución como base. GCC y QEMU son herramientas del equipo de desarrollo, no componentes del sistema.

## Estado comprobado

- Cargador BIOS propio, imagen de disco cruda y entrada a modo protegido.
- Kernel freestanding en C y ensamblador, con inicialización de BSS y pila.
- Escritorio VGA de 320×200 con ventana de terminal y fuente bitmap propia.
- Entrada por teclado PS/2 y consola serie COM1; comandos `help`, `about`, `clear` y `echo`.
- `F1` muestra ayuda y `F2` limpia la terminal.

Todavía no hay red ni lenguaje. Se implementarán por hitos separados, con pruebas antes de cada commit. La interfaz inicial se controla por teclado: no tiene ratón ni aplicaciones de escritorio. No es un sistema listo para uso diario.

## Construir y probar

Requisitos en el host: GNU Make, GCC con generación de código `-m32` (sin libc de 32 bits), GNU binutils, Python 3 y `qemu-system-i386`.

```sh
make
make test
make run
```

El test arranca realmente la imagen en QEMU, captura su salida VGA, envía teclas al dispositivo PS/2 y verifica comandos por el puerto serie. `build/smoke.log` conserva la salida. QEMU usa emulación por software, sin necesitar KVM.

## Diseño inicial

BIOS carga el sector de 512 bytes. Nuestro cargador usa INT 13h para leer 64 sectores en `0x10000`, activa A20, instala una GDT y entra a modo protegido. El kernel utiliza una pila en `0x90000`; la imagen admite hasta 32 KiB de código/datos. La entrada limpia BSS antes de llamar a C. Las interrupciones permanecen desactivadas en esta etapa.

Objetivo inicial: PC BIOS emulado en QEMU. UEFI, hardware real, multiproceso, aislamiento, disco persistente y compatibilidad con aplicaciones existentes quedan fuera de esta primera versión.
