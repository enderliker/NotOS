# Hoja de ruta

## Continuación implementada

- curl upstream HTTP/HTTPS en ring 3 con MbedTLS fijado, CA inmutable y peers
  locales TLS 1.2/1.3. Arranque bajo 512 KiB más 768 KiB altos.
- `SYS_EXEC=37`: reemplazo preparado antes del commit, PID/cwd/FDs conservados
  salvo CLOEXEC. `spawn-elf PATH [ARGUMENTS]` carga desde VFS.
- Directorios reales, cwd privado, mkdir/rmdir/getcwd/readdir; tmpfs y NotFS v2
  escribible. Migración v1 conservando bancos antiguos hasta verificar v2.
- Historial de ocho comandos, edición flechas/Home/End/Delete, completado Tab
  de comandos/rutas; teclado PS/2 y serie CSI fuera de IRQ.
- Pruebas nativas de fallos de publicación/migración, QEMU para reemplazo,
  coherencia/persistencia y edición. Contratos: VFS.md, STORAGE.md, ELF.md,
  CURL_PORT.md, UI.md. TESTING.md registra solo resultados observados.

## Próximos hitos

1. API de directorios POSIX sobre la enumeración wire existente, rename
   transaccional y estrategia de reciclado de inodes persistentes.
2. Presupuesto de CPU configurable con política que limite cadenas exec;
   timers/timeouts y latencia del kernel durante operaciones ATA largas.
3. Parser shell con comillas/escapes y control explícito de trabajos/terminal,
   sin cambiar silenciosamente el contrato actual de argumentos.
4. Capacidades TLS adicionales según aplicaciones: certificados cliente,
   revocación, CApath, actualización explícita de raíces y HTTP/2.
5. Paquetes/aplicaciones ELF pequeñas instalables en el disco dedicado.

Permanecen cuatro procesos, ocho endpoints, 16 FDs, RAM tmpfs de 256 KiB,
NotFS de 60 KiB de payload. No hay SMP/threads, FPU/SSE, UEFI, hardware físico
validado, preemption kernel ni conformidad POSIX completa. No hay TODOs nuevos
que representen funcionalidad parcial dentro de estos hitos.
