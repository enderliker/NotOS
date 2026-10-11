# Libc de usuario

## Qué se ha portado

**Picolibc upstream 1.8.11**, compilada como biblioteca estática freestanding para i486/i386, con un adaptador de sistema propio. No es una colección de sustitutos de `malloc`/`printf` en el kernel ni la libc de Linux. Las fuentes upstream se descargan con SHA256 fijado y no se modifican; licencias y procedencia en `third_party/`.

La aplicación `build/libc-demo.elf` se enlaza por separado con `build/sysroot/lib/notos/libc.a`. El linker elimina secciones no utilizadas; `build/libc-demo.map` muestra qué implementaciones upstream se incorporaron. `build/libc-app.elf` conserva los segmentos ELF pero elimina símbolos/debug antes de embeberse en la imagen. El cargador existente valida y carga ese archivo en **ring 3** con CR3, datos, pila y heap privados. No se enlaza libc dentro del kernel.

## Uso y construcción

```text
libc-test
libc-run test
libc-run exit
ps
```

`libc-test` y `libc-run test` lanzan la misma batería de comprobaciones como proceso background. El modo `exit` prueba salida buffered, callbacks y estado de terminación 23. `libc-run noentropy` es un diagnóstico para imágenes sin semilla: debe comprobar `EIO`, no simular aleatoriedad.

El host necesita Meson, Ninja, GCC con `-m32`, binutils y Python 3.12+, además de los requisitos habituales del proyecto. `make` construye el sysroot y la demo; `make test` incluye `tests/libc.py`. Para uso offline:

```sh
PICOLIBC_ARCHIVE=/ruta/picolibc-1.8.11.tar.xz make
```

El hash se comprueba también al usar un archivo local. No se ejecutan binarios target en el host. El enlace es estático con GNU ld, sin libc/libgcc del host ni semihosting. Las opciones del cross-build y la configuración del compilador forman una firma; cambios de esa firma reconstruyen biblioteca y sysroot. Las bibliotecas de TLS siguen teniendo sus adaptadores freestanding separados.

## Runtime y funciones comprobadas

- `_start` prepara la llamada C alineada; el kernel prepara `argc`, `argv`, un `envp` vacío y el par `AT_NULL`. La API `process_spawn_elf_args` conserva nombre de hasta 23 bytes y un argumento de hasta 64 bytes. `process_spawn_elf_split`, usada por curl, admite 256 bytes de texto separado por espacios y hasta 16 elementos de argv, incluido el nombre; sin comillas ni escapes.
- BSS a cero; ejecución de `.init_array` antes de `main`, `.fini_array` al salir y `atexit`. Retornar de `main` usa `exit`; `stdout`/`stderr` se vacían antes de `_exit`.
- `malloc`, `calloc`, `realloc`, `free`, `strdup`: asignador real de Picolibc sobre `sbrk`/SYS_BRK. Heap privado de hasta 4 MiB; crecimiento y rollback pertenecen al gestor VM. Recolección de todo el espacio al terminar o cancelar el proceso.
- Strings/memoria, `strtol`/`strtoll`, detección de overflow, `qsort`/`bsearch` y `setenv`/`getenv`/`unsetenv`.
- `printf`, `snprintf`, `puts`, `fprintf`, buffering de stdio y escritura de consola. Configuración **entera y long long**, no formatos de punto flotante. `write` divide transferencias en llamadas de hasta 4096 bytes.
- `errno` global de Picolibc es privado **por proceso**, porque cada ELF tiene datos propios. No es thread-local ni se soportan threads. El adaptador traduce errores NotOS a constantes Picolibc: no se supone que ENOSYS/ENOTSUP tengan los mismos números.
- `time_t` de 64 bits, `time`, `gettimeofday`, `clock_gettime`, `clock_getres` y `gmtime_r`. RTC UTC de un segundo; monotónico de 64 bits en milisegundos, actualizado por PIT. El reloj no valida confianza del firmware ni ofrece precisión HPET/APIC.
- `nanosleep` bloquea el proceso mediante SYS_SLEEP sin bloquear la shell; resolución de 1 ms, duración máxima por llamada de 60 s. `sleep` divide esperas más largas. Sin señales/EINTR.
- `getpid`, consola `isatty` y `getentropy` de hasta 256 bytes. Entropía procede del DRBG existente con semilla fresca fw_cfg; **sin semilla falla cerrado**. No es una nueva fuente hardware ni se usan datos predecibles como reemplazo.

El port integra solo funciones seleccionadas y comprobadas. La presencia de una función/header en el archive upstream **no implica que funcione en NotOS**: componentes no seleccionados pueden necesitar otros hooks o soporte de CPU.

## ABI añadida (INT 0x80)

EAX número/resultado; EBX, ECX y EDX argumentos. Se preservan los números anteriores. Los errores kernel son enteros negativos; la libc devuelve `-1` y establece `errno` cuando corresponde.

