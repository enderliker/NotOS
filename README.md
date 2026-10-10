# NotOS

Prototipo de sistema operativo educativo escrito desde cero para x86 de 32 bits. Tiene cargador y kernel propios: no usa Linux, otro kernel ni una distribución como base. TLS utiliza MbedTLS 3.6.5 (Apache-2.0), y HMAC-DRBG/SHA256 usa BearSSL (MIT); no se afirma que esa biblioteca criptográfica sea original. La libc de usuario es Picolibc 1.8.11 upstream, con sus licencias originales y un port propio; no se afirma que esa biblioteca sea original. GCC, Python y QEMU son herramientas del equipo de desarrollo, no componentes del sistema invitado.

## Implementado y probado

| Hito | Implementación | Prueba |
| --- | --- | --- |
| Arranque | Cargador BIOS propio, imagen cruda, A20, GDT y modo protegido | Ejecución real del kernel C en QEMU |
| Interfaz gráfica | VGA 320×200, PS/2 mouse, cursor, toolbar y minimizar/restaurar | Framebuffer, movimiento/clicks QMP y teclado PS/2 |
| Lenguaje | NotLang 0.1 kernel y 0.2 compilador/VM ring3, repeat y exec de archivos | Aritmética, loops, errores, aislamiento y ejecución tras reinicio |
| Red | Driver RTL8139, ARP, IPv4, UDP/DNS, TCP/HTTP/HTTPS, retry y config estática | NAT, pérdidas SYN/GET/SYN-ACK/respuesta y DNS/HTTP/HTTPS público, TLS1.2/1.3, X509, AEAD, pérdidas y certificados inválidos |
| Excepciones | IDT propia de 256 entradas, frame x86 y pantalla de panic | INT3 recuperable y fallos reales #DE, #UD y #GP |
| Interrupciones | PIC 8259, PIT por IRQ0 y teclado por IRQ1 | HLT, reloj durante trabajo en primer plano y cola PS/2 |
| Memoria física | Mapa BIOS E820 y frames de 4 KiB con bitmap | Agotamiento/liberación en QEMU con 16/32/64/128 MiB |
| Paginación | PAE, CR0.PG/WP, NX y código W^X | Aliases, TLB y fallos #PF reales con CR2 |
| Heap | kmalloc/kcalloc/kfree, split/coalesce y alineación de 16 bytes | 20000 operaciones nativas y asignaciones reales en el kernel |
| ELF32 y VM user | Segmentos PT_LOAD W^X, BSS, pila 64 KiB, brk y mmap privado | Parser con sanitizadores, ring3, cruces PAE, NX, concurrencia y rollback OOM |
| Libc de usuario | Picolibc estática, CRT, heap, stdio entero, errno, tiempo64 y entropía | ELF ring3, cuatro procesos, OOM, buffers, exit/flush y recursos restaurados |
| Aislamiento básico | GDT/TSS, CR3 privados, INT 0x80 y buffers validados | CPL3 real, datos separados, fallos user y timeout recuperables |
| Espera de procesos | SYS_SLEEP, TASK_BLOCKED y despertar por tiempo | Shell/worker concurrentes, cancelación, slots bloqueados y clock wrap |
| Archivos | ATA PIO y NotFS propio en disco de datos separado | Reinicios, CRC, rollback y CRUD persistente |
| Multitarea básica | Round-robin por IRQ0, cuatro slots, pilas privadas y SYS_YIELD | Contextos/registros, lotes mixtos, presupuesto, FPU rechazada y rollback OOM |

Cada avance se sube en un commit `feat(alcance) mensaje` después de sus pruebas. No es un sistema de escritorio completo ni está listo para uso diario.

## Construir, probar y arrancar

Objetivo de CPU: x86 de 32 bits con PAE, MSR y NX (QEMU `-cpu max`), probado solo en QEMU. El kernel comprueba las capacidades antes de activar PAE/NXE. El código se genera sin SSE/FPU.

Requisitos del host: GNU Make, GCC capaz de generar código `-m32` (el kernel no necesita libc de 32 bits), GNU binutils, Meson/Ninja, Python 3.12+ (para extracción segura de la dependencia), OpenSSL para fixtures y `qemu-system-i386`. En Ubuntu/Debian:

```sh
sudo apt-get install gcc binutils make python3 meson ninja-build openssl qemu-system-x86
make
make test
make test-online  # opcional: requiere DNS, HTTP y HTTPS salientes
make run
```

`make run` abre QEMU con gráfica y salida serie en la terminal. La imagen arrancable es `build/notos.img`. Las pruebas usan emulación TCG, sin necesitar KVM. Se puede seleccionar otro ejecutable con la variable de entorno `QEMU` para las pruebas.

La primera compilación descarga BearSSL, MbedTLS y Picolibc con SHA256 fijado; se conserva en `.cache/`. Para compilar offline, usa `BEARSSL_ARCHIVE=/ruta/bearssl-0.6.tar.gz` y `MBEDTLS_ARCHIVE=/ruta/mbedtls-3.6.5.tar.bz2` y `PICOLIBC_ARCHIVE=/ruta/picolibc-1.8.11.tar.xz` (hashes verificados).

El workflow de GitHub ejecuta `make clean test` y conserva los artefactos. Su ejecución remota no fue comprobada desde este entorno.

## Comandos del sistema

