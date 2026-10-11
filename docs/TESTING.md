# Verificación y evidencia

## Ejecutar las pruebas

| Comando | Cobertura |
| --- | --- |
| `make` | Imagen de producción y dependencias fijadas |
| `make test` | Unidades nativas y suites sobre imágenes reales de QEMU |
| `make clean test` | Reconstrucción desde cero y la misma suite |
| `make test-online` | DNS/HTTP/HTTPS públicos de los clientes existentes |

Las unidades de parsers, memoria, VFS, poll y runtimes pertinentes usan
ASAN/UBSAN. Los peers HTTP, TCP, DNS y TLS son fixtures del host; las aplicaciones
y el protocolo cliente se ejecutan dentro del invitado. Los logs y capturas se
guardan en `build/`, excluido de git. Las claves TLS efímeras también quedan
fuera de los artifacts de CI.

Para curl HTTP: `tests/argument_unit.c`, `tests/float_unit.c`, las comprobaciones
select/mkdir del ELF de libc y `tests/curl.py`. Esta última requiere la imagen de
producción y `build/notos-socket-test.img`; `make test` construye ambas.
Ver [CURL_PORT.md](CURL_PORT.md) para cobertura y límites.

## Sincronización del monitor QEMU

El arnés espera hasta que QMP acepte conexiones, dentro del plazo de arranque
de diez segundos. La presencia del archivo del socket no garantiza que esté
listo: se reintentan ENOENT/ECONNREFUSED. QEMU terminado o el plazo agotado
siguen fallando; no se omiten pruebas ni se amplían sus timeouts.

## Lectura del registro histórico

Las secciones siguientes describen pruebas de cada hito en su fecha; sus
versiones de QEMU, tamaños y funciones pendientes no son el estado actual.
Los contratos vigentes están en los documentos de módulo y en
[ROADMAP.md](ROADMAP.md). Un PASS local no acredita CI remota ni hardware físico.

## Hito de arranque

Probado localmente con GCC 11, GNU binutils y QEMU 9.2.4 (emulación TCG, 32 MiB RAM).

`make clean test` — PASS:

- Sector BIOS de 512 bytes con firma `55 aa`.
- Lectura de kernel desde la imagen cruda.
- Entrada real a modo protegido y ejecución del kernel C.
- Recepción por COM1 de `BOOT: protected mode OK` y `BOOT: kernel C OK`.

El workflow de GitHub reproduce las pruebas, pero su ejecución remota no se ha verificado aquí.

## Hito gráfico

`make clean test` — PASS en QEMU 9.2.4:

- Arranque y salida serie conservados.
- Captura real VGA: fondo, marco y barra de título comprobados por píxeles.
- Modo lógico 320×200; QEMU puede mostrarlo escalado a 640×400.
- Comandos por COM1 y escritura `echo kbd` mediante teclas PS/2 emuladas.
- Cambio de pantalla tras escribir y limpieza con F2.

Las capturas quedan en `build/desktop*.ppm`. El driver de pruebas utiliza QMP para sincronizar las acciones del emulador.

## Hito del lenguaje

`make clean test` — PASS:

- Pruebas nativas del parser, límites enteros, 16 variables y errores.
- Programas ejecutados en el kernel emulado: precedencia, variables y persistencia.
- División por cero, desbordamiento, variable inexistente, paréntesis incompletos y profundidad excesiva.
- La consola sigue funcionando después de los errores; reinicio de variables comprobado.

La prueba de profundidad detectó instrucciones SSE emitidas por el compilador del host sin inicialización de SSE en el kernel. Se corrigió seleccionando explícitamente `-march=i386 -mno-sse -mno-sse2 -mno-mmx -msoft-float` y se repitió toda la suite antes del commit. `QEMU_TRACE=1 make test` permite obtener el log de excepciones del emulador.

## Hito de red y suite final

`make clean test` — PASS en QEMU 9.2.4:

- Todos los hitos anteriores siguen pasando.
- Checksum, parsing de IPv4 y límites de lectura DNS comprobados nativamente.
- RTL8139 real emulada, DMA, ARP, IPv4 y TCP hasta un servidor controlado en el host por NAT.
- Tres conexiones sucesivas; respuesta HTTP de 7686 bytes, con la línea de estado partida en dos envíos.
- Consumo de datos a través del límite del anillo RX y cierre de conexiones.
- Rechazo de hostname y puerto inválidos.
- Entrada de shell excesiva descartada sin ejecutar su cola.

El test de conexiones sucesivas detectó reutilización prematura del puerto TCP. Se corrigió asignando un puerto efímero nuevo para cada conexión y se repitió la suite antes del commit.

`make test-online` — PASS: consulta DNS pública y conexión HTTP real a `example.com`. Resultado observado (las direcciones y longitudes pueden cambiar):

```text
NET: TCP connect 172.66.147.243:80
HTTP: HTTP/1.1 200 OK
HTTP: received 891 bytes
```

Los logs completos quedan en `build/smoke.log`, `build/network.log` y `build/online.log`. No se ha probado HTTPS ni hardware real. El kernel de la versión 0.4 ocupaba 11416 bytes de código/datos en una imagen de disco de 1 MiB.

## Fase 2, avance de IDT/excepciones

