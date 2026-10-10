# Sockets de usuario — NotOS 0.16

## Alcance

Cliente **TCP/IPv4 real** en ring3 mediante Picolibc y syscalls propias. lwIP upstream 2.2.1 aporta TCP, UDP interno para DNS, ARP e ICMP; el driver RTL8139 y el kernel de NotOS siguen siendo propios. No usa Linux como base, sockets del host, semihosting ni un proxy TLS. Las bibliotecas externas se identifican y conservan sus licencias.

`user/libc/socket.c` traduce la API BSD a `socket_abi.h`. Cabeceras públicas en `user/include/`; las constantes no son una ABI Linux. `kernel/socket_stack.c` encapsula el API raw de lwIP (`NO_SYS=1`); `fd.c` posee referencias de descripciones abiertas; `syscall.c` valida punteros y decide bloqueo; `process_poll` completa operaciones mediante aliases físicos del espacio bloqueado. No se entra a lwIP desde IRQ ni hay preemption del kernel/SMP.

## Operaciones disponibles

- `socket(AF_INET, SOCK_STREAM, 0/IPPROTO_TCP)`, flags `SOCK_NONBLOCK` y `SOCK_CLOEXEC`.
- `connect`, `send`, `recv`, `shutdown`, `getsockname`, `getpeername`, `close`.
- `read`/`write`, `dup`/`dup2` y `fcntl` sobre FDs de socket. `fstat` devuelve S_IFSOCK.
- `MSG_DONTWAIT`, `MSG_PEEK`; `MSG_NOSIGNAL` en send, sin señales/SIGPIPE en este runtime.
- `SO_ERROR` (consumo una vez), `SO_TYPE`, `SO_RCVTIMEO`, `SO_SNDTIMEO`, `TCP_NODELAY`. SO_RCVBUF/SO_SNDBUF son consultas de capacidades fijas, no ajuste de buffers.
- `inet_pton`/`inet_ntop`, conversiones de orden de bytes y `getaddrinfo`/`freeaddrinfo`/`gai_strerror`: IPv4, TCP, servicios numéricos. Resolución DNS A, NXDOMAIN y timeout; AI_PASSIVE/NUMERICHOST/NUMERICSERV. No AI_CANONNAME ni búsqueda de servicios del sistema.

connect/send/recv bloqueantes suspenden el **proceso**, no la shell. El frame queda guardado y el event loop atiende NIC/timers y otros procesos. NONBLOCK produce EINPROGRESS/EAGAIN; MSG_DONTWAIT afecta solo esa llamada. Connect bloqueante tiene timeout predeterminado de 10 s; los timeouts de envío/recepción valen 0 (sin límite) o 1..60000 ms. Una espera sin datos vence con EAGAIN. `getaddrinfo` usa consultas asíncronas y SYS_SLEEP de 10 ms mientras espera, no un busy-loop kernel. No hay señales/EINTR.

## Recursos y ciclo de vida

- 8 endpoints globales y 8 TCP PCBs; 4 procesos gestionados, 16 FDs/proceso y 64 descripciones compartidas (límites existentes).
- RX/TX 8192 bytes, MSS 1460, 96 segmentos TCP, heap estático lwIP de 64 KiB, pool de 32 pbufs de 1600 bytes. I/O syscall hasta 4096 bytes; send/recv pueden devolver transferencias parciales.
- 4 consultas DNS simultáneas, máximo una por propietario; nombres de hasta 95 bytes, deadline de aplicación 8 s. Tokens monotónicos de 31 bits no reutilizados; kill/exit cancela el registro propietario. Un callback tardío no escribe en un slot de proceso reutilizado. La consulta/caché raw de lwIP puede persistir de forma acotada hasta resolver o vencer; no se declara cancelación física inmediata del paquete.
- Quotas/OOM fallan explícitamente. Antes de `tcp_new`, si el pool está lleno se reclama solo un TIME_WAIT sin propietario; de lo contrario ENOMEM. No se expulsa una conexión activa para abrir otra.
- FIN remoto conserva datos RX pendientes y EOF; no impide enviar hasta SHUT_WR. Lecturas PEEK no consumen bytes ni ventana. SHUT_RD descarta datos y sigue reconociendo segmentos; no usa TF_RXCLOSED, que suprime notificaciones de liberación raw.
- Cuando FIN y cierre local dejan un PCB reciclable se desconectan callbacks y se conserva la identidad de direcciones/NODELAY en el endpoint. Un FD abierto no conserva un puntero a un PCB TIME_WAIT liberado. El último close libera el endpoint; dup conserva la conexión hasta la última referencia.
- Los tests exigen restauración exacta de frames físicos/heap kernel, tmpfs/descripciones/endpoints/consultas de aplicación. TIME_WAIT, ARP y caché DNS son estado protocolario **acotado**, no necesariamente cero inmediatamente tras close.

