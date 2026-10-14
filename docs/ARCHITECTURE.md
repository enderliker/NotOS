# Arquitectura y convenciones

NotOS es un kernel educativo freestanding. Las capas de ejecución se mantienen separadas:

| Módulo | Responsabilidad | No debe hacer |
| --- | --- | --- |
| `arch.*`, `user_entry.S`, `isr.S` | GDT/TSS, ABI de CPU, entrada/retorno e interrupciones | interpretar comandos |
| `physical*`, `paging*`, `space*`, `heap*` | memoria y espacios de direcciones | decidir política de planificación |
| `task.*` | recursos, estado, estadísticas y terminación de tareas | imprimir resultados de demos |
| `syscall.*` | ABI, validación y despacho de syscalls | crear comandos de shell |
| `user.*` | demos, resultados esperados y diagnóstico de shell | manejar directamente interrupciones |
| `syscall_numbers.h` | constantes ABI compartidas entre C y assembly | contener tipos C que rompan el assembler |

`scheduler_policy.*` contiene round-robin puro y probado nativamente. `scheduler.*` mantiene el lote activo, quantum, trazas y cambios de contexto; `multitask.*` crea y verifica las demos sin participar en IRQ.

Cada tarea tiene un CR3 propio y una pila kernel de 16 KiB con canario. IRQ0 entrega EOI antes de elegir el siguiente contexto; el dispatcher devuelve el frame a restaurar por assembly. Al cambiar se actualizan CR3 y ESP0 de la TSS. El kernel conserva su frame de entrada y recupera su CR3 al terminar el lote.

El quantum es de 5 ticks del PIT; el presupuesto máximo es de 250 ticks entregados a CPL3 por tarea, no de tiempo de pared ni tiempo de CPU exacto. Hasta cuatro tareas por lote; no hay SMP ni preemption del kernel. CR0.TS bloquea FPU/MMX/SSE: no se declara preservación de ese estado. Shell y red no se procesan mientras el lote ocupa la llamada síncrona.

## Módulos añadidos en 0.9

- `process.*`: propiedad de slots/PID y recolección; `scheduler_pulse` recupera el kernel después de cada quantum/yield/terminación para mantener el event loop vivo.
- `disk.*` / `fs.*`: transporte ATA primary-slave separado de snapshots/CRC/CRUD; nunca escribir el boot drive.
- `mouse_decode.*` / `mouse.*` / `gui.*`: parser puro, cola IRQ y event loop, rendering y acciones diferidas, respectivamente.
- `notlang_compile.*` / `notlang_command.*` / `user/notlang_vm.c`: compilación acotada, carga y VM ejecutada en un espacio ring3; protocolo compartido `notlang_bytecode.h`.
- `tcp_retry.*`: política pura de ACK/backoff separada del driver/protocolo; la pila sigue teniendo una solicitud activa y una transmisión pendiente a la vez.

## TLS en 0.10

`tls.*` maneja engine/application y limpieza; `tls_profile.*` define crypto/versiones; `x509_policy.*` delega validación y añade límites/SAN/EKU; `certificate_policy.*` es el parser puro probado con sanitizadores. `entropy.*`, `rtc.*`, `utc.*` encapsulan plataforma. `net.*` solo mueve bytes/registros por TCP. No se emiten datos de aplicación antes de validar el handshake ni se degrada a plaintext.

## Reglas

- Headers con guards y APIs explícitas; datos internos `static` cuando corresponda.
- No asignar memoria, imprimir diagnósticos extensos ni interpretar programas dentro de handlers IRQ.
- Validar rangos completos antes de leer buffers user; preservar el estado de IF en secciones críticas.
- Destruir recursos solo cuando ya no están activos; reportar resultados antes de liberar el espacio que los contiene.
- Errores de programa y fallos del kernel son categorías diferentes.
- Constantes ABI nombradas, compartidas por C/assembly y protegidas por dependencias de Make.
- Los tests de kernel arrancan la imagen real; los algoritmos puros se prueban también nativamente.
- Cambios funcionales en commits `feature(alcance) descripción`, después de compilar con `-Wall -Wextra -Werror` y pasar las pruebas aplicables.

