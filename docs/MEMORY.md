# Memoria — fase 3, frames y paginación

El cargador obtiene un mapa BIOS E820 completo antes de entrar en modo protegido. El header está en 0x5000; admite hasta 64 entradas de 24 bytes, con compatibilidad para entradas de 20 bytes. Si el BIOS no proporciona un mapa completo y válido, el sistema no continúa a la asignación de RAM.

El gestor físico usa páginas de 4096 bytes y dos bitmaps: páginas utilizables y páginas libres. Respeta regiones no disponibles incluso si se solapan con regiones de tipo RAM. Solo admite páginas enteras dentro de RAM habilitada. Todo el primer MiB está reservado para kernel, firmware, VGA, mapa BIOS, pila y estructuras estáticas.

Se gestiona como máximo el primer bloque de 64 MiB de direcciones físicas. Esto no equivale a afirmar que toda esa región sea RAM: el mapa BIOS decide qué páginas se pueden usar. En QEMU se comprobó con 16, 32, 64 y 128 MiB; en el último caso solo se gestiona RAM hasta el límite de 64 MiB.

```text
mem
frame-test
```

`frame-test` agota los frames libres, comprueba escritura en los extremos de cada página, rechaza liberaciones inválidas/dobles y devuelve todos los frames que adquirió. No toca páginas ya asignadas a otros componentes. El asignador entrega frames sin inicializar; quien los utilice debe limpiar su contenido si hace falta.

El cargador ahora lee dos bloques de 32 KiB, en 0x10000 y 0x18000. Una firma al final de los 64 KiB verifica realmente la segunda lectura. La imagen del kernel está rellenada hasta 64 KiB; el linker y el generador rechazan contenido que exceda el límite. El sector BIOS sigue teniendo exactamente 512 bytes.

El heap dinámico todavía está pendiente en este avance. No hay espacios de direcciones separados, ring 3, swapping ni recuperación de procesos.

## Paginación implementada

Páginas x86 de 4 KiB, directorio/tablas propias y CR0.PG + CR0.WP activos. Se requiere una CPU x86 con las capacidades de 486 (WP/INVLPG); QEMU es el único hardware comprobado. El kernel y las estructuras estáticas usan identity mapping. Por encima de 1 MiB solo se mapea RAM utilizable según E820, hasta el cap de 64 MiB. El VGA del primer MiB permanece mapeado sin caché.

- Página cero no presente: las lecturas nulas provocan #PF.
- Código y rodata alineados y de solo lectura, incluso para escrituras supervisor con WP.
- Página 0x80000 no presente como guard inferior de la zona de pila. Un desbordamiento que corrompa la pila puede necesitar TSS/stack alternativo, todavía ausentes.
- Ventana virtual supervisor de 4 MiB en 0xd0000000 para mapear frames ya asignados. Todas las entradas son supervisor, no user.
- Map/unmap con invalidación TLB mediante INVLPG y secciones críticas que preservan IF. No se permite sobrescribir un mapping ya presente.

```text
vm
vm-test
fault null
fault readonly
fault guard
fault unmapped
```

Los comandos `fault` detienen deliberadamente la CPU y requieren reiniciar QEMU. Se comprueban #PF y CR2 reales: nulo (error 0, CR2 0), escritura en código (error 3, CR2 0x10000), guard virtual y acceso tras unmap. Este último toca el mapping antes de eliminarlo para comprobar la invalidación del TLB.

`vm-test` verifica dos aliases de un mismo frame, traducción con offset, remapping a otro frame, limpieza y restauración del contador físico. Los mappings no tienen conteo de referencias: el llamador debe eliminar todos los aliases antes de liberar un frame. La ventana no es un API de usuario ni implica aislamiento de procesos. El identity mapping de los frames sigue existiendo y no hay NX, ASLR, demand paging, swapping ni espacios de direcciones por proceso.
