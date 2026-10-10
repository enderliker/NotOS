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

Los logs completos quedan en `build/smoke.log`, `build/network.log` y `build/online.log`. No se ha probado HTTPS ni hardware real. El kernel final ocupa 11416 bytes de código/datos en una imagen de disco de 1 MiB.