`make clean test` — PASS en QEMU 9.2.4, conservando los tests de gráficos, lenguaje y red.

- IDT de 256 puertas de interrupción, selector de código 0x08 y frames uniformes.
- INT3 repetido con retorno IRET y verificación de todos los registros generales, CF y DF. El handler C recibe DF limpio.
- Fallos de CPU reales (no `int N` simulados): división por cero #DE (vector 0), UD2 #UD (vector 6) y carga de selector inexistente #GP (vector 13).
- Error #GP esperado `0x00000020`, EIP/CS/registros impresos y pantalla de diagnóstico.
- CPU detenida con HLT, QEMU vivo y comandos posteriores sin ejecutarse; no hay reinicio por triple fault.

Capturas: `build/panic-0.ppm`, `build/panic-6.ppm`, `build/panic-13.ppm`. Logs: `build/idt.log`, `build/panic-*.log`. Las excepciones no provocadas por estos tests no se declaran comprobadas.

## Fase 2, IRQ/PIT/teclado y validación final

`make clean test` y `make test-online` — PASS en QEMU 9.2.4.

- IRQ0 reales y reloj monotónico con IF activo.
- Espera HLT repetida y trabajo en primer plano de 250/1000 ms sin depender del sondeo del PIT.
- INT3 conserva también IF con las interrupciones activas.
- Prueba de las rutas espurias IRQ7/15 mediante entradas software con ISR del PIC vacío; no se afirma haber provocado IRQ espurias físicas. El timer continúa funcionando después.
- Orden `echo queued` escrita por PS/2 durante `busy 1000`: la IRQ la encola y la shell la ejecuta al terminar. Contador IRQ1 aumenta; sin descartes en este test.
- Shift y signos comprobados con `echo A!`.
- Duraciones inválidas y demasiado grandes rechazadas.
- Aritmética de fracciones del reloj durante 1000000 ticks y rollover de 32 bits comprobados nativamente con la misma función utilizada por IRQ0. No es una prueba real de semanas de funcionamiento.
- Gráficos, errores de NotLang, excepciones fatales y tres conexiones HTTP por NAT siguen pasando.
- Consulta DNS externa y `HTTP/1.1 200 OK` de `example.com` con 891 bytes recibidos, con IRQ activas.

El kernel 0.5 ocupa 19258 bytes de código/datos; conserva el límite comprobado de 32 KiB del cargador. Los nuevos resultados están en `build/interrupts.log`; las capturas y logs de excepciones se mantienen. CI remota y hardware real no se han verificado aquí.

## Fase 3, mapa BIOS y memoria física

`make clean test` — PASS en QEMU 9.2.4:

- BIOS E820 real y sector BIOS de 512 bytes.
- Segunda lectura del kernel comprobada mediante firma en 0x1fffc. Imagen rellenada hasta 64 KiB; límite del cargador ampliado y validado por linker/generador.
- Arranque con 16, 32, 64 y 128 MiB; cap físico de 64 MiB comprobado.
- Agotamiento del pool, escritura en cada página adquirida, liberación y restauración exacta del contador de frames.
- Pruebas nativas: alineación, reservas solapadas, overflow de rangos E820, OOM, reutilización y rechazo de liberaciones inválidas/dobles.
- Gráficos, NotLang, excepciones, IRQ, teclado y HTTP por NAT siguen pasando.

La prueba real de arranque detectó que E820 modifica DL; se restauró la unidad BIOS antes de INT 13h y se repitió la suite antes del commit. Los resultados nuevos quedan en `build/physical-*.log`.

## Fase 3, paginación y protección

`make test` y `make test-online` — PASS en QEMU 9.2.4:

- CR0.PG/WP leídos del estado real de CPU. Código/rodata de solo lectura.
- Aliases virtuales altos, traducción de offset, remapping y eliminación con invalidación TLB. Tres ejecuciones restauran los frames físicos.
- #PF reales: null/error 0/CR2 0; write-protect/error 3/CR2 0x10000; guard/error 0/CR2 0xd0000000; unmap/error 0/CR2 0xd03ff000.
- Panic muestra el diagnóstico y la CPU llega a HLT sin triple fault.
- Las cuatro configuraciones de RAM, gráficos, NotLang, IRQ, teclado y HTTP local siguen pasando con la MMU activa.
- DNS externo y HTTP 200 OK/891 bytes siguen pasando con paginación activa.

Capturas `build/pagefault-*.ppm` y logs `build/pagefault-*.log`, `build/paging.log`. Se separaron segmentos ELF de código/datos y se alineó la región de solo lectura. El objetivo de instrucciones pasó a i486 por WP/INVLPG; no se declara soporte probado para hardware real.

## Fase 3, heap y validación final de NotOS 0.6

`make clean test` y `make test-online` — PASS en QEMU 9.2.4.

- Heap real de 1 MiB respaldado por 256 frames; guards antes y después.
- Pruebas nativas: split/coalesce, alineación, calloc/overflow, OOM, free inválido/doble, validación de metadata y 20000 operaciones con verificación de patrones.
- Tres autopruebas del heap en el kernel; ocho asignaciones simultáneas mediante la shell y liberación fuera de orden. Se restaura exactamente el espacio libre y quedan cero asignaciones.
- Frame-test y VM-test ejecutados con el heap ya presente; no dañan el heap ni pierden frames.
- Escritura sobre el guard final genera #PF con error 2 y CR2 0xd0101000.
- Todas las pruebas anteriores siguen pasando: las cuatro configuraciones de RAM, gráficos, lenguaje, excepciones, IRQ y HTTP por NAT.
- DNS/HTTP externos pasan con PG/WP/IRQ/heap activos: respuesta 200 OK y 891 bytes recibidos.

