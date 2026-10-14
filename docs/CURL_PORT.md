# curl upstream en NotOS

## Alcance

curl **8.11.1** se compila como ELF32 estático y se ejecuta en ring 3 con
Picolibc 1.8.11, heap y pila privados. El cliente utiliza sockets TCP/IPv4 y
DNS del invitado sobre RTL8139/lwIP, y `poll` para esperar eventos.

El port habilita HTTP y HTTPS con MbedTLS 3.6.5, construido por separado para
Picolibc en ring 3. La confianza por defecto es `/system/cacert.pem`, el bundle
fijado en trust/roots.pem; `--cacert PATH` selecciona otro archivo del invitado.
No se modifica el bundle automáticamente. TLS 1.2/1.3 verifica cadena, fechas,
SAN, EKU serverAuth, SHA256/384/512 y RSA de al menos 2048 bits. Entropía proviene
de SYS_ENTROPY; RTC y semilla ausentes fallan cerrado.

## Uso

```text
curl --version
curl -sS https://example.com/
curl -sS -o /tmp/page.txt http://example.com/
curl -sS --max-time 1 http://10.0.2.2:8080/
ps
kill PID
```

La shell continúa atendiendo entrada mientras curl se ejecuta en segundo
plano. Por defecto solo se muestra la salida de curl, sin PID ni estadísticas
de terminación. `process-trace on` habilita estos diagnósticos;
`process-trace off` los deshabilita. `ps` permite consultar procesos activos.
Los errores de opciones,
HTTP con `-f`, DNS, conexión y timeout conservan los códigos de salida de curl.

El ELF se ejecuta en el invitado NotOS y usa sus syscalls, libc y pila TCP/IP;
no llama al curl, libc ni sockets del host. QEMU proporciona la NIC virtual.
Con el backend NAT de QEMU, el acceso externo depende de la conectividad y de
la configuración de red del sistema anfitrión.

Los argumentos se separan solo por espacios; los espacios repetidos se
colapsan. Máximo **256 bytes** después de `curl ` y **16 elementos de argv**,
incluido `argv[0]` (15 argumentos después del nombre). Las comillas y barras inversas son caracteres literales;
no hay expansión, escapes ni sintaxis de shell POSIX. Los excesos se rechazan,
sin ejecutar una entrada truncada. Los comandos comunes conservan 159 caracteres; spawn-elf también admite una línea extendida.

`ls /tmp`, `stat /tmp/page.txt`, `cat /tmp/page.txt` y `rm /tmp/page.txt`
operan sobre las descargas. `cd /tmp` permite usar nombres relativos en los
comandos de archivos de la shell; curl comienza con cwd `/` y admite rutas relativas a ese cwd.
Los archivos tmpfs viven hasta unlink o reinicio. Ver [STORAGE.md](STORAGE.md)
para navegar y escribir en el disco persistente mediante la shell.

## Construcción

`tools/fetch_curl.py` descarga el archivo 8.11.1 con SHA256 fijado. Para usar un
archivo local verificado: `CURL_ARCHIVE=/ruta/curl-8.11.1.tar.xz make`.
`tools/build_curl.py` compila libcurl y la herramienta después de extraer las
fuentes. Make agrupa ambos archives en una sola receta, también bajo `-j`.

`user/curl_port/curl_config.h` selecciona HTTP/HTTPS, IPv4 y poll. El enlace incorpora
la libc del sysroot y runtimes propios de enteros/binary64; no utiliza libc del
host, libgcc, semihosting ni instrucciones FPU/MMX/SSE. Stdio sigue configurado
para formatos enteros: no se declara soporte general de libm o formato float.

El ELF stripped se incorpora como bloque de solo lectura/NX en memoria física
alta. El cargador BIOS mantiene los 512 KiB del kernel bajo 1 MiB y copia otros
768 KiB mediante un buffer temporal, sin ocupar VGA. Ver [memoria](MEMORY.md).

El código propio mantiene `-Wall -Wextra -Werror`. Curl upstream se compila con
`-Wall -Wextra`, siguiendo la política separada de dependencias sin modificar
sus fuentes. GCC 15.2 muestra avisos de conversión enum/bool en `transfer.c` y
función sin uso en `url.c`; no se silencian.

## Límites

- Cuatro procesos gestionados, ocho endpoints TCP y 16 FDs por proceso.
- tmpfs hasta 64 KiB/256 KiB globales; NotFS hasta 60 KiB de payload total.
  Directorios reales y `--create-dirs` disponibles; ver VFS.md.
- Sin HTTP/2, HTTP/3, proxy, cookies, MIME/multipart, FTP ni otros protocolos.
- Verificación estricta: `-k` no permite saltar validaciones. Sin certificados
  cliente, CApath ni revocación OCSP/CRL. Parseo CA limitado a 256 KiB y cadena
  a ocho certificados de 16 KiB; sin actualización automática de confianza.
- EOF TLS sin close_notify devuelve error si la transferencia necesita EOF.
  Una respuesta con Content-Length completo puede terminar antes de leer el
  cierre; no se presenta ese éxito como prueba de close_notify.
- Sin fork, threads, señales ni conformidad POSIX completa.
- La resolución DNS del adaptador tiene su propio límite de espera; no se
  garantiza que `--max-time` interrumpa una resolución en curso.
- La cobertura es de QEMU/TCG; no se declara validación de hardware físico.

## Validación

`make test` incluye `tests/curl.py`: versión/protocolo, argumentos vacíos y sus
límites, HTTP, descarga binaria de 32768 bytes, lectura/verificación/unlink desde
otro ELF y upload POST del mismo contenido. Comprueba HTTP404 con `-f`, conexiones HTTP/HTTPS
rechazadas, timeout, shell/INT3 disponibles, cuatro
procesos y cancelación/slot reutilizado. DNS A/NXDOMAIN utiliza un peer local
con la imagen de fixture, mientras producción conserva el puerto DNS 53.

Después de escenarios sin archivos persistentes en tmpfs, frames, heap,
descripciones, endpoints y consultas vuelven al estado inicial. El mapa de
enlace verifica Picolibc y ausencia de bibliotecas del host. El runtime binary64
se compara con el hardware del host bajo ASAN/UBSAN con un millón de pares.

`tests/curl_https.py` prueba TLS 1.2/1.3 con una CA de fixture descargada
explícitamente a tmpfs, rechazo por confianza/SAN/EKU/clave/firma, EOF truncado y
limpieza en 16/64 MiB. Producción no incluye la CA de fixture. La validación
opcional online se describe en TESTING.md.
