# FDs, VFS y entrada de procesos — NotOS 0.15

## Alcance comprobado

La libc ahora opera sobre **archivos reales del invitado**, no sobre archivos/sockets del anfitrión. El kernel tiene una tabla de descriptores por tarea, descripciones de archivo abiertas con referencias compartidas y un VFS mínimo. No es un filesystem POSIX completo. Curl HTTP-only utiliza tmpfs para sus salidas; ver CURL_PORT.md.

| Ruta | Backend | Semántica |
| --- | --- | --- |
| `/tmp/NAME` | tmpfs propio en frames físicos | lectura/escritura binaria; vive hasta unlink o reinicio |
| `/disk/NAME` | NotFS existente, disco ATA primary-slave | snapshot de solo lectura por open; nuevos opens ven el snapshot NotFS actual |
| `/dev/stdin` | cola de entrada de ese proceso | bloqueo hasta datos/EOF; O_NONBLOCK devuelve EAGAIN |
| `/dev/stdout`, `/dev/stderr` | consola existente | escritura; sin salida separada por terminal |
| `/dev/console` | consola/cola de ese proceso | acceso según flags de open |
| `/dev/null` | dispositivo nulo | lectura EOF real y escritura descartada |

Solo rutas absolutas y nombres planos de 1..23 caracteres alfanuméricos, punto, guion o underscore. Se rechazan componentes `.`, `..`, subdirectorios y rutas demasiado largas. La ruta wire permite hasta 63 caracteres + terminador. No hay cwd, creación de directorios, symlinks, rename ni montaje configurable. El adaptador libc mkdir devuelve ENOTSUP, incluso bajo /tmp.

NotFS no se formatea automáticamente ni escribe el boot drive. Para `/disk`, debe existir el disco dedicado y haberse montado/formateado mediante los comandos existentes. `open` para escribir y `unlink` en ese montaje fallan EROFS. No se presenta escritura POSIX persistente como implementada. Un handle abierto conserva una copia inmutable incluso si la shell modifica/formatea NotFS; es una política explícita de snapshot, no semántica POSIX de modificación compartida del archivo en disco.

## Funciones de libc

`open`, `read`, `write`, `close`, `lseek`, `stat`, `fstat`, `unlink`, `ftruncate`, `dup`, `dup2`, `isatty` y `fcntl` están conectadas a syscalls NotOS. Picolibc upstream proporciona `fopen`, `fread`, `fwrite`, `fseek`, `fclose`, buffering, EOF y error de stdio sobre esos hooks.

Flags comprobados: acceso read/write/read-write, create/exclusive/truncate, append, nonblock y CLOEXEC. `fcntl` soporta GETFL/SETFL y GETFD/SETFD. CLOEXEC es estado de cada descriptor; **no hay aún exec de binarios desde VFS**, cuya implementación futura deberá aplicar el cierre. No se soportan locks, F_DUPFD ni todas las opciones POSIX: fallan explícitamente, sin asumir números Linux/Picolibc iguales a NotOS.

`dup`/`dup2` comparten posición y flags de la descripción abierta; un `open` separado tiene posición independiente. Los flags FD_CLOEXEC pertenecen al slot, no a esa descripción. `dup2` valida origen y destino antes de cerrar el descriptor reemplazado. Unlink elimina el nombre, pero un inode abierto sigue disponible hasta cerrar su última referencia. Puede crearse otro archivo con el mismo nombre sin alterar el handle antiguo.

Seek más allá de EOF seguido de write deja un hueco a cero; truncate libera páginas y borra el tail retenido, evitando reaparecer datos al crecer otra vez. Append obtiene EOF en cada llamada de escritura: operación kernel no preemptiva, **no atomicidad de un fwrite completo dividido en varias syscalls**. `read` puede ser corto; la libc no vuelve a bloquear después de entregar bytes.

`stat` proporciona tipo, permisos guardados, inode, links, tamaño y bloques de la representación RAM. IDs compatibles con ino_t i386 y sin colisiones entre nodos tmpfs vivos; inode de NotFS basado en su slot. Timestamps y credenciales se rellenan a cero: no hay atime/mtime mantenidos, usuarios/grupos ni control de acceso multiusuario. No se declara conformidad POSIX completa.

## Límites y propiedad de memoria

- 16 FDs por tarea, incluidos 0/1/2 iniciales; hasta 64 descripciones abiertas globales.
- 24 nodos globales, como máximo 16 nombres tmpfs enlazados.
- Archivo tmpfs: 64 KiB; cuota global: 64 páginas de 4 KiB (**256 KiB**), incluyendo snapshots `/disk`.
- Entrada: 256 bytes por proceso. Transferencia por syscall read/write: hasta 4096 bytes.
- NotFS mantiene su límite de 8 archivos y 1536 bytes por archivo; VFS no lo convierte silenciosamente en almacenamiento general.

