# Continuidad: HTTPS, ejecución VFS y QOL

## Implementación

- curl upstream 8.11.1 HTTP/HTTPS en ring 3, MbedTLS 3.6.5 separado del backend
  kernel y enlazado a Picolibc. Raíces production inmutables `/system/cacert.pem`;
  fuentes/versiones/SHA de terceros intactos. Ver CURL_PORT.md y TLS.md.
- Arranque 512 KiB bajos + 768 KiB altos, firmas 0x8fffc/0x1bfffc. Curl/CA R/NX;
  BSS 0x1c0000 hasta <0x280000 reservado físicamente. Imagen de 1.5 MiB.
- SYS_EXEC=37 (36 sigue poll): preparar imagen/espacio/argumentos antes del commit,
  mismo PID/cwd/FDs salvo CLOEXEC. Cambiar CR3 antes de destruir espacio anterior.
  `spawn-elf PATH [ARGS]` carga system/tmpfs/NotFS. Argumentos espacios, 256 bytes,
  16 elementos, envp vacío. Ver ELF.md y PROCESSES.md.
- NotFS v2: 2x64 KiB, 24 nodos, 60 KiB payload; directorios, escritura persistente,
  CRC/layout y publicación con FLUSH/readback. Migrar v1 primero en banco alto,
  conservando sectores 0..65. Inodes persistentes monotónicos. Ver STORAGE.md.
- tmpfs/directorios, cwd privado por proceso y mkdir/rmdir/getcwd/readdir wire.
  Disco enlazado coherente; unlink conserva handles en RAM antes de publicar.
  `mkfs` invalida handles viejos; no toca boot. Ver VFS.md.
- Shell: historial8, flechas/Home/End/Delete, Tab comandos/rutas, serie CSI/SS3 y
  teclado PS/2 fuera de IRQ. Lógica pura line_core y adaptador line_input.

## Límites

Cuatro procesos, ocho endpoints, 16 FDs/tarea, 64 descripciones, tmpfs64KiB y
256KiB RAM globales, NotFS60KiB payload. Presupuesto normal250 ticks CPL3;
curl system2500, quantum5; exec no reinicia contador. Sin POSIX completo,
threads/SMP, FPU/SSE, hardware físico probado, kernel preemptivo ni señales.
TLS estricto, sin bypass con `-k`, sin CApath/certificados cliente/revocación.

## Validación

Repetir `make` y `make test`; para build `make clean test`. Fixtures nuevos:
fs-unit, line-unit, continuation.py, curl_https.py. `make test-online` incluye
curl_online.py; no confundir fixtures locales con Internet ni con CI remota.

`make` y `make test` completos pasaron todas las unidades nativas y las 25 suites
de QEMU, incluyendo TLS1.2/1.3/certificados/truncación en 16/64MiB,
exec/CLOEXEC/cwd/editor y carga ELF persistida por curl tras reboot. Ver
TESTING.md.

GCC muestra avisos upstream curl enum/bool/url.c y Picolibc _set_tls; mantener
política explícita. No silenciar warnings, tocar upstream o desactivar tests.
El ensayo libc individual conserva3MiB; escenario concurrente2MiB por proceso
para caber con kernel ampliado en16MiB, conservando OOM y aislamiento.

No se crearon commits ni se hizo push. No hay TODOs nuevos. Próximos hitos en
ROADMAP.md: dirent/rename/inodes, política presupuesto, parser shell, TLS y
aplicaciones instalables. El trabajo local debe conservarse.
