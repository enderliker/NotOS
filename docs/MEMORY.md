# Memoria — fase 3, avance físico

El cargador obtiene un mapa BIOS E820 completo antes de entrar en modo protegido. El header está en 0x5000; admite hasta 64 entradas de 24 bytes, con compatibilidad para entradas de 20 bytes. Si el BIOS no proporciona un mapa completo y válido, el sistema no continúa a la asignación de RAM.

El gestor físico usa páginas de 4096 bytes y dos bitmaps: páginas utilizables y páginas libres. Respeta regiones no disponibles incluso si se solapan con regiones de tipo RAM. Solo admite páginas enteras dentro de RAM habilitada. Todo el primer MiB está reservado para kernel, firmware, VGA, mapa BIOS, pila y estructuras estáticas.

Se gestiona como máximo el primer bloque de 64 MiB de direcciones físicas. Esto no equivale a afirmar que toda esa región sea RAM: el mapa BIOS decide qué páginas se pueden usar. En QEMU se comprobó con 16, 32, 64 y 128 MiB; en el último caso solo se gestiona RAM hasta el límite de 64 MiB.

```text
mem
frame-test
```

`frame-test` agota los frames libres, comprueba escritura en los extremos de cada página, rechaza liberaciones inválidas/dobles y devuelve todos los frames que adquirió. No toca páginas ya asignadas a otros componentes. El asignador entrega frames sin inicializar; quien los utilice debe limpiar su contenido si hace falta.

El cargador ahora lee dos bloques de 32 KiB, en 0x10000 y 0x18000. Una firma al final de los 64 KiB verifica realmente la segunda lectura. La imagen del kernel está rellenada hasta 64 KiB; el linker y el generador rechazan contenido que exceda el límite. El sector BIOS sigue teniendo exactamente 512 bytes.

Paginación y heap todavía pendientes en este avance. No hay espacios de direcciones separados, ring 3, swapping ni recuperación de procesos.
