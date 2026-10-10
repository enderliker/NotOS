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
