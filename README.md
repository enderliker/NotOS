# NotOS

Prototipo de sistema operativo educativo escrito desde cero para x86 de 32 bits. Tiene cargador y kernel propios: no usa Linux, otro kernel ni una distribución como base. GCC, Python y QEMU son herramientas del equipo de desarrollo, no componentes del sistema invitado.

## Implementado y probado

| Hito | Implementación | Prueba |
| --- | --- | --- |
| Arranque | Cargador BIOS propio, imagen cruda, A20, GDT y modo protegido | Ejecución real del kernel C en QEMU |
| Interfaz gráfica | VGA 320×200, fuente bitmap propia y ventana de terminal | Captura del framebuffer e interacción PS/2 |
| Lenguaje | NotLang: `let`, `print`, variables y aritmética entera | Pruebas nativas y programas dentro del kernel |
| Red | Driver RTL8139, ARP, IPv4, UDP/DNS y TCP/HTTP | Servidor de prueba por NAT y DNS/HTTP de internet |

Cada hito se subió en un commit `feat(alcance) mensaje` después de sus pruebas. No es un sistema de escritorio completo ni está listo para uso diario.

## Construir, probar y arrancar

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
```

`F1` muestra ayuda y `F2` limpia la terminal. Se controla por teclado PS/2 o por COM1. La fuente inicial representa las minúsculas como mayúsculas; el intérprete conserva la entrada original. La red responde de forma asíncrona en la terminal.

## Diseño

BIOS carga nuestro sector de 512 bytes. El cargador lee 64 sectores en `0x10000`, activa A20, configura una GDT, selecciona VGA modo 13h y entra a modo protegido. La entrada del kernel configura una pila en `0x90000` y limpia BSS antes de llamar a C. El código/datos de la imagen no pueden superar 32 KiB; el generador verifica este límite.

El kernel funciona en ring 0, con memoria física y dispositivos sondeados. La red tiene buffers DMA estáticos y usa la tarjeta RTL8139 emulada y la NAT de QEMU para salir del equipo. El invitado genera y procesa sus propios paquetes; no llama a sockets de un sistema operativo base.

## Límites importantes

- Solo PC BIOS/x86 y hardware emulado especificado; no UEFI ni soporte comprobado en equipos reales.
- Sin paginación, procesos, aislamiento, sistema de archivos persistente, instalador o multitarea.
- Sin IDT/manejadores de excepciones; un fallo inesperado del kernel puede detener el sistema. Interrupciones desactivadas.
- Interfaz gráfica mínima, sin ratón, navegador ni gestor completo de ventanas.
- NotLang es un intérprete pequeño, sin bucles, funciones ni acceso a archivos.
- Red con IP estática para QEMU, una solicitud a la vez, IPv4 y HTTP sin cifrar. Sin DHCP, IPv6, TLS/HTTPS, retransmisión TCP propia ni reensamblado de fragmentos IP. El cliente muestra el estado HTTP y la cantidad de bytes, no renderiza páginas web.
- Pensado para pruebas de laboratorio; no usar para información sensible ni redes hostiles.

## Documentación

- [NotLang](docs/NOTLANG.md)
- [Red](docs/NETWORK.md)
- [Pruebas y resultados](docs/TESTING.md)
- [Hitos y próximos pasos](docs/ROADMAP.md)
