# Verificaciones

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
