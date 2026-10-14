# Memoria — NotOS 0.6

El cargador obtiene un mapa BIOS E820 completo antes de entrar en modo protegido. El header está en 0x5000; admite hasta 64 entradas de 24 bytes, con compatibilidad para entradas de 20 bytes. Si el BIOS no proporciona un mapa completo y válido, el sistema no continúa a la asignación de RAM.

El gestor físico usa páginas de 4096 bytes y dos bitmaps: páginas utilizables y páginas libres. Respeta regiones no disponibles incluso si se solapan con regiones de tipo RAM. Solo admite páginas enteras dentro de RAM habilitada. Todo el primer MiB está reservado para kernel, firmware, VGA, mapa BIOS, pila y estructuras estáticas.

Se gestiona como máximo el primer bloque de 64 MiB de direcciones físicas. Esto no equivale a afirmar que toda esa región sea RAM: el mapa BIOS decide qué páginas se pueden usar. En QEMU se comprobó con 16, 32, 64 y 128 MiB; en el último caso solo se gestiona RAM hasta el límite de 64 MiB.

```text
mem
frame-test
```

`frame-test` agota los frames libres, comprueba escritura en los extremos de cada página, rechaza liberaciones inválidas/dobles y devuelve todos los frames que adquirió. No toca páginas ya asignadas a otros componentes. El asignador entrega frames sin inicializar; quien los utilice debe limpiar su contenido si hace falta.

El sector BIOS tiene exactamente 512 bytes. La carga actual separa el kernel bajo y el ELF curl en memoria alta; las direcciones, firmas y capacidades se especifican al final de este documento. Linker y generador verifican sus límites.

Desde NotOS 0.7 existen espacios privados y demos en ring 3 (ver USERSPACE.md); todavía no hay swapping ni un gestor de procesos de propósito general.

## Paginación implementada

Páginas x86 de 4 KiB, directorio/tablas propias y CR0.PG + CR0.WP activos. Se requiere una CPU x86 con las capacidades de 486 (WP/INVLPG); QEMU es el único hardware comprobado. El kernel y las estructuras estáticas usan identity mapping. Por encima de 1 MiB se mapea RAM utilizable según E820 y la región estática reservada del kernel, hasta el cap de 64 MiB. El VGA del primer MiB permanece mapeado sin caché.

- Página cero no presente: las lecturas nulas provocan #PF.
- Código y rodata alineados y de solo lectura, incluso para escrituras supervisor con WP.
- Página 0x90000 no presente como guard inferior de la zona de pila. Un desbordamiento que corrompa la pila puede necesitar TSS/stack alternativo, todavía ausentes.
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

`vm-test` verifica dos aliases de un mismo frame, traducción con offset, remapping a otro frame, limpieza y restauración del contador físico. Los mappings no tienen conteo de referencias: el llamador debe eliminar todos los aliases antes de liberar un frame. La ventana del heap sigue siendo exclusivamente supervisor; las demos de usuario tienen directorios separados desde NotOS 0.7. El identity mapping de los frames sigue existiendo. PAE/NX y memoria privada bajo demanda se describen abajo y en ELF.md; no hay ASLR ni swapping. Los espacios privados de las demos user se documentan por separado.

## Heap dinámico implementado

Se reserva un heap de 1 MiB en 0xd0001000 usando 256 frames físicos, con guards no presentes en 0xd0000000 y 0xd0101000. Si no se pueden obtener todos los frames al iniciar, se eliminan los mappings adquiridos y se devuelven sus frames antes de reportar el fallo. El heap es fijo: las liberaciones reutilizan el espacio del heap, no devuelven sus páginas al gestor físico.

API del kernel:

- `kmalloc(bytes)`: asigna, con alineación de 16 bytes; devuelve null si bytes=0 o no cabe. Contenido no inicializado.
- `kcalloc(count, bytes)`: comprueba overflow del producto y limpia el contenido solicitado. La limpieza se hace con IRQ habilitadas cuando lo estaban al entrar.
- `kfree(pointer)`: libera un bloque válido; null es un no-op exitoso. Rechaza punteros ajenos, interiores, no alineados y doble liberación de un bloque que sigue libre.

El algoritmo first-fit divide bloques cuando hay espacio para otro header/payload y une vecinos libres al liberar. Los headers tienen magic y comprobación de rangos/alineación/enlaces antes de recorrer la lista. Es una ayuda para detectar corrupción, no una defensa completa frente a código malicioso en ring 0. El heap no se usa dentro de handlers IRQ y no es un asignador SMP o de tiempo real.

```text
heap
heap-test
alloc 128
alloc 4097
heap
free 0
free 1
heap
fault heap-end
```

Los comandos `alloc`/`free` ofrecen ocho slots de diagnóstico. `alloc` usa kcalloc; `free` recibe el índice, no una dirección arbitraria. `heap-test` requiere heap sin asignaciones activas y comprueba alineación, división, unión, calloc, OOM y liberaciones inválidas; restaura el estado. `fault heap-end` escribe en el guard final: #PF esperado, error 2 y CR2 0xd0101000. **Este comando detiene el sistema.**

Pruebas: 20000 operaciones de asignación/liberación con patrones en una arena nativa, más tres autopruebas en el invitado y ocho asignaciones simultáneas liberadas fuera de orden. Se verifica restauración de espacio/contadores y funcionamiento tras agotar frames libres. La validación de metadata corrupta se prueba nativamente; no se afirma recuperación segura de cualquier corrupción.

Los guards protegen los bordes de la región virtual, no cada bloque individual. No hay detección completa de use-after-free, redzones ni crecimiento automático del heap. El heap kernel es RW/NX; el aislamiento de aplicaciones utiliza espacios privados. Los componentes del kernel comparten el espacio supervisor; las demos en ring 3 utilizan sus propios directorios CR3 y no un heap de aplicación.

## PAE y NX

Se exige CPUID.PAE y CPUID.NX: EFER.NXE, CR4.PAE y CR0.PG/WP.
PDPT de cuatro entradas, directorios/tablas de 512 entradas de 64 bits; cada frame sigue siendo de 4 KiB.
Text RX; rodata R/NX; datos, heap y pilas RW/NX. El descriptor se publica después de escribir su mitad alta.
Cada tarea tiene PDPT y directorio privado, compartiendo exclusivamente los mapeos supervisor.
Las pruebas ejecutan saltos desde ring3 al dato/pila: #PF con error 21, sin terminar el kernel.

El cargador lee 512 KiB hacia 0x10000..0x90000 y valida el sello 0x8fffc.
Para curl y raíces añade veinticuatro lecturas de 32 KiB: cada bloque pasa por el buffer BIOS
0x90000..0x98000 y se copia con INT 15h/AH=87 a 0x100000..0x1c0000. El sello
alto está en 0x1bfffc. Las transferencias BIOS no cruzan un límite de 64 KiB;
VGA y la pila inicial 0x9f000 quedan fuera de ambos destinos.

La imagen ELF de curl ocupa la región alta de solo lectura/NX. BSS comienza
en 0x1c0000, se borra en entry y debe terminar antes de 0x280000. El asignador
reserva desde 0x100000 hasta el fin de BSS antes de entregar frames; el mapping
supervisor incluye esa región reservada. Linker y generador comprueban las
capacidades baja/alta y tamaño del archivo de disco. La protección R/NX no
convierte el blob en código kernel: el cargador ELF lo copia al espacio user.

La ejecución ELF, brk y mmap privados están documentados en [ELF.md](ELF.md).
