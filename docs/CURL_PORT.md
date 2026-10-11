# curl upstream en NotOS

## Alcance

curl **8.11.1** se compila como ELF32 estático y se ejecuta en ring 3 con
Picolibc 1.8.11, heap y pila privados. El cliente utiliza sockets TCP/IPv4 y
DNS del invitado sobre RTL8139/lwIP, y `poll` para esperar eventos.

Este port habilita **HTTP**. El comando kernel `https` conserva TLS 1.2/1.3;
ese backend no está conectado a curl. Una URL HTTPS en curl devuelve el error
upstream de protocolo no soportado.

## Uso

```text
curl --version
curl -sS http://example.com/
curl -sS -o /tmp/page.txt http://example.com/
curl -sS --max-time 1 http://10.0.2.2:8080/
ps
kill PID
```

La shell informa el PID y continúa atendiendo entrada. La terminación se
reporta como `PROCESS: pid=... exit=... ticks=...`. Los errores de opciones,
HTTP con `-f`, DNS, conexión y timeout conservan los códigos de salida de curl.

Los argumentos se separan solo por espacios; los espacios repetidos se
colapsan. Máximo **256 bytes** después de `curl ` y **16 elementos de argv**,
incluido `argv[0]` (15 argumentos después del nombre). Las comillas y barras inversas son caracteres literales;
no hay expansión, escapes ni sintaxis de shell POSIX. Los excesos se rechazan,
sin ejecutar una entrada truncada. Los demás comandos conservan 159 caracteres.

## Construcción

`tools/fetch_curl.py` descarga el archivo 8.11.1 con SHA256 fijado. Para usar un
archivo local verificado: `CURL_ARCHIVE=/ruta/curl-8.11.1.tar.xz make`.
`tools/build_curl.py` compila libcurl y la herramienta después de extraer las
fuentes. Make agrupa ambos archives en una sola receta, también bajo `-j`.

`user/curl_port/curl_config.h` selecciona HTTP, IPv4 y poll. El enlace incorpora
la libc del sysroot y runtimes propios de enteros/binary64; no utiliza libc del
host, libgcc, semihosting ni instrucciones FPU/MMX/SSE. Stdio sigue configurado
para formatos enteros: no se declara soporte general de libm o formato float.

El ELF stripped se incorpora como bloque de solo lectura/NX en memoria física
alta. El cargador BIOS mantiene los 512 KiB del kernel bajo 1 MiB y copia otros
320 KiB mediante un buffer temporal, sin ocupar VGA. Ver [memoria](MEMORY.md).

El código propio mantiene `-Wall -Wextra -Werror`. Curl upstream se compila con
`-Wall -Wextra`, siguiendo la política separada de dependencias sin modificar
sus fuentes. GCC 15.2 muestra avisos de conversión enum/bool en `transfer.c` y
función sin uso en `url.c`; no se silencian.

## Límites

- Cuatro procesos gestionados, ocho endpoints TCP y 16 FDs por proceso.
- Archivos `/tmp/NAME` planos de hasta 64 KiB; cuota global de 256 KiB.
  `/disk` permanece de solo lectura. Ver [VFS](VFS.md) y [sockets](SOCKETS.md).
- `mkdir` devuelve ENOTSUP; `--create-dirs` no crea directorios.
- Sin HTTPS, HTTP/2, HTTP/3, proxy, cookies, MIME/multipart, FTP ni protocolos
  adicionales. La salida de `--version` enumera solo `http`.
- Sin ejecutables desde VFS, fork, threads, señales ni conformidad POSIX completa.
- La resolución DNS del adaptador tiene su propio límite de espera; no se
  garantiza que `--max-time` interrumpa una resolución en curso.
- La cobertura es de QEMU/TCG; no se declara validación de hardware físico.

## Validación

`make test` incluye `tests/curl.py`: versión/protocolo, argumentos vacíos y sus
límites, HTTP, descarga binaria de 32768 bytes, lectura/verificación/unlink desde
otro ELF y upload POST del mismo contenido. Comprueba HTTP404 con `-f`, protocolo
no soportado, conexión rechazada, timeout, shell/INT3 disponibles, cuatro
procesos y cancelación/slot reutilizado. DNS A/NXDOMAIN utiliza un peer local
con la imagen de fixture, mientras producción conserva el puerto DNS 53.

Después de escenarios sin archivos persistentes en tmpfs, frames, heap,
descripciones, endpoints y consultas vuelven al estado inicial. El mapa de
enlace verifica Picolibc y ausencia de bibliotecas del host. El runtime binary64
se compara con el hardware del host bajo ASAN/UBSAN con un millón de pares.

## Siguiente hito

Integrar un backend TLS en curl userspace con validación de CA, identidad,
fechas y cierre autenticado. Exige peers TLS 1.2/1.3, certificados inválidos,
timeouts, limpieza de recursos y pruebas HTTP/HTTPS públicas. El cliente kernel
no sustituye esa validación.
