# NotOS

Prototipo de sistema operativo educativo escrito desde cero para x86 de 32 bits. Tiene cargador y kernel propios: no usa Linux, otro kernel ni una distribución como base. GCC, Python y QEMU son herramientas del equipo de desarrollo, no componentes del sistema invitado.

## Implementado y probado

| Hito | Implementación | Prueba |
| --- | --- | --- |
| Arranque | Cargador BIOS propio, imagen cruda, A20, GDT y modo protegido | Ejecución real del kernel C en QEMU |
| Interfaz gráfica | VGA 320×200, fuente bitmap propia y ventana de terminal | Captura del framebuffer e interacción PS/2 |
| Lenguaje | NotLang: `let`, `print`, variables y aritmética entera | Pruebas nativas y programas dentro del kernel |
| Red | Driver RTL8139, ARP, IPv4, UDP/DNS y TCP/HTTP | Servidor de prueba por NAT y DNS/HTTP de internet |
| Excepciones | IDT propia de 256 entradas, frame x86 y pantalla de panic | INT3 recuperable y fallos reales #DE, #UD y #GP |
| Interrupciones | PIC 8259, PIT por IRQ0 y teclado por IRQ1 | HLT, reloj durante trabajo en primer plano y cola PS/2 |
| Memoria física | Mapa BIOS E820 y frames de 4 KiB con bitmap | Agotamiento/liberación en QEMU con 16/32/64/128 MiB |
| Paginación | CR0.PG/WP, código de solo lectura y ventana virtual | Aliases, TLB y fallos #PF reales con CR2 |
| Heap | kmalloc/kcalloc/kfree, split/coalesce y alineación de 16 bytes | 20000 operaciones nativas y asignaciones reales en el kernel |
| Aislamiento básico | GDT/TSS, CR3 privados, INT 0x80 y buffers validados | CPL3 real, datos separados, fallos user y timeout recuperables |
| Archivos | ATA PIO y NotFS propio en disco de datos separado | Reinicios, CRC, rollback y CRUD persistente |
| Multitarea básica | Round-robin por IRQ0, cuatro slots, pilas privadas y SYS_YIELD | Contextos/registros, lotes mixtos, presupuesto, FPU rechazada y rollback OOM |

Cada avance se sube en un commit `feat(alcance) mensaje` después de sus pruebas. No es un sistema de escritorio completo ni está listo para uso diario.

## Construir, probar y arrancar

Objetivo de CPU: x86 de 32 bits con WP/INVLPG (486 o posterior), probado solo en QEMU.

Requisitos del host: GNU Make, GCC capaz de generar código `-m32` (el kernel no necesita libc de 32 bits), GNU binutils, Python 3 y `qemu-system-i386`. En Ubuntu/Debian:

```sh
sudo apt-get install gcc binutils make python3 qemu-system-x86
make
make test
make test-online  # opcional: requiere DNS y HTTP salientes
make run
```

`make run` abre QEMU con gráfica y salida serie en la terminal. La imagen arrancable es `build/notos.img`. Las pruebas usan emulación TCG, sin necesitar KVM. Se puede seleccionar otro ejecutable con la variable de entorno `QEMU` para las pruebas.

El workflow de GitHub ejecuta `make clean test` y conserva los artefactos. Su ejecución remota no fue comprobada desde este entorno.

## Comandos del sistema

```text
help
about
clear
echo Hola NotOS
run let x = (2 + 3) * 4; print x
run print x - 7
lang-reset
net
dns example.com
http example.com
http 10.0.2.2:8080
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

BIOS carga nuestro sector de 512 bytes. El cargador lee dos bloques de 64 sectores en `0x10000` y `0x18000`, activa A20, configura una GDT, selecciona VGA modo 13h y entra a modo protegido. La entrada del kernel configura una pila en `0x90000` y limpia BSS antes de llamar a C. El payload no puede superar 64 KiB; linker y generador verifican el límite. Una firma al final verifica la segunda lectura. El kernel detecta RAM mediante BIOS E820 y reserva el primer MiB; el gestor de frames tiene un límite físico de 64 MiB.

El kernel funciona en ring 0 con paginación supervisor, frames físicos y un heap virtual de 1 MiB. El código/rodata están protegidos contra escritura y la página nula no está mapeada. El PIC está remapeado, el PIT actualiza el reloj por IRQ0 y el teclado encola scancodes por IRQ1; la shell los procesa fuera del handler. La consola serie y la red siguen sondeadas. La red tiene buffers DMA estáticos y usa la tarjeta RTL8139 emulada y la NAT de QEMU para salir del equipo. El invitado genera y procesa sus propios paquetes; no llama a sockets de un sistema operativo base.

## Límites importantes

- Solo PC BIOS/x86 y hardware emulado especificado; no UEFI ni soporte comprobado en equipos reales.
- Lotes síncronos de hasta cuatro tareas ring 3, con round-robin por IRQ0 y espacios/pilas privados; gestor de cuatro procesos background; sin carga de aplicaciones externas ni instalador. El heap es fijo de 1 MiB y el gestor físico administra como máximo 64 MiB, según E820; no hay crecimiento automático.
- Los fallos comprobados de las demos user terminan la ejecución y regresan a la shell; los fallos del kernel siguen siendo fatales. No hay pila independiente para double fault. Las IRQ de timer y teclado están activas, pero no hay APIC, SMP ni preemption del kernel. FPU/MMX/SSE no están soportados en tareas.
- Interfaz gráfica mínima, sin ratón, navegador ni gestor completo de ventanas.
- NotLang es un intérprete pequeño, sin bucles, funciones ni acceso a archivos.
- Red con IP estática para QEMU, una solicitud a la vez, IPv4 y HTTP sin cifrar. Sin DHCP, IPv6, TLS/HTTPS, retransmisión TCP propia ni reensamblado de fragmentos IP. El cliente muestra el estado HTTP y la cantidad de bytes, no renderiza páginas web.
- Pensado para pruebas de laboratorio; no usar para información sensible ni redes hostiles.

## Documentación

- [Arquitectura y convenciones](docs/ARCHITECTURE.md)
- [Ejecución de usuario](docs/USERSPACE.md)
- [Multitarea y planificador](docs/TASKS.md)
- [Procesos en segundo plano](docs/PROCESSES.md)
- [Memoria](docs/MEMORY.md)
- [Almacenamiento](docs/STORAGE.md)
- [NotLang](docs/NOTLANG.md)
- [Red](docs/NETWORK.md)
- [Interrupciones y excepciones](docs/INTERRUPTS.md)
- [Pruebas y resultados](docs/TESTING.md)
- [Hitos y próximos pasos](docs/ROADMAP.md)