La documentación de límites es parte del contrato. Un prototipo que pasa estos tests no equivale a un sistema de producción ni a una auditoría completa de seguridad.

## TLS y memoria en 0.11

PAE y NX reemplazan tablas de 32 bits: PDPT privado y directorio por tarea; región estática alta reservada explícitamente antes de asignar frames; BSS actual desde 0x1c0000. Texto RX, rodata R/NX, datos/pilas RW/NX.
`tls_profile.*` ahora configura MbedTLS 1.2/1.3; `tls.*` proporciona BIO no bloqueante, verifica el handshake y libera contextos/slots PSA.
`certificate_policy.*` sigue aplicando SAN/EKU/límites sin borrar errores de MbedTLS. `x509_policy.*` es legado BearSSL, no se enlaza en el cliente actual.
`tls_platform_port.*` adapta heap/RTC/DRBG; no es una libc POSIX. `integer_runtime.*` aporta división u64 probada para i386.

## ELF32 y VM de usuario

`elf.*` valida sin asignar; `space.*` carga segmentos y administra frames/PTE privados, brk y anónimos.
`task_create_elf` utiliza el entry/stack del espacio; `process_spawn_elf` reutiliza slots y recolección existentes.
`elf_command.*` es integración/diagnóstico, no código del parser. Las demos C se compilan y enlazan separadas del kernel.
La validación de buffers recorre PDEs de 2 MiB; la propiedad se recupera caminando tablas, no con un array grande por tarea.

## Bloqueo temporizado

`wait_policy.h` contiene la comparación relativa pura (sin I/O ni asignación). SYS_SLEEP valida y registra duración; scheduler_block conserva el frame y devuelve el kernel para procesos gestionados.
`process_poll` despierta vencidos antes del round-robin. Ningún handler IRQ asigna o libera procesos; kill/recolección siguen perteneciendo al event loop.
Los lotes síncronos no registran una espera que no puedan despertar: devuelven un error definido.

## Libc de usuario en 0.14

`user/libc/crt0.S` y `start.c` son el CRT de aplicaciones; `port.c` traduce ABI/errno y encapsula los hooks OS de Picolibc. `demo.c` es la batería ejecutada en ring3, no una implementación alternativa de libc. `tools/fetch_picolibc.py` fija/verifica fuente; `build_picolibc.py` genera el cross-build Meson y sysroot. `libc_command.*` solo lanza el ELF; no contiene asignador ni stdio.

`platform_abi.h` fija límites y layout wire del tiempo. Las syscalls validan rangos/permisos antes de tocar buffers; el runtime de usuario no puede desactivar esa comprobación. `clock_uptime_ms` toma una instantánea atómica de 64 bits con IF preservado. `integer_runtime.*` proporciona división/remainder signed y unsigned al kernel y al ELF por enlaces separados, sin libgcc host.

Los hooks no soportados no devuelven éxito ficticio. libc privada single-thread no significa threads ni conformidad POSIX completa. VFS y sockets tienen los adaptadores descritos abajo. Ver LIBC.md.

## FDs, VFS y entrada en 0.15

`vfs.*` posee nodos/frames, nombres tmpfs y NotFS v2 escribible e imágenes system inmutables; `fd.*` posee descripciones abiertas, referencias, posición/flags y colas de entrada. `task.*` adquiere/libera la tabla de descriptores. Ni VFS ni FDs interpretan punteros de usuario o deciden planificación.

`syscall.*` valida/copia rutas y buffers antes de llamar al backend. `file_abi.h` fija el wire independiente de Picolibc. `user/libc/port.c` traduce constantes/errores/stat y usa los hooks reales de stdio upstream. `process_poll` termina lecturas TASK_WAIT_INPUT mediante `space_copy_to_user` sobre aliases del espacio bloqueado, sin activar su CR3. Shell/IRQ no comparten datos de entrada entre PIDs.