Logs `build/heap.log` y `build/heap-guard.log`; captura `build/heap-guard.ppm`. El payload del build observado termina en 0x17006, antes de la firma 0x1fffc; la imagen del kernel está rellenada a 65536 bytes. BSS termina en 0x49028, antes del guard de pila 0x80000. El linker comprueba ambos límites. Estos tamaños pueden cambiar con el compilador. No se han verificado CI remota ni hardware real.

## Fase 4, GDT/TSS y entrada a CPL 3

`make test` — PASS en QEMU 9.2.4.

- TR real contiene selector 0x28.
- Tres ejecuciones de programa user real: CPL=3, SS=0x23, stack supervisor de la TSS comprobado, salida 42 y IRQ0 durante la ejecución.
- INT 0x80 y syscall desconocida (-38), DF conservado para usuario y limpio al entrar a C.
- Retorno a la shell, restauración de frames, INT3, VM y heap tras las ejecuciones.
- Todas las regresiones anteriores pasan. La nueva GDT hace válido 0x20; el test #GP fue actualizado a selector inválido 0x30/error 0x30.

Log `build/user-ring3.log`. No se afirma todavía aislamiento por CR3 ni recuperación de fallos user en este avance.

## Fase 4, aislamiento y validación final de NotOS 0.7

`make clean test` y `make test-online` — PASS en QEMU 9.2.4.