```text
help
about
clear
echo Hola NotOS
run let x = (2 + 3) * 4; print x
run print x - 7
run-user let n = 1; repeat 3 { let n = n * 2; }; print n
write demo.nl print 42
exec demo.nl
lang-reset
net
dns example.com
http example.com
https example.com
tls
tls-selftest
http 10.0.2.2:8080
net-config 10.0.2.15 10.0.2.2 10.0.2.3
uptime
sleep 200
busy 250
irqs
int3
mem
vm-test
heap
alloc 128
free 0
heap-test
ring3-test
multitask-test
libc-test
libc-run exit
elf-run demo
spawn worker
spawn spin
ps
kill 2
mkfs
write hello.txt Hola
cat hello.txt
ls
rm hello.txt
multitask-run four
multitask-run yield
multitask-run fault
multitask-run budget
multitask-run fpu
user-run write
user-run bad-buffer
user-run kernel-read
```

`F1` muestra ayuda y `F2` limpia la terminal. Se controla por teclado PS/2 o por COM1. La fuente inicial representa las minúsculas como mayúsculas; el intérprete conserva la entrada original. La red responde de forma asíncrona en la terminal.

## Diseño

BIOS carga nuestro sector de 512 bytes. El cargador lee doce bloques de 64 sectores consecutivos desde `0x10000`, activa A20, configura una GDT, selecciona VGA modo 13h y entra a modo protegido. La entrada del kernel configura una pila en `0x90000` y limpia BSS antes de llamar a C. El payload no puede superar 384 KiB; linker y generador verifican el límite. Una firma en 0x6fffc verifica la última lectura. El kernel detecta RAM mediante BIOS E820 y reserva el primer MiB y el BSS situado desde 1 MiB; el gestor de frames tiene un límite físico de 64 MiB.

El kernel funciona en ring 0 con paginación supervisor, frames físicos y un heap virtual de 1 MiB. El código/rodata están protegidos contra escritura y la página nula no está mapeada. El PIC está remapeado, el PIT actualiza el reloj por IRQ0 y el teclado encola scancodes por IRQ1; la shell los procesa fuera del handler. La consola serie y la red siguen sondeadas. La red tiene buffers DMA estáticos y usa la tarjeta RTL8139 emulada y la NAT de QEMU para salir del equipo. El invitado genera y procesa sus propios paquetes; no llama a sockets de un sistema operativo base.

## Límites importantes

- Solo PC BIOS/x86 y hardware emulado especificado; no UEFI ni soporte comprobado en equipos reales.
- Lotes síncronos de hasta cuatro tareas ring 3, con round-robin por IRQ0 y espacios/pilas privados; gestor de cuatro procesos background; exec de fuente NotLang persistente; ELF32 estático embebido con libc de usuario; sin VFS de ejecutables ni instalador. El heap es fijo de 1 MiB y el gestor físico administra como máximo 64 MiB, según E820; no hay crecimiento automático.
- Los fallos comprobados de las demos user terminan la ejecución y regresan a la shell; los fallos del kernel siguen siendo fatales. No hay pila independiente para double fault. Las IRQ de timer y teclado están activas, pero no hay APIC, SMP ni preemption del kernel. FPU/MMX/SSE no están soportados en tareas.
- Interfaz gráfica mínima con ratón PS/2, toolbar y minimizar/restaurar; sin navegador ni gestor completo de ventanas.
- NotLang tiene intérprete 0.1 y compilador/VM ring3 0.2 con repeat y exec de archivos fuente; sin funciones ni acceso a archivos desde las aplicaciones.
- Red con IP estática para QEMU, una solicitud a la vez, IPv4, HTTP y HTTPS/TLS1.2/1.3 con validación X509. Configuración estática /24 y retransmisión con backoff; sin DHCP, IPv6 ni reensamblado de fragmentos IP. TLS requiere semilla fresca por firmware y RTC UTC válidos. El cliente muestra el estado HTTP y la cantidad de bytes, no renderiza páginas web.
- Picolibc es un port parcial: stdio entero, heap y tiempo, sin stdin funcional, VFS POSIX, sockets ni float/libm. No equivale a curl disponible.
- Pensado para pruebas de laboratorio; no usar para información sensible ni redes hostiles.

## Documentación

- [Arquitectura y convenciones](docs/ARCHITECTURE.md)
- [Ejecución de usuario](docs/USERSPACE.md)
- [Multitarea y planificador](docs/TASKS.md)
- [Procesos en segundo plano](docs/PROCESSES.md)
- [ELF32 y memoria privada](docs/ELF.md)
- [Libc de usuario](docs/LIBC.md)
- [Memoria](docs/MEMORY.md)
- [Almacenamiento](docs/STORAGE.md)
- [Ratón y controles gráficos](docs/UI.md)
- [NotLang](docs/NOTLANG.md)
- [NotLang ring3 y programas](docs/NOTLANG_USER.md)
- [Red](docs/NETWORK.md)
- [HTTPS/TLS y sus límites](docs/TLS.md)
- [Interrupciones y excepciones](docs/INTERRUPTS.md)
- [Pruebas y resultados](docs/TESTING.md)
- [Hitos y próximos pasos](docs/ROADMAP.md)

## Port de curl — en desarrollo

TLS 1.3 conserva TLS 1.2, con negociación/verificación dentro del invitado; ver [TLS](docs/TLS.md).
PAE/NX ya protege datos y pilas; ELF32, brk y mmap anónimo privados se describen en [ELF](docs/ELF.md). Picolibc ya ejecuta una aplicación real en ring3 con malloc/stdio/errno/tiempo; ver [LIBC](docs/LIBC.md). **curl upstream todavía no está portado ni disponible**: `https` es el cliente HTTP educativo, no curl.
La hoja de ruta verificable de libc/ELF/memoria/sockets/VFS/poll está en [CURL_PORT](docs/CURL_PORT.md).