El backend tiene tests ASAN/UBSAN con un asignador de frames falso host-only; `tests/vfs.py` comprueba el ELF real y recuperación exacta en QEMU. La operación no preemptiva del kernel garantiza consistencia de referencias en un CPU; no se declara soporte SMP ni sincronización multi-CPU.

## TCP y DNS de usuario en 0.16

`socket_stack.*` encapsula lwIP raw NO_SYS; `lwip_port/` aporta configuración/límites, reloj y RNG. `net.*` comparte RTL8139/DMA y demultiplexa puertos con el transporte legacy. `fd.*` integra el endpoint en referencias OFD/dup; no decide bloqueo. `syscall.*` registra espera/punteros validados; `process_poll` copia desde/hacia el espacio bloqueado por aliases físicos y reanuda el frame.

`user/include` define cabeceras propias y `user/libc/socket.c` traduce API/errno/native structs al wire estable `socket_abi.h`. `user/socket_demo.c` es diagnóstico ELF separado, no implementación de TCP. Fixtures de tests usan sockets del host solo como peers; no se incorporan al invitado. `tools/fetch_lwip.py` fija SHA/licencia y `build_lwip.py` cruza upstream sin headers/libc de host. Lifecycle raw, quotas y pendientes están documentados en SOCKETS.md.

## poll en 0.17

poll_abi.h fija vector/eventos; fd_poll y socket_events.h calculan readiness sin consumo ni política de espera. socket_ready solo adapta estado/capacidad de lwIP a esa política. poll_wait.* valida/copia snapshots y conserva/devolver el frame mediante scheduler_block. process_poll orquesta wakeup; ni IRQ ni FD backend planifican procesos. El adaptador user/libc/poll.c traduce structs/errno; poll_demo.c es un módulo de pruebas del ELF, no una implementación alternativa. Ver POLL.md.

## curl HTTP y runtime de argumentos

`argument_core.*` separa texto acotado sin I/O ni asignación. `space.*` copia
strings y construye el stack ABI; `process.*` comparte creación y rollback
para argumentos simples o separados. `curl_command.*` solo lanza el ELF
upstream. `select` es un adaptador de libc sobre poll, no una syscall nueva.

El cargador BIOS añade un bloque alto de 768 KiB usando un buffer temporal y
INT 15h/AH=87. El linker coloca el blob curl R/NX en 0x100000 y BSS desde
0x1c0000; physical reserva toda la región estática antes de asignar frames.
La ejecución sigue ocurriendo en el espacio privado de usuario. Ver MEMORY.md,
LIBC.md y CURL_PORT.md para contratos y límites.

## Comandos de archivos

`path_core.*` normaliza rutas kernel sin I/O ni asignación; tiene pruebas
nativas con sanitizadores. `file_command.*` conserva cwd de la shell y adapta
los comandos a tmpfs y NotFS, con resolución de rutas y cwd por proceso en la ABI incremental. `vfs_entry`
expone copias de nombres enlazados para listado, sin transferir referencias.
El lanzador conserva `data.img` fuera de los productos de construcción.

## Continuación: TLS, exec y filesystem

`system_file.*` expone blobs inmutables, `executable.*` adquiere/libera imágenes
VFS y el lanzador. `syscall.*` copia rutas/argumentos y prepara espacio; `task.*`
confirma cambio CR3/frame y aplica CLOEXEC con `fd.*` antes de destruir espacio.
`fs_core.*` contiene CRC/validación/compactación sin I/O; `fs.*` publica dos bancos
ATA y migra v1 con rollback. `vfs.*` conserva referencias y handles sin nombre.
`line_core.*` edita sin plataforma; `line_input.*` interpreta CSI/completa/renderiza
fuera de IRQ. El backend MbedTLS de curl se enlaza a Picolibc por separado del
TLS kernel, con fuentes/versiones de terceros intactas.
