# Procesos en segundo plano

```text
spawn worker
spawn spin
spawn fault
ps
kill PID
```

`process.*` posee una tabla de cuatro slots y PIDs monotónicos no reutilizados (el agotamiento de 32 bits rechaza nuevas creaciones). El estado persiste entre iteraciones de la shell, **no entre reinicios**. La creación copia código/datos al espacio privado ring 3.

El planificador puede reanudar un contexto durante un pulso: al vencer el quantum de cinco muestras IRQ0 user, al yield o al terminar, recupera el frame kernel del event loop. El gestor rota slots READY y reanuda una tarea por iteración; red, serie, teclado y ratón pueden procesarse entre pulsos. Una syscall sigue siendo no preemptiva. Comandos kernel bloqueantes pueden demorar los procesos; no es un kernel preemptivo.

`kill` opera desde el loop, nunca destruye un contexto ejecutándose. Salida normal, fallo y presupuesto se reportan y recolectan automáticamente: frames y pila kernel se liberan fuera del handler. No hay zombies, pero tampoco almacenamiento persistente de resultados de procesos ya recolectados, espera/join, IPC, prioridades ni PID concurrente por múltiples CPUs.

Las demos batch de fase 5 siguen siendo síncronas. Los procesos background esperan mientras se ejecuta un batch o comando kernel bloqueante. `process_spawn_image` es la interfaz usada por el cargador de la VM NotLang; no acepta imágenes mayores de una página.

Pruebas reales: cuatro procesos, límite de slots, shell viva, kill, auto-reaping, fallo de uno y progreso de otro, restauración exacta de heap/frames y convivencia con batches. HTTP por NAT se prueba en la misma VM mientras cuatro spins background están activos. QEMU únicamente.

## Esperas bloqueantes de procesos gestionados — NotOS 0.13

`elf-run sleep` ejecuta un ELF que bloquea 600 ms y comprueba el tiempo monotónico al reanudarse. No hace busy wait: SYS_SLEEP guarda el frame, cambia a TASK_BLOCKED y devuelve el kernel al event loop. La política round-robin excluye bloqueados; el gestor despierta tareas vencidas antes de elegir un READY.

INT 0x80, EAX=9 / SYS_SLEEP, EBX=0..60000 ms. Cero equivale a yield; longitud inválida devuelve -EINVAL. Al despertar se vuelve al syscall con EAX=0 y registros/pila conservados. El contador de presupuesto solo cuenta IRQ0 entregadas en CPL3; el tiempo bloqueado no se consume como CPU user.

`ps` muestra PROCESS WAIT para estos estados. `kill` destruye también procesos bloqueados; reutilizar un slot empieza con timestamps limpios y no hereda un despertar antiguo. Los deadlines relativos usan resta unsigned, probada al envolver el reloj de 32 bits.

**Alcance:** bloqueos por tiempo para procesos persistentes ejecutados mediante scheduler_pulse. Los lotes síncronos de diagnóstico no tienen event loop para despertar y devuelven -EOPNOTSUPP para esperas positivas; se prueba ese rechazo. Desde 0.16 hay espera de sockets (ver abajo); sigue sin poll/select de FDs ni señales/EINTR ni garantías de latencia mientras el kernel procesa un comando largo. No se declara completado el planificador POSIX de curl.

Los retornos administrados aplican la misma sanitización de EFLAGS que los cambios de contexto de lotes (NT/IOPL/VM limpiados, IF activado). La demo de espera intenta conservar NT y comprueba su rechazo al reanudarse; las pruebas de registros/DF anteriores se conservan.

## Entrada de procesos en 0.15

TASK_WAIT_INPUT conserva una syscall read hasta datos o EOF en la cola de esa tarea; no usa el deadline de SYS_SLEEP. `feed PID TEXT` y `feed-eof PID` enrutan explícitamente la entrada sin robar teclas a shell u otros procesos. Kill cierra los FDs, libera inodes huérfanos y reinicia cola/espera antes de reutilizar el slot. Ver VFS.md para límites y política NONBLOCK. No es poll/select ni job control.

## Esperas de socket en 0.16

TASK_WAIT_CONNECT/RECEIVE/SEND guardan fd, buffer, longitud/flags y deadline. process_poll atiende backend y copia buffers mediante aliases del CR3 privado; EAGAIN/EINPROGRESS siguen bloqueados hasta evento o timeout. Kill/exit limpia endpoints/consultas mediante la tabla FD. No hay poll/select, señales ni threads; ver SOCKETS.md.
