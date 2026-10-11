# Continuidad: curl HTTP en NotOS

## Estado

El avance integra curl upstream 8.11.1 HTTP-only como ELF32 ring 3, con
argumentos separados por espacios y pruebas de transferencia real. El objetivo
del handoff anterior queda implementado; HTTPS en curl sigue pendiente.

La referencia de uso, construcción y límites es [docs/CURL_PORT.md](docs/CURL_PORT.md).
Las reglas de contribución están en [AGENTS.md](AGENTS.md).

## Cambios que deben conservarse

- `tools/build_curl.py` construye libcurl y la herramienta tras extraer las
  fuentes fijadas. Una receta agrupada evita la carrera de archives en `make -j`.
- `argument_core.*` valida y separa hasta 256 bytes/16 argumentos, sin truncar.
  `space.*` construye argc/argv/envp/AT_NULL y alinea ESP a 16 bytes;
  `process_spawn_elf_split` reutiliza creación y rollback existentes.
- `curl_command.*` lanza el binario upstream; reporta fallos y, con `process-trace on`, PID.
- El loader copia 320 KiB adicionales desde un buffer BIOS temporal a
  0x100000..0x150000. La imagen curl es R/NX y BSS comienza en 0x150000;
  el asignador reserva toda la región estática. Se mantienen kernel bajo,
  guard 0x90000 y pila inicial 0x9f000.
- `select` filtra eventos por conjuntos solicitados, devuelve EBADF para NVAL,
  valida buffers y evita overflow al convertir timeouts de 64 bits.
- `mkdir` devuelve ENOTSUP: el VFS no tiene directorios ni éxito simulado.
- Las pruebas binary64 se ejecutan en `make test`, incluyendo suma/resta y
  operandos subnormales, junto con los tests de argumentos y curl en QEMU.

## Validación que debe repetirse al cambiar código

```sh
make
make test
```

Para cambios de construcción: `make clean test`. Para pruebas públicas de los
clientes existentes: `make test-online`. No afirmar CI remota sin comprobarla;
no publicar cambios salvo petición explícita.

Logs relevantes: `build/curl-16M.log`, `build/curl-64M.log`,
`build/curl-refused-*.log`, `build/curl-dns.log`, `build/libc-*.log`.
`build/curl.map` debe incorporar Picolibc del sysroot y no host libc/libgcc.

GCC 15.2 emite avisos upstream de enum/bool y función sin uso en curl; su
política conserva Wall/Wextra sin modificar la dependencia. El código propio
mantiene Werror. Picolibc tiene su política upstream separada documentada.

## Fases publicadas

- 77f80c5: build y sincronización QMP; CI success.
- 8886cc0: contratos libc y binary64; CI success.
- 714e2b7: integración HTTP de curl y carga alta; CI success.

Los enlaces de ejecución están en docs/TESTING.md. Cada fase pasó make y
make test localmente antes del commit/push. No se ejecutó test-online.

## Siguiente tarea

Backend TLS en userspace para curl, raíces y validación de certificados.
Consultar [docs/TLS.md](docs/TLS.md), [docs/SOCKETS.md](docs/SOCKETS.md) y
[docs/ROADMAP.md](docs/ROADMAP.md) antes de diseñarlo. Mantener versiones/SHA de
terceros salvo autorización expresa.

Límites a respetar: cuatro procesos, ocho endpoints, VFS plano, archivos tmpfs
hasta 64 KiB y 256 KiB globales. No usar `mkdir` falso ni presentar el cliente
kernel HTTPS como curl. No hay TODOs añadidos al código en este avance.
