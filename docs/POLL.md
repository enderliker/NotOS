# E/S multiplexada — NotOS 0.17

## Implementación

`poll(struct pollfd *, nfds_t, int timeout)` está disponible en la libc de usuario. Espera eventos de varios descriptores mediante una syscall **propia**, no un loop de `nanosleep`, una syscall Linux ni sockets del host. Un proceso sin eventos queda TASK_BLOCKED y devuelve el control al event loop; shell/NIC/IRQ y otros procesos siguen funcionando.

- `kernel/poll_abi.h`: números de eventos y wire fijo; 64 entradas máximo, independientemente de los 16 FDs por proceso. Permite duplicados y entradas negativas.
- `fd_poll`: backend sobre vectores confiables. Consulta readiness sin leer/escribir datos ni modificar posición, flags o SO_ERROR.
- `socket_events.h`: política pura, separada de PCBs y comprobada nativamente. `socket_ready` toma una instantánea del endpoint y capacidad TX de lwIP, sin conservar un PCB reciclable.
- `poll_wait.*`: validación, snapshot de entrada y registro/completado de espera. No interpreta comandos ni implementa TCP.
- `process_poll`: evalúa esperas antes del round-robin. Resultados se copian al espacio bloqueado por aliases físicos sin activar su CR3. Solo event loop/kernel no preemptivo; no se entra desde IRQ.
- `user/libc/poll.c`: traduce structs, límites y errno; `user/poll_demo.c` son pruebas en un ELF separado, no otro backend de poll.

## Contrato

```c
struct pollfd { int fd; short events; short revents; };
int poll(struct pollfd *fds, nfds_t count, int timeout);
```

Retorna el **número de entradas** con revents distinto de cero, no cantidad de bits ni de FDs únicos. Un descriptor repetido cuenta una vez por entrada. Retorna 0 al vencer el timeout; -1/errno en errores de argumentos/buffers o espera no soportada.

- `timeout == 0`: consulta inmediata, nunca bloquea.
- `timeout > 0`: espera relativa hasta INT_MAX ms; resolución PIT/event loop. La comparación unsigned funciona al envolver el reloj de 32 bits. No ofrece garantías duras de latencia cuando hay comandos kernel largos.
- `timeout < 0`: espera indefinida hasta evento o cancelación del proceso, incluidos valores distintos de -1.
- `count == 0`: espera temporal o indefinida sin FDs; puntero ignorado. Con timeout 0 retorna 0.
- Más de 64 entradas: EINVAL antes de leer el vector. Se valida **todo el rango writable/readable** antes de tocar salida. Buffers kernel, readonly, desbordados o no mapeados: EFAULT. El vector raw puede estar desalineado; se copia bytewise antes de interpretarlo.
- `fd < 0`: se ignora y revents queda cero. FD no abierto/fuera de tabla: POLLNVAL en esa entrada, **no EBADF global**.
- POLLIN/POLLOUT y POLLRDHUP se filtran por events. POLLERR/POLLHUP/POLLNVAL se devuelven incluso con events=0. revents de entrada se ignora y sobrescribe al completar.
- Bits de events no soportados se ignoran. POLLPRI está definido como constante convencional, pero **no se implementan datos TCP urgentes/eventos prioritarios**. No se declara conformidad POSIX completa.

La syscall captura fd/events en almacenamiento acotado del task, no retiene el vector como datos confiables en memoria user. El pointer de salida se conserva solo para completar la copia validada. Al despertar limpia el snapshot; kill/fault/destrucción del task descartan toda la espera, sin asignaciones/FDs/PCBs adicionales ni callbacks que despierten slots reutilizados. Si evento y timeout coinciden en una iteración, se entrega readiness primero. Los lotes diagnósticos síncronos permiten consultas inmediatas, pero rechazan esperas que necesitarían el event loop.

## Readiness por backend

| Backend | Eventos |
| --- | --- |
| tmpfs / snapshot NotFS / null | IN y OUT cuando se solicitan, incluso en EOF; operaciones sobre acceso incorrecto pueden fallar sin bloquear |
| stdout / stderr | OUT |
| stdin | IN al tener datos o EOF; HUP para EOF permanente de la cola por PID |
| console | OUT e IN según cola de entrada; HUP al EOF permanente |
| TCP conectando | Ninguno hasta terminar connect o fallar |
| TCP conectado | IN si hay bytes, EOF o SHUT_RD; OUT si hay capacidad de envío/cola o SHUT_WR (write fallaría sin bloquear) |
| TCP FIN remoto | IN; RDHUP si se solicita. HUP cuando ambos lados están cerrados para I/O |
| TCP fallido | IN/OUT solicitados y HUP; ERR mientras SO_ERROR esté pendiente |
| TCP abierto sin connect | HUP y OUT solicitado, no NVAL |

ERR no consume SO_ERROR: consultar poll repetidamente conserva el error; getsockopt(SO_ERROR) lo consume una vez, mientras HUP y el estado fallido siguen disponibles. RX pendiente no se drena; PEEK/read conservan su contrato. TX readiness es una instantánea de capacidad, no reserva de heap/segmentos ni garantía de escritura completa: el caller debe tolerar transferencias parciales/EAGAIN, especialmente al ampliar el modelo a concurrencia futura.

## Comandos y pruebas

```text
socket-run poll-basic 1
socket-run poll-timer 1
socket-run poll-stdin 1
feed PID texto
feed-eof PID
socket-run poll-wait 1
ps
kill PID
socket-run poll-web 80
```

`poll-basic` comprueba límites/rangos, stdin/stdout/null/tmpfs, duplicados/negativos/NVAL, vector raw desalineado y timeout. `poll-wait` espera indefinidamente sin FDs hasta kill. Los modos poll/poll-reset/poll-refused/poll-write necesitan los peers controlados de tests; no son una CLI de red general. `poll-web` usa DNS y HTTP público example.com con sockets no bloqueantes y poll, **no es curl**.

`tests/poll_unit.c` usa ASAN/UBSAN sobre la orquestación real y política pura: snapshot contra cambios de memoria, rango inválido sin efectos, count, duplicados, consulta cero, timeout relativo/wrap, espera indefinida/cancelación, copia fallida y prioridad de eventos al deadline. Los mocks son host-only. `tests/vfs_unit.c` comprueba readiness del backend FD real, sin consumir entrada ni posición de archivos.

`tests/poll.py` arranca la imagen real en 16/64 MiB: dos conexiones pendientes, datos demorados por dos peers en una espera de múltiples FDs, duplicados/PEEK, FIN/RDHUP/HUP, refusal/RST, SO_ERROR no consumido, TX que llega a EAGAIN y vuelve a OUT, stdin/EOF, worker/INT3/shell durante espera, cuatro procesos bloqueados con despertar de uno solo, kill, fallo #UD y slot reutilizado. Recursos de aplicación, frames y heap deben restaurarse exactamente. `tests/poll_online.py` añade HTTP200 público. El fixture RST cierra directamente con SO_LINGER(0), evitando que socketserver envíe un FIN previo por su cierre automático.

## Alcance pendiente

No hay select/ppoll/pselect, señales/EINTR, threads/SMP/kernel preemptivo, sockets server ni UDP público. La multiplexación necesaria para un cliente curl configurado con poll tiene una base comprobada, **pero curl upstream aún no está portado**. El siguiente gate es completar los hooks/configuración que demande su compilación y ejecutar el binario HTTP-only. TLS userspace para curl sigue siendo un gate independiente; el TLS kernel 1.2/1.3 se conserva. Solo QEMU y hardware emulado especificado, no equipos reales ni auditoría de seguridad.