| Número | Operación | Argumentos | Resultado |
| --- | --- | --- | --- |
| 10 | SYS_FD_WRITE | EBX fd, ECX buffer, EDX bytes | bytes escritos; FDs generales desde 0.15, sockets desde 0.16 |
| 11 | SYS_TIME | EBX reloj 0 realtime/1 monotónico, ECX salida | 0 o error |
| 12 | SYS_ENTROPY | EBX salida, ECX bytes | 0 o error |
| 13 | SYS_PID | ninguno | PID positivo |

SYS_TIME escribe una estructura wire de 12 bytes: segundos low/high de 32 bits y nanosegundos de 32 bits. No expone padding ni tipos nativos del kernel. En este target Picolibc, `timespec`/`timeval` tienen esa misma disposición, protegida por static assertions. El adaptador traduce también identificadores de reloj. El kernel valida todo el rango y permisos antes de leer/escribir un puntero de usuario.

## Runtime adicional para curl

`user/float_runtime.c` implementa helpers binary64 mediante operaciones enteras,
sin FPU/SSE ni libgcc. Incluye conversiones, comparaciones y suma/resta/producto/
división. La prueba diferencial nativa compara un millón de pares con el
hardware del host bajo ASAN/UBSAN; no equivale a una biblioteca libm completa.

`select` adapta conjuntos de descriptores a `poll`, filtra por intereses
solicitados y devuelve el número de bits listos en los conjuntos. Rechaza
nfds fuera de 0..64, timeouts inválidos y buffers inaccesibles; NVAL produce
EBADF. Los conjuntos se limpian al vencer el timeout y permanecen intactos
cuando la llamada falla. No modifica timeval; redondea hacia arriba a ms y
satura en INT_MAX sin overflow. `exceptfds` corresponde a POLLPRI, evento
urgente que la pila actual no implementa. No hay pselect ni señales/EINTR.

`mkdir` devuelve ENOTSUP para cualquier ruta. `/tmp` es un montaje plano y no
se informa creación de subdirectorios. El port HTTP de curl y sus límites están
en [CURL_PORT.md](CURL_PORT.md).

## Límites explícitos

- Desde 0.15 hay FDs por proceso, archivos tmpfs, snapshots NotFS read-only y open/read/write/close/seek/stat/dup/ftruncate/fcntl; `fopen` opera sobre ellos. Ver VFS.md para cuotas y operaciones ausentes; no es VFS POSIX completo.
- Stdin bloquea hasta datos/EOF entregados con feed/feed-eof al PID; NONBLOCK produce EAGAIN y stdio diferencia EOF de error. stdout/stderr comparten la consola y dup2 permite redirección a archivos. Desde 0.17 hay poll; no hay raw TTY/job control.
- Desde 0.16 hay API BSD parcial TCP/IPv4 y getaddrinfo DNS; ver SOCKETS.md. Desde 0.17 hay poll de FDs (POLL.md); con adaptador `select`, sin señales, fork, ejecutables desde archivos, enlazado dinámico ni TLS de threads.
- FPU/MMX/SSE siguen rechazados por el kernel. No se declara `libm`, formato float general ni conformidad C/POSIX completa. Los helpers binary64 descritos arriba tienen cobertura específica. La configuración selecciona stdio entero; el archive upstream contiene componentes adicionales no preparados para este runtime.
- `mmap`/`munmap` básicos del kernel siguen disponibles por ABI, pero no se presenta un wrapper POSIX completo. El asignador comprobado usa `sbrk`.
- Solo QEMU/TCG y hardware emulado del proyecto; no prueba en hardware físico ni auditoría de seguridad.

Curl upstream 8.11.1 HTTP-only ya se ejecuta con esta libc. HTTPS en curl requiere un backend de usuario separado; ver [CURL_PORT.md](CURL_PORT.md).

## Pruebas

`tests/libc.py` arranca la imagen real con 16 y 64 MiB, ejecuta dos procesos secuenciales, salida 23 y cuatro procesos simultáneos; rechaza el quinto slot. Comprueba strings/formato/calendario, 2500 operaciones de heap, alineación, calloc cero, realloc/OOM sin pérdida de datos, errno privado a través de espera, entrada no bloqueante con EAGAIN, buffers inválidos y constructor/atexit/destructor/flush.

Cada terminación debe restaurar exactamente frames físicos y estado del heap kernel. También prueba reloj real en 2050, ausencia de entropía, el mapa de enlace upstream, syscalls legacy con buffers inválidos y rollback ELF/OOM. La división signed/unsigned de 64 bits tiene pruebas nativas de límites y 100000 comparaciones; el reloj verifica carry al envolver los 32 bits bajos. Las regresiones completas y la red se describen en `TESTING.md`.

La batería de libc también comprueba select con intereses distintos, un FD en
varios conjuntos, timeout, EBADF, EFAULT, timeval64 enorme y rechazo de mkdir.
`tests/curl.py` verifica el cliente upstream y archivos binarios desde otro ELF.
