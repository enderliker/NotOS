# HTTPS/TLS — NotOS 0.11

## Uso

```text
https example.com
https example.com:443
https localhost:8443 10.0.2.2
net
tls
tls-selftest
```

`https HOST[:PORT] [IP]` usa 443 por defecto. El hostname DNS se usa para **SNI y verificación del certificado**. La IP opcional solo cambia dónde conectar, no la identidad a verificar; es equivalente a una resolución explícita y permite tests locales. Identidades IP literales/iPAddress SAN no se soportan: usar un nombre DNS y, si hace falta, la IP de conexión. No hay `--insecure` ni fallback a HTTP.

El cliente hace GET `/` con HTTP/1.0 dentro del canal cifrado, muestra estado y bytes de plaintext autenticado, y requiere `close_notify` autenticado para declarar terminación correcta. No renderiza el cuerpo ni implementa un navegador. Una línea de estado puede mostrarse durante streaming antes de detectar un truncamiento posterior; eso no equivale a una respuesta completada.

Ejecutar `make run`: genera una semilla fresca para el invitado. Para añadir disco persistente, usar `python3 tools/run.py -drive if=ide,index=1,format=raw,file=build/data.img`. Reutilizar data.img no requiere reutilizar la semilla.

## Arquitectura y política

MbedTLS 3.6.5 (Apache-2.0) suministra TLS1.2/1.3 y X.509; BearSSL conserva solo HMAC-DRBG/SHA256; NotOS mantiene kernel, Ethernet, IPv4, TCP, retransmisión y event loop propios. **No hay proxy ni TLS del host en el camino del cliente**. El build obtiene fuentes sin modificar, con SHA256 fijado antes de extraer/compilar; no usa libc ni runtime Linux en el invitado. Ver third_party/README.md y su opción offline.

- TLS **1.3 preferido, mínimo 1.2**: ECDHE, RSA-PSS/ECDSA; AES128/256-GCM o ChaCha20-Poly1305 para 1.3. ECDHE_RSA/ECDHE_ECDSA con AES128-GCM/ChaCha20 para 1.2. Sin TLS1.0/1.1, PSK, 0-RTT ni renegociación.
- RSA >=2048 bits exactos (también se rechaza 2047), certificados EC P256/P384/P521; firmas de certificados SHA256/384/512. No MD5/SHA1 chains, CBC ni intercambio RSA estático.
- Cadena contra un snapshot Mozilla/curl comprometido; no actualizaciones silenciosas de raíces.
- Identidad DNS verificada, incluyendo wildcard de una sola etiqueta. SAN DNS requerido: no se acepta fallback CN-only.
- EKU, cuando está presente en hoja o intermedios enviados, debe autorizar serverAuth o anyExtendedKeyUsage. MbedTLS verifica firmas, CA/basic constraints, keyUsage y fechas; el wrapper añade SAN/EKU/límites.
- Hasta ocho certificados de 16 KiB cada uno. DER adicional usa slices limitados; el validador original sigue verificando la estructura/cadena.
- Registros TLS sobre TCP con una transmisión pendiente; reenvío conserva ciphertext/sequence originales. Input/output FIFO de 32 KiB y buffers MbedTLS completos para records de 16 KiB.
- Estado/buffers por conexión en heap, borrados de forma explícita antes de liberar. Sin buffers secretos compartidos con user space.
- Límite de respuesta de 16 MiB, inactividad TLS de 30 s y total de 60 s; timeouts y errores limpian el contexto.

## Entropía y tiempo

`make run` genera 64 bytes nuevos con el CSPRNG del host y los entrega como `opt/notos/entropy` por QEMU fw_cfg. El invitado siembra HMAC-DRBG/SHA256, borra temporales y deriva entropía distinta por intento. No usa PIT jitter, semilla fija, OS RNG implícito ni extensiones CPU. Sin semilla, TLS falla **antes de conectar**; HTTP sigue disponible. La seguridad exige un host confiable y semillas impredecibles/no reutilizadas. Restaurar/clonar snapshots puede repetir DRBG state y no se soporta como flujo seguro. Todavía no hay fuente de entropía para equipos físicos.

CMOS se lee con snapshots coherentes; formatos BCD/binario y 12/24 h, UTC 2000..2099. Clock ausente/inválido falla cerrado. Host/firmware/RTC pertenecen al límite de confianza; no hay tiempo autenticado ni protección contra rollback del host. El snapshot de raíces también requiere mantenimiento explícito.

El cargador lee 384 KiB en doce transferencias; firma final 0x6fffc. BSS reservado a partir de 1 MiB. Límites de payload, readonly/BSS y guard de stack siguen comprobados por linker. CSPRNG y TLS son kernel no preemptivo; procesamiento criptográfico puede demorar el loop.

## Pruebas y límites

`make clean test` genera CA/keys **efímeros en build** y otra imagen (`notos-tls-test.img`) que confía solo en esa CA. **La imagen de producción no confía en ella**; se prueba su rechazo. Keys no se comprometen en git y se excluyen de artifacts de CI. El servidor Python/OpenSSL solo es un peer de test: la negociación cliente/cifrado/verificación ocurren dentro del invitado.

Casos positivos: TLS12 y TLS13, RSA-PSS/ECDSA, HelloRetryRequest, TLS12 AES-GCM/ChaCha20, SNI, SAN/wildcard, múltiples solicitudes, headers divididos, body mayor que un RX ring, cierre autenticado y recuperación de pérdida RX.

Casos negativos: identidad incorrecta/wildcard multietiqueta, raíz no confiable, falta de SAN, EKU incorrecto de hoja/intermedio, RSA débil, certificado futuro/expirado, firma SHA1 de hoja, truncamiento sin close_notify y ciphertext AEAD alterado. Sin entropía y OOM no se conecta. Frames/heap se restauran exactamente tras intentos; ring3/batches/heap siguen operativos. PCAP verifica ausencia de GET/body en claro y ClientHello random distinto por intento. Parser de política DER probado con ASAN/UBSAN, todos los prefijos truncados y 10000 mutaciones.

`make test-online` prueba DNS/HTTP y HTTPS público con roots de producción: example.com, versión negociada 1.2/1.3, HTTP200 y close_notify. Un peer solo TLS13 ya se acepta; uno solo TLS12 conserva compatibilidad.

No revocación OCSP/CRL/CT, client certificates, session resumption, HTTP2, browser, general TCP stack, hardware real ni auditoría de producción. No se implementa todo el ecosistema de políticas PKI. La criptografía portable no garantiza ausencia de canales laterales en equipos físicos. Es un hito comprobado en QEMU, no un sistema para manejar credenciales/información sensible ni una garantía contra todos los ataques.

## Port de plataforma

El backend se compila con cabeceras freestanding propias y configuración limitada. BIO no bloqueante devuelve WANT_READ/WANT_WRITE; TCP conserva sus retransmisiones cifradas. Se liberan cadena/contextos y slots PSA al terminar.
RTC se convierte a epoch UTC de 64 bits y calendario sin libc del anfitrión. Fechas posteriores a 2038 se prueban explícitamente. RNG PSA consume la misma fuente fail-closed en bloques limitados; no existe fallback determinista.
Las funciones de formato de diagnóstico no utilizadas fallan explícitamente; **no constituyen una libc para curl**. El cliente educativo sigue siendo `https`.