## Coexistencia con HTTPS

La pila educativa anterior se conserva para comandos kernel `dns/http/https` (una solicitud activa). Sus puertos TCP son 49153..51999; lwIP usa 52000..60999 y DNS puertos aleatorios 61000..61999. RX enruta por protocolo/puerto; ARP se entrega a ambas pilas. `net_frame_send` copia el frame de cada productor a DMA. La configuración /24 solo cambia si ambas pilas están libres, incluidos PCBs pendientes/TIME_WAIT.

La entropía del DRBG existente inicializa ISNs y puertos DNS; sin NIC o sin semilla el API falla cerrado. El TLS kernel 1.2/1.3 no cambia ni se incorpora automáticamente a sockets de usuario.

## Diagnóstico y pruebas

```text
sockets
socket-run echo PORT
socket-run http PORT
socket-run dns PORT
socket-run web 80
socket-drop syn
socket-drop tx
socket-drop rx
ps
kill PID
```

`socket-run` lanza un ELF independiente contra el host QEMU 10.0.2.2; echo espera el fixture binario de `tests/socket_fixture.py`. `http` espera `/socket` con 32768 bytes del patrón del fixture y verifica escritura a `/tmp/http-PID`. `web` hace DNS y HTTP público example.com. No es un curl alternativo ni una CLI HTTP general.

`make test` incluye `tests/sockets.py`: 16/64 MiB, flujo binario, transferencia parcial, HTTP->tmpfs, cuatro procesos, NONBLOCK, rechazo/RST, timeout, cancelación, cuotas/dup, SYN/TX/RX perdidos y más de ocho cierres/reaperturas manteniendo un FD antiguo. También fuerza ocho PCBs ocupados con siete FDs: la novena apertura falla ENOMEM sin expulsar conexiones activas. Un servidor DNS UDP local en **53530** comprueba A/NXDOMAIN/cancelación/callback tardío con `build/notos-socket-test.img`; solo esa imagen enlaza lwIP con el puerto de fixture. La imagen de producción siempre usa DNS 53, sin requerir root para CI. `make test-online` incluye DNS/HTTP reales desde ring3.

## Pendiente

**No hay poll/select, sockets UDP públicos, bind/listen/accept, IPv6, DHCP, reensamblado IP, threads, fork, ejecutables VFS ni curl upstream.** El siguiente gate es E/S multiplexada y validación de los hooks que demande curl. El backend TLS de curl deberá portarse y probarse en userspace; el comando kernel HTTPS no satisface ese gate. Solo QEMU/TCG y hardware emulado especificado están comprobados, no equipos reales ni una auditoría de seguridad.

## Wire de syscalls (INT 0x80)

EAX número/resultado, EBX/ECX/EDX primeros tres argumentos; **ESI cuarto argumento** de send/recv. El kernel devuelve errores negativos; libc los traduce a errno Picolibc. Todas las entradas/salidas se validan antes de tocar memoria o consumir estado.

| Número | Operación | EBX / ECX / EDX / ESI |
| --- | --- | --- |
| 26 | SOCKET | AF_INET / STREAM / flags NotOS / — |
| 27 | CONNECT | fd / dirección wire / — / — |
| 28 | SEND | fd / buffer / longitud / flags |
| 29 | RECV | fd / buffer / longitud / flags |
| 30 | GETOPT | fd / opción wire / salida u32 / — |
| 31 | SETOPT | fd / opción wire / valor u32 / — |
| 32 | SHUTDOWN | fd / how 0..2 / — / — |
| 33 | ADDRESS | fd / salida wire / peer 0..1 / — |
| 34 | RESOLVE | nombre terminado / — / — / — |
| 35 | RESOLVE_RESULT | token / salida IPv4 4 bytes / — / — |

Dirección wire: IPv4 de cuatro bytes seguido de puerto u32 en orden nativo, tamaño total 8; no es `sockaddr_in` ni expone punteros/kernel padding. Los adaptadores traducen timeval64 a ms y SO_ERROR a errno nativo. Los números 0..25 conservan su ABI; read/write/close/dup/control existentes se aplican también a los descriptores nuevos.
