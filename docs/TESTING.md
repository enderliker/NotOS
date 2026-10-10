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