- Dos espacios privados creados a la vez; CR3 real diferente y datos privados en la misma dirección virtual. Se ejecutan secuencialmente y se comprueba que A no escribe B.
- Tres repeticiones de la demo normal restauran kernel CR3 y frames físicos.
- Fallos reales de usuario recuperados: leer kernel (#PF/error 5), escribir código (#PF/error 7), usar puertos (#GP/error 0), escribir guard (#PF/error 6), invocar INT 0x20 (#GP/error 258) y leer el alias supervisor de datos ajenos (#PF/error 5). La shell y el timer siguen funcionando.
- SYS_write acepta un buffer de código user y rechaza kernel, guard, span cruzado, overflow y longitud excesiva sin dereferenciarlos. Pruebas nativas de bounds y permisos de ambos niveles PDE/PTE.
- Un bucle infinito se termina por IRQ0 con estado 124; timer y HLT siguen funcionando después (EOI correcto).
- Heap/VM/frame-test y contador de frames intactos después de todas las demos. Pruebas adicionales de roots privados/fault recovery con 16 y 64 MiB.
- Gráficos, NotLang, memoria, fallos kernel, IRQ y teclado siguen pasando. HTTP por NAT se comprueba en el mismo invitado después de ring3-test y de un fallo user I/O.
- DNS e internet HTTP 200 OK/891 bytes pasan después de la demo de buffers inválidos en el mismo invitado.

Logs `build/user-isolation.log`, `build/user-16M.log`, `build/user-64M.log`. CI remota y hardware real no se han verificado aquí. El kernel conserva el límite de payload de 64 KiB y el límite de BSS anterior al guard 0x80000, comprobados por el linker.

## Fase 5, preparación de la arquitectura

`make clean test` — PASS. Se separó el monolito de demos user en task (ciclo de vida), syscall (ABI/validación) y user (diagnóstico). Las constantes de syscall se comparten entre C y assembly.

El comportamiento previo sigue intacto: roots privados, fallos recuperables, presupuesto, buffers validados, memoria, gráficos, NotLang, IRQ y HTTP local. Todavía no se declara planificación concurrente en este avance.

## Fase 5, round-robin preemptivo

`make test` — PASS. Política pura: rotación de cuatro slots, fairness, wrap, exclusión de terminadas y entradas inválidas. Imagen real: tres lotes de dos workers sin SYS_YIELD, cambios IRQ0 en ambos sentidos, CR3/pilas distintos, registros/DF/canarios privados y salida 42 de ambos. Frames y heap se restauran exactamente; INT3, espera por IRQ y heap funcionan después. Todas las regresiones previas, incluyendo HTTP local, pasan.

Log `build/multitask.log`. La ejecución sigue siendo un lote síncrono, no un gestor de procesos en segundo plano.

## Fase 5, validación final de NotOS 0.8

`make clean test` y `make test-online` — PASS en QEMU 9.2.4. Compilación con `-Wall -Wextra -Werror`.

- Workers sin yield: dos y cuatro tareas cambian por IRQ0, con roots/pilas distintos, registros/DF/canarios conservados y salida 42.
- SYS_YIELD real: cambios voluntarios en ambos sentidos y retorno 0 en el programa user.
- #PF de una tarea: vector 14/error 5/CR2 0x10000; las otras dos terminan normalmente. Una tarea terminada no vuelve a seleccionarse.
- Bucle infinito concurrente: el worker normal termina, el bucle se detiene exactamente al llegar a 250 muestras IRQ0 user, con estado 124.
- x87 no soportado: FLDZ causa #NM/vector 7; esa tarea sale con 135 y otro worker sale con 42. No se comparte estado FPU inadvertidamente.
- Rechazo de lotes vacíos/sobredimensionados, punteros o IDs duplicados y tareas no READY antes de ejecutar.
- RAM 16/32/64 MiB: todos los escenarios, repetición adicional en 32 MiB, frames y heap exactamente restaurados. Heap insuficiente para ninguna pila y para un lote parcial: rollback sin pérdidas y siguiente lote de cuatro tareas válido.
- INT3, timer/HLT y heap siguen funcionando después de los lotes. Todas las regresiones previas de arranque, gráficos, NotLang, excepciones, memoria e aislamiento pasan.
- Tres respuestas HTTP locales en la misma VM, incluyendo después de un #PF concurrente y de agotar presupuesto. DNS/HTTP externos después de cuatro workers y una tarea x87 rechazada: HTTP 200 OK, 891 bytes.

Logs `build/multitask-16M.log`, `build/multitask-32M.log`, `build/multitask-64M.log`, `build/network.log`, `build/online.log`. Payload observado hasta 0x19006, antes de firma 0x1fffc; BSS hasta 0x4f1f0, antes del guard 0x80000. El linker comprueba los límites; estos tamaños pueden variar por compilador.

No se han comprobado CI remota ni hardware real. Los budgets son muestras CPL3, no una garantía estricta de consumo CPU/tiempo de pared; no hay preemption del kernel. Shell/red esperan el fin del lote y FPU/SIMD no están soportados. Ver [contratos y límites](TASKS.md).

## Almacenamiento ATA / NotFS

`make test` — PASS en QEMU 9.2.4. Nuevo test storage: disco ausente, formato explícito del primary-slave, CRUD, reinicio, recuperación del snapshot anterior al corromper el último, límite de ocho archivos y formato persistente. Imagen de arranque sin modificar. Todas las regresiones previas pasan. Logs `build/storage-*.log`; no se han probado fallos eléctricos ni discos físicos.

## Procesos background

Tests `process.py` y `multitask.py` — PASS: cuatro slots ocupados a la vez, PID monotónico, shell responde, kill con estado 143, fallo user no afecta otro proceso, salida normal y presupuesto, recolección automática y restauración de frames/heap. Las regresiones batch siguen pasando. Se añadió integración HTTP con cuatro spins activos; ver resultado de regresión del commit. Los procesos no sobreviven al reinicio.

`make test` del avance background — PASS en QEMU 9.2.4, incluyendo tres respuestas HTTP por NAT mientras se ejecutan spins background; todas las regresiones previas y persistencia pasan.

## Ratón y UI

`make test` — PASS en QEMU 9.2.4. Decodificador PS/2 nativo; test QMP de IRQ12/movimiento/clicks, toolbar CLR/FILES/TASKS, minimizar/restaurar framebuffer, teclado operativo después. Todas las regresiones de IRQ, memoria, procesos, disco y red pasan. Logs `build/mouse.log` y capturas `build/mouse-*.ppm`. No se ha probado ratón físico ni USB.

## NotLang 0.2 y archivos de programas

`make test` — PASS en QEMU 9.2.4. Compilador nativo acotado, VM de 1264 bytes enlazada en 0x08000000; loops/anidamiento y aritmética ejecutados en ring3, overflow/división por cero terminan con 65, errores de compile no crean proceso. Fuente guardada en NotFS, reinicio y exec con salida 42. Frames/heap restaurados; todas las regresiones anteriores pasan. Logs `build/notlang.log`, `build/notlang-reboot.log`. La VM no usa el intérprete kernel; no es un cargador ELF ni incluye funciones.

## NotOS 0.9, red y validación final conjunta

`make clean test` y `make test-online` — PASS en QEMU 9.2.4, compilación `-Wall -Wextra -Werror`, sin warnings observados.

- Política pura de retry: ACK parcial/fuera del límite, wrap de secuencia/timer, backoff y límite de intentos. Configuración IPv4 válida/ inválida probada nativamente.
- Siete respuestas HTTP por NAT: tres normales con spins background y cuatro después de perder SYN, GET, SYN-ACK o primer payload de respuesta. Todas HTTP 200, cantidad exacta de bytes y sin duplicar solicitudes entregadas al servidor. Configuración alternativa 10.0.2.16 funciona y vuelve a default. Retransmisiones propias SYN/GET verificadas en logs.
- DNS externo y HTTP 200 OK/891 bytes pasan con todos los avances juntos. **No se probó HTTPS: no está implementado.**
- Todas las regresiones: gráficos/teclado/ratón, excepciones, IRQ, E820 16/32/64/128 MiB, paginación, heap, CPL3 y aislamiento, seis escenarios de multitarea con 16/32/64 MiB, procesos background, persistencia/reboot/CRC y NotLang ring3 desde archivo.
- Imagen kernel rellenada a 65536 bytes; payload observado hasta 0x1c020 y BSS hasta 0x58414, debajo de sus límites comprobados por el linker. Tamaños pueden variar con el compilador.

Logs `build/network.log`, `build/online.log`, `build/process.log`, `build/storage-*.log`, `build/mouse.log`, `build/notlang*.log` y las suites anteriores. No se han verificado CI remota ni hardware real. Los módulos nuevos son propios; no se añadió kernel/runtime Linux ni proxy host para aparentar capacidades del invitado.

## Preparación TLS: cargador de 192 KiB

`make clean test` — PASS en QEMU 9.2.4. Cargador BIOS ampliado a seis lecturas de 32 KiB, sin cruzar límites de 64 KiB por transferencia; firma en 0x3fffc. BSS permanece debajo del guard 0x80000, comprobado por linker. Todas las suites, incluidas persistencia y siete respuestas HTTP con pérdidas, pasan. Este avance aún no habilita TLS.

## Preparación TLS: criptografía y plataforma

`make test` — PASS en QEMU 9.2.4. SHA256/abc known answer, perfil TLS12-only, UTC/Gregorian epoch y fechas inválidas, semilla fw_cfg presente/ausente, autoprueba después de ring3/CR0.TS y heap restaurado. Todas las regresiones previas pasan. BearSSL portable sin OS RNG/time ni SIMD; roots obtenidas del snapshot comprometido. Todavía no se afirma transporte HTTPS en este avance. Logs `build/tls-platform-*.log`.

## HTTPS/TLS, validación final de NotOS 0.10

`make clean test` y `make test-online` — PASS en QEMU 9.2.4, `-Wall -Wextra -Werror`, sin warnings observados.

- Cliente TLS dentro del invitado con BearSSL portable; TLS1.2 ECDHE/RSA/ECDSA y AEAD. Public roots y X509 verificados, no proxy del host.
- Peers controlados: AES128-GCM y ChaCha20-Poly1305, SNI, DNS SAN y wildcard de una etiqueta. Múltiples solicitudes y headers divididos/body mayor que un RX ring: cantidad exacta de bytes y close_notify autenticado.
- Rechazados antes de enviar GET: identidad incorrecta, wildcard multietiqueta, raíz no confiable, CN-only/SAN incorrecto, EKU clientAuth de hoja o intermedio, RSA1024/RSA2047, fechas futuras/expiradas y peer TLS13-only.
- Truncamiento sin close_notify y ciphertext alterado rechazados; corrupción AEAD no entrega status HTTP. Pérdida RX recuperada por TCP.
- Semilla ausente y OOM fallan antes de conectar. Heap/frames restaurados exactamente después de intentos; batches/ring3/heap siguen operativos. PCAP sin GET/body en claro y random ClientHello distinto por intento.
- Parser extra DER: ASAN/UBSAN, todos los prefijos truncados y 10000 mutaciones. SHA256 known answer, UTC epoch y perfil TLS12-only pasan; crypto funciona después de ring3/CR0.TS sin SIMD/FPU.
- Imagen TLS de pruebas confía solo en una CA efímera generada bajo build. Imagen de producción rechaza esa CA. Sus keys no se comprometen y se excluyen de artifacts CI.
- Internet real: DNS/HTTP y HTTPS example.com, TLS1.2/chain/DNS/time verificados, HTTP200, 891 bytes, cierre autenticado y heap limpio.
- Todas las regresiones previas de loader, UI/teclado/ratón, IRQ/excepciones, RAM/memoria/heap, aislamiento, multitarea/procesos, persistencia y NotLang pasan.

Logs `build/https-local.log`, `build/https-date-*.log`, `build/https-untrusted.log`, `build/https-no-entropy.log`, `build/https-online.log`; captura `build/https.pcap`. El kernel usa la imagen de 192 KiB/firma0x3fffc; los límites de payload/BSS/guard siguen comprobados por linker. Tamaños varían por toolchain.

No se han comprobado CI remota ni hardware real. TLS requiere host/firmware/RTC confiables y semilla fresca; no TLS1.3, revocación ni auditoría PKI/producción completa. Ver [política, uso y límites](TLS.md).

## NotOS 0.11: PAE/NX y TLS1.2/1.3

`make clean test` y `make test-online` PASS en QEMU 9.2.4. Este avance sustituye el backend TLS12-only de 0.10.

- MbedTLS freestanding y fuentes fijadas por SHA256; no host libc, FPU ni proxy TLS.
- Servidor solo TLS1.3 y solo TLS1.2; SNI/SAN/wildcard, RSA-PSS/ECDSA, HelloRetryRequest P384 y cierre autenticado.
- Ambas versiones rechazan SAN/EKU/root incorrectos, RSA débil, SHA1 de hoja, truncamiento y ciphertext alterado; pérdida RX recuperada.
- Sin fuente de entropía no hay conexión; OOM sin fugas; todos los intentos devuelven frames/heap.
- UTC 64 bits: epoch/2038/2099 y 10004 comparaciones calendario bajo ASAN/UBSAN.
- División u64: límites y 100000 comparaciones; ejecución i386 en las operaciones RSA de TLS.
- PCAP sin petición/cuerpo en claro y ClientHello random distinto por intento.
- NX de dato/pila en ring3 produce #PF/error21; kernel recuperado. CPUID/PAE/NXE/WP comprobados, reserva BSS sin frames solapados.
- HTTPS público: example.com negoció TLSv1.3, HTTP200, 892 bytes y close_notify. El tamaño puede cambiar.
- Regresiones de todos los módulos anteriores pasan; CI remota/hardware físico no comprobados.

curl upstream sigue pendiente; no se presenta `https` como curl. Ver CURL_PORT.md.

## NotOS 0.12: ELF32 y VM privada

`make clean test` y `make test-online` PASS en QEMU 9.2.4, compilación sin warnings con `-Wall -Wextra -Werror`.

- ELF32 ET_EXEC/i386 real, CPL3, segmentos separados, BSS multipágina, datos inicializados y pila de 64 KiB.
- brk crecer/reducir/regresar, páginas liberadas inaccesibles; mmap de 2 MiB + 4 KiB y buffer de SYS_WRITE que cruza PDEs.
- Cuatro ELF concurrentes con globals/heap/pilas privados en 16/64 MiB, quinto slot rechazado y recolección exacta.
- NX heap/pila, guard y texto readonly generan #PF recuperable; shell y NotLang continúan.
- Rollback completo de carga inválida/truncada, creación parcial de tablas, crecimiento brk/mmap al cruzar una tabla PAE y fallo de pila kernel por OOM.
- Parser: ASAN/UBSAN, todos los prefijos y 50000 mutaciones; sin modificar output al rechazar.
- Todas las regresiones anteriores pasan. HTTPS público negoció TLSv1.3, HTTP200 y close_notify; TLS1.2 sigue cubierto por peers controlados.

Logs `build/elf-16M.log`, `build/elf-64M.log`. CI remota y equipos físicos no verificados. libc, ABI POSIX, sockets y curl upstream siguen pendientes; ELF.md describe el alcance exacto.

## NotOS 0.13: bloqueo temporizado

`make clean test` y `make test-online` PASS. Después de endurecer el retorno de flags en scheduler_pulse se repitieron wait/ELF/user/multitask: PASS.

- SYS_SLEEP conserva el frame y suspende un proceso gestionado 600 ms, sin consumir presupuesto durante el bloqueo.
- Shell, INT3, estado de red y worker concurrente siguen disponibles; todos los slots pueden quedar bloqueados y despertar correctamente.
- Cancelar un bloqueado libera frames/heap y no deja un timeout que despierte el slot reutilizado.
- Intervalos inválidos rechazados, cero yield, lotes síncronos rechazan espera positiva; round-robin excluye TASK_BLOCKED.
- Comparación relativa de deadlines probada al envolver el contador de 32 bits.
- El ELF de espera intenta mantener NT; al reanudar se limpia NT/IOPL/VM igual que en los cambios de contexto de lotes. DF/registros siguen cubiertos por las regresiones anteriores.
- HTTPS público sigue negociando TLSv1.3 y los peers controlados cubren 1.2/1.3.

Log `build/blocked-wait.log`. No equivale a poll/select ni sockets bloqueantes de libc; ver PROCESSES.md. Sin verificación de CI remota/hardware físico.

## NotOS 0.14: Picolibc de usuario

`make clean test` y `make test-online` PASS en QEMU 9.2.4. Después de añadir copia bytewise para buffers SYS_TIME desalineados se repitieron `make test` y `make test-online`; resultados conservados en los logs del port.

- Picolibc upstream 1.8.11 enlazada estáticamente en un ELF independiente; mapa de enlace comprueba libc.a del sysroot, sin host libc/libgcc ni semihosting.
- Arranque CPL3 con argc/argv/envp, constructor, main, atexit, destructor y vaciado de stdio; salida explícita 23 y retorno 0.
- Strings/memoria, formato integer/long-long y truncamiento snprintf, strtol/strtoll/overflow, qsort/bsearch y environment.
- 2500 operaciones malloc/free, alineación, calloc cero y overflow, realloc crecer/reducir, OOM que conserva la asignación original y rechazo de sbrk(INTPTR_MIN).
- Dos procesos secuenciales y cuatro concurrentes en 16/64 MiB; errno privado conservado a través de nanosleep, quinto slot rechazado y frames/heap restaurados exactamente.
- stdout/stderr reales, EBADF/EFAULT, reloj inválido, SYS_TIME desalineado, stdin/fgetc y fopen fallan explícitamente; no se afirma VFS.
- getentropy rechaza rangos readonly/tamaños inválidos y falla cerrado sin semilla. Los bytes aleatorios no se imprimen ni guardan en logs.
- time_t64, calendario 2040 y RTC real 2050; monotónico/resoluciones y nanosleep inválido. Carry del contador ms bajo probado nativamente.
- División/remainder signed/unsigned de 64 bits: límites y 100000 comparaciones. Helpers propios enlazados separadamente en kernel y aplicación.
- Regresiones de arranque/UI/IRQ/memoria/ELF/aislamiento/multitarea/NotLang/esperas/archivos/red/TLS pasan. Internet: DNS y HTTP200; HTTPS example.com negoció TLSv1.3, HTTP200 y cierre autenticado. TLS1.2 mantiene cobertura con peers controlados.

Código propio con `-Wall -Wextra -Werror`. La biblioteca upstream se compila sin elevar sus warnings a errores; se observó un warning de prototype de `_set_tls` en machine/x86/tls.c, con TLS de threads desactivado. No se silencian ni se presentan las fuentes upstream como libres de warnings.

Logs `build/libc-16M.log`, `build/libc-64M.log`, `build/libc-noentropy.log`, `build/libc-time2050.log`; ELF/símbolos/mapa en build. No se probaron CI remota ni hardware físico. Port inicial single-thread/stdio entero, sin archivos POSIX/stdin funcional/sockets/poll/float/libm ni curl upstream: ver LIBC.md.

## NotOS 0.15: FDs y VFS de usuario

`make clean test` y `make test-online` PASS en QEMU 9.2.4. Tras ajustar IDs de inode compatibles con Picolibc i386 y añadir wrap/lectores concurrentes se repitieron `make test` y `make test-online`: PASS.

- Backend tmpfs/FDs bajo ASAN/UBSAN: holes/truncate a cero, unlink con handles abiertos, offsets de dup compartidos y de open independientes, fd/space quotas, OOM parcial con rollback y descriptor lleno sin mutar un archivo.
- 70000 ciclos crear/unlink/close prueban wrap de inode de 16 bits manteniendo un inode huérfano abierto sin colisión.
- ELF real en 16/64 MiB: fopen/fread/fwrite/fseek/fclose binarios de 8193 bytes, append, stat, redirección dup2, EOF, EAGAIN, EBADF/EFAULT, rutas sin terminador cruzando a página no mapeada y límites de FDs.
- Cuatro escritores concurrentes con FDs propios; cuatro lectores stdin bloqueados, feed/EOF para uno y los otros permanecen bloqueados. Shell/INT3 responden.
- Kill/fault #UD cierran FDs y liberan inodes huérfanos; cancelación de stdin, cola llena y slot reutilizado sin entrada ni referencias anteriores. Frames/heap/nodos/descripciones restaurados exactamente.
- NotFS read-only: handle conserva snapshot, nuevos opens ven cambios de shell, reboot conserva disco y boot image permanece idéntica. Escritura a ese montaje se rechaza.
- Regresiones completas de libc, ELF/VM/aislamiento, scheduler/esperas, UI, IRQ, NotLang, memoria, almacenamiento y TLS pasan. Internet real: DNS/HTTP200 y HTTPS TLSv1.3/HTTP200/close_notify; los peers controlados mantienen cobertura TLS1.2/1.3.

Logs `build/vfs-16M.log`, `build/vfs-64M.log`, `build/vfs-notfs.log`, `build/vfs-notfs-reboot.log`. Código propio con Werror activo; sigue el warning upstream de Picolibc ya documentado en 0.14. No se comprobaron CI remota ni hardware físico. VFS.md especifica cuotas, montaje disco read-only, entrada enrutada por PID y funciones POSIX ausentes. Todavía no hay sockets/poll ni curl upstream.

## Preparación de transporte TCP: capacidad BIOS

`make test` PASS en QEMU 9.2.4. El loader carga 512 KiB en 16 lecturas de 32 KiB, sello 0x8fffc. La pila inicial se mueve a 0x9f000, bajo EBDA del target QEMU; guard 0x90000, fuera del payload. BSS sigue desde 1 MiB y reservado antes de asignar frames. Se mantienen todas las regresiones de memoria, ring3, VFS y TLS. No añade todavía sockets ni nuevos protocolos por sí mismo.

## NotOS 0.16: TCP/IPv4 y DNS de usuario

`make clean` seguido de `make -j4 test` y `make test-online`: PASS en QEMU 9.2.4/TCG con libslirp 4.8.0. Código propio y lwIP seleccionado con `-Wall -Wextra -Werror`; Picolibc conserva la política upstream y el warning de `_set_tls` ya documentado, sin desactivar los warnings del proyecto.

- ELF ring3 independiente enlazado a Picolibc upstream, API TCP/IPv4 sobre syscalls propias y RTL8139/lwIP reales, sin host libc/sockets dentro del invitado.
- 16/64 MiB: streams de 32768 bytes con envíos/lecturas parciales, PEEK y half-close/EOF; descarga HTTP binaria a tmpfs con contenido/stat/unlink verificados. fstat S_IFSOCK, read/write y dup sobre sockets.
- NONBLOCK/EINPROGRESS y fcntl, CLOEXEC, NODELAY, direcciones/puertos, EFAULT/read-only, IPv6/DGRAM rechazados, refusal/SO_ERROR consumido una vez, RST real con datos pendientes y timeout EAGAIN.
- Cuatro procesos TCP simultáneos, pérdidas SYN/TX/RX recuperadas, shell/INT3 disponibles durante recv bloqueado, kill y limpieza exacta de frames/heap/descripciones/tmpfs/endpoints/registros DNS.
- Más de ocho conexiones secuenciales, FD antiguo abierto tras FIN, TIME_WAIT reciclado sin punteros colgantes; caché de dirección y NODELAY conservada. Ocho endpoints/dup y cuota EMFILE. Pool de ocho PCBs retenidos con siete FDs: nueva apertura devuelve ENOMEM y conexiones restantes conservan identidad y envío, sin expulsión de activos.
- DNS A/NXDOMAIN en fixture UDP 53530 separado de producción 53; cancelación, propietario/slot reutilizado y callback tardío sin contaminación. Ausencia de NIC/semilla falla cerrado.
- Regresiones completas de loader/memoria/PAE/ELF/libc/VFS/IRQ/UI/NotLang/procesos/almacenamiento/red/TLS: PASS. Peers TLS controlados mantienen cobertura 1.2/1.3 y certificados inválidos. Internet real: kernel DNS/HTTP200, HTTPS TLSv1.3/HTTP200/close_notify y DNS/HTTP200 desde el ELF de usuario.

Logs: `build/sockets-16M.log`, `build/sockets-64M.log`, `build/sockets-dns.log`, `build/sockets-online.log`, además de regresiones existentes. Mapas/ELFs y las tres imágenes quedan en build. El primer fixture RST sin datos pendientes no era estable bajo slirp; se hizo determinista dejando datos pendientes antes del reset. También se corrigió la lectura de líneas parciales de logs y se usa Connection: close, no un half-close temprano, para el peer HTTP público. La reconstrucción limpia final pasó después de esos ajustes.

Los recursos protocolarios estáticos TIME_WAIT/ARP/DNS son acotados, no se exige que desaparezcan instantáneamente con el FD. Sin CI remota/hardware físico comprobados. API cliente parcial: no poll/select, bind/listen/accept, UDP público, IPv6 ni curl upstream/TLS userspace; ver SOCKETS.md y CURL_PORT.md.

## NotOS 0.17: poll y espera múltiple

`make clean` + `make -j4 test` + `make test-online`: PASS en QEMU 9.2.4/TCG, libslirp 4.8.0. Kernel/adaptadores/demos propios con Wall/Wextra/Werror, ASAN/UBSAN en unidades nativas. Permanece el warning upstream de Picolibc _set_tls ya documentado; no se silenció ni se desactivó Werror del proyecto.

- Orquestación real poll_wait bajo sanitizadores: vector/capacidad/rangos sin efectos, negativos/duplicados, consulta inmediata, timeout relativo y wrap, infinito/cancelación, copia fallida, snapshot no alterable por memoria user y readiness que gana al deadline.
- Política de sockets pura y backend FD real: connect, capacidad TX, RX, EOF/half-close, shutdown, error/HUP y SO_ERROR consumido; stdin/EOF, stdout y archivos/null listos sin consumir datos/offset.
- ELF ring3 en 16/64 MiB: hasta 64 entradas, buffers inválidos/readonly, wire desalineado, negativos/NVAL y duplicados; dos sockets no bloqueantes y eventos demorados de dos peers, PEEK sin consumo, FIN/RDHUP/HUP, refusal/RST y error no consumido por poll.
- TX llega a EAGAIN y vuelve a POLLOUT; stdin despierta por feed/EOF. Timer/shell/INT3/worker durante espera; cuatro procesos bloqueados, solo el PID seleccionado despierta, kill/fault #UD/slot reutilizado sin heredar vector/deadline.
- Restauración exacta de frames/heap/descripciones/archivos/endpoints/registros DNS. Los pools/cachés protocolarios siguen siendo acotados, no se confunden con recursos por proceso.
- Todas las regresiones anteriores pasan. Internet real: DNS/HTTP200 kernel y userspace; HTTPS TLSv1.3/HTTP200/close_notify; DNS y HTTP200 del ELF no bloqueante usando poll. Peers controlados conservan cobertura TLS1.2/1.3 y pruebas negativas de certificados/AEAD.

Logs `build/poll-16M.log`, `build/poll-64M.log`, `build/poll-online.log`, más logs de regresiones. El fixture RST ahora cierra directamente con SO_LINGER(0): el cierre automático de socketserver hacía shutdown(SHUT_WR) antes de close y podía introducir FIN. La suite limpia final pasó después de hacerlo determinista. Los nombres generados siguen el guard portable añadido para upload-artifact.

No se comprobaron CI remota ni hardware físico. Alcance parcial: sin select/ppoll/pselect, señales/EINTR, threads/SMP, urgent TCP, ni curl upstream/TLS userspace. Ver POLL.md y CURL_PORT.md para límites y siguiente gate.

## curl HTTP: validación local y CI

En este avance se ejecutaron make y make test con GCC 15.2.0, binutils 2.46 y
QEMU 10.2.1/TCG: PASS. La construcción limpia también se comprobó durante la
integración; los fallos encontrados en expectativas de ayuda y sincronización
de fixtures se corrigieron antes de publicar la fase.

La fase build (77f80c5), libc (8886cc0) y curl (714e2b7) pasaron cada una make y
make test localmente, y su CI remoto make clean test concluyó success:

- [Construcción y QMP](https://github.com/enderliker/NotOS/actions/runs/38106625143)
- [Contratos de libc y binary64](https://github.com/enderliker/NotOS/actions/runs/38107037072)
- [Integración curl HTTP](https://github.com/enderliker/NotOS/actions/runs/38107623839)

Curl informa versión 8.11.1 y solo http; se comprobaron argumentos, archivos y
upload binarios de 32768 bytes, DNS A/NXDOMAIN, HTTP404, conexión rechazada,
timeout, cuatro procesos, kill/reuso y recuperación de frames/heap/FDs/endpoints.
La regresión TLS kernel 1.2/1.3 y almacenamiento/boot drive pasa. No se ejecutó
make test-online para este avance ni se probó HTTPS en curl o hardware físico.

El código propio mantiene Werror. Los avisos upstream de Picolibc _set_tls y
curl transfer.c/url.c quedan visibles y documentados; no se modificaron fuentes
ni versiones de dependencias. No se añadieron TODOs.