tmpfs asigna frames físicos bajo demanda, fuera del heap kernel. Antes de crecer se reservan todas las páginas de esa operación; ENOSPC/ENOMEM libera solo las páginas nuevas y deja contenido, tamaño y posición previos. Writes mayores pueden retornar parcialmente si una syscall posterior falla. No hay mmap de archivos, page cache coherente con disco ni journaling nuevo.

Los nombres enlazados conservan sus páginas aunque todos los procesos salgan: no es una fuga, es el contenido del filesystem. Un archivo sin nombre libera sus páginas al cerrar el último open. La destrucción de tareas por exit, fallo, presupuesto o kill cierra todos sus FDs. La creación parcial de una tarea/tabla revierte todas las referencias adquiridas.

## Stdin sin bloquear la shell

```text
libc-run stdin
ps
feed PID hello-vfs
feed-eof PID
```

Sustituir PID por el reportado al arrancar la aplicación. `feed` añade la línea y un newline a la cola de ese proceso; `feed-eof` marca fin permanente del canal, después de drenar lo ya encolado. Una cola llena rechaza la entrega completa. PID inexistente o reutilizado no recibe datos de la tarea anterior.

Una lectura vacía bloqueante conserva el frame como TASK_WAIT_INPUT y vuelve al event loop. `process_poll` entrega datos mediante aliases supervisor del espacio **de esa tarea**, establece el resultado de la syscall y la vuelve READY. No consume presupuesto CPL3 mientras está bloqueada. Los lotes diagnósticos síncronos no pueden mantener esa espera y devuelven ENOTSUP. El timer/SYS_SLEEP conserva su política anterior.

Teclado y serial siguen alimentando la shell: ésta enruta explícitamente líneas al proceso seleccionado. No hay job control, raw TTY, termios, señales, Ctrl-C dirigido a procesos ni control de terminal completo. Poll de FDs está disponible; ver POLL.md. Esta interfaz evita que varios procesos roben teclas a la shell o entre sí.

## ABI incremental

INT 0x80 usa EAX número/resultado y EBX/ECX/EDX argumentos. Se preservan syscalls anteriores; SYS_FD_WRITE=10 ahora despacha el descriptor, no solo la consola.

| Número | Operación | Argumentos |
| --- | --- | --- |
| 14 | OPEN | path, flags wire, mode |
| 15 | READ | fd, buffer, bytes |
| 16 | CLOSE | fd |
| 17 | SEEK | fd, offset signed32, origin |
| 18 | FSTAT | fd, salida wire |
| 19 | STAT | path, salida wire |
| 20 | UNLINK | path |
| 21 | DUP | fd, destino o -1 para primer slot libre |
| 22 | CONTROL | fd, command wire, argument |
| 23 | TRUNCATE | fd, longitud |
| 24 | ISATTY | fd |
| 25 | USER_CHECK | buffer, bytes, writable 0/1 |

`file_abi.h` define valores y `notos_file_info` de 20 bytes. El adaptador convierte a stat nativo después de validar su rango completo; no se escribe una estructura nativa del kernel sobre memoria de usuario. Las rutas se copian byte a byte con permisos y límites comprobados **antes de mutar el filesystem**. Buffers read/write y salidas stat se validan completos, incluidos cruces de página y destinos readonly. No hay ABI Linux ni compatibilidad con binarios arbitrarios.

## Pruebas

```sh
make test
# o, después de make:
python3 tests/vfs.py
```

- Backend nativo bajo ASAN/UBSAN: posición compartida/independiente, unlink abierto, holes/truncate, cuotas y OOM en mitad de reserva con rollback exacto, FD lleno sin truncar/crear archivos, 70000 ciclos de inode/wrap con un inode huérfano abierto, snapshots y colas privadas.
- ELF ring3 en QEMU 16/64 MiB: stdio binario de más de dos páginas, append, EOF/error, redirección stdout con dup2, stat, límites de FDs y rutas/buffers inválidos.
- Cuatro procesos con tablas aisladas; stdin bloqueante y EOF sin colgar shell/INT3; cola llena, cancelación, slot reutilizado, fallo #UD y recolección de inodes huérfanos.
- Disco real emulado: open antiguo conserva snapshot, nuevo open ve actualización, reboot conserva NotFS, boot image intacta.
- Tras cada escenario limpio, frames/heap/descripciones/nodos vuelven al estado exacto inicial.

No se ha probado hardware físico ni CI remota desde este entorno. Las cuotas hacen que este VFS sirva para salidas pequeñas; un bundle completo de CAs o descargas grandes necesitará ampliar el almacenamiento o un backend distinto en futuros hitos.

## Descriptores de socket en 0.16

FILE_SOCKET comparte referencias/flags NONBLOCK mediante dup igual que otras descripciones. El último close libera el endpoint; stat informa S_IFSOCK y seek/truncate se rechazan. Exit/kill/fault también cancela registros DNS del propietario. Ver SOCKETS.md para cuotas, bloqueo y límites; poll se describe en POLL.md; no añade archivos de disco escribibles.
