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
