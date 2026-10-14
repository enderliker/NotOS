# Aplicaciones ELF32 y memoria privada — NotOS 0.12

## Uso

```text
elf-test
elf-run demo
elf-run nx-heap
elf-run nx-stack
elf-run stack-guard
elf-run readonly
elf-run sleep
ps
```

`elf-run` arranca ELF32 estáticos **reales** compilados por separado y embebidos como archivos ELF, no código plano rebautizado. Se ejecutan en ring3, comparten el gestor de cuatro procesos con NotLang y son recolectados al terminar. La shell no espera a que acabe el proceso. Las variantes de fallo son diagnósticos intencionales: solo termina la tarea.

`spawn-elf PATH [ARGS]` carga ejecutables VFS y SYS_EXEC reemplaza el proceso. El comando shell `exec FILE` conserva su significado existente: compilar fuente NotLang. Curl HTTP/HTTPS usa el mismo cargador; ver CURL_PORT.md.

## Validación y carga

`elf.c` es un parser puro y acotado. Solo acepta ELF32 little-endian/System V, ET_EXEC/i386/version1, cabecera de 52 bytes, program headers de 32 bytes y hasta 16 headers. El archivo está limitado a 2 MiB. El entry point debe estar dentro de bytes de archivo de un segmento ejecutable.

Los PT_LOAD deben tener lectura, filesz <= memsz, rangos completos dentro del archivo y direcciones dentro de la región de imagen. Se comprueba alineación potencia de dos y congruencia offset/VA; no se supone que los punteros estén alineados. Se rechazan W+X, solapamientos incluso a nivel de página, intérprete, segmentos dinámicos/TLS y GNU_STACK ejecutable. No se implementan relocaciones ni enlazado dinámico.

Solo después de validar todo el plan se adquieren frames. Cada segmento se copia por páginas usando aliases físicos supervisor; BSS y padding arrancan a cero. El código user es RX, rodata R/NX y datos RW/NX. Fallos parciales liberan todas las páginas/tablas y conservan el contador de frames.

## Espacio por proceso

| Región | Dirección | Uso |
| --- | --- | --- |
| Imagen | 0x08000000..0x08400000 | PT_LOAD |
| Heap | 0x08400000..0x08800000 | brk, demanda, RW/NX |
| Anónima | 0x08800000..0x08c00000 | mmap privado, RW/NX |
| Reserva/guard | hasta 0x08ff0000 | no presente |
| Pila ELF | 0x08ff0000..0x09000000 | 64 KiB RW/NX |

Cada espacio tiene PDPT/directorio privado y tablas user creadas bajo demanda. La destrucción recorre exclusivamente la región user; nunca libera mappings supervisor compartidos. No hay una lista grande de propietarios dentro de la estructura task. El layout de las demos antiguas de una página y de NotLang se conserva.

## ABI incremental, no ABI Linux/POSIX completa

INT 0x80: EAX número/resultado, EBX primer argumento, ECX segundo. Se conservan todos los números existentes.

- **6 / SYS_BRK:** EBX break solicitado; cero consulta. Devuelve el break actual/antiguo si falla. Extender hace rollback completo ante OOM; reducir libera páginas completas.
- **7 / SYS_MMAP:** EBX longitud. Devuelve VA de memoria anónima privada RW/NX o -ENOMEM. Sin prot/flags/address hint, archivos ni MAP_FIXED.
- **8 / SYS_MUNMAP:** EBX VA alineada, ECX longitud. Devuelve 0 o -EINVAL; solo acepta la región anónima. Las tablas vacías se liberan.

Validación de buffers recorre todos los PDE/PTE involucrados, incluyendo cruces de 2 MiB. Se invalida TLB al cambiar mappings del proceso activo. El kernel sigue sin preemption/SMP.

Esto **no** es ABI Linux, ni mmap POSIX completo, ni una implementación completa de mmap POSIX. Desde 0.14, el runtime de Picolibc prepara argc/argv, envp vacío y AT_NULL, con errno/stdio/malloc reales; ver LIBC.md. Las demos standalone conservan su entry propio. Thread-local storage sigue sin soporte.

## Pruebas

- Parser nativo con ASAN/UBSAN: tipos/flags/rangos/entry/alineación/solapamiento, todos los prefijos y 50000 mutaciones. Output sin cambios cuando falla.
- QEMU: BSS multipágina, dato inicializado, pila >12 KiB en región de 64 KiB, crecimiento/reducción/reutilización brk y cero al reutilizar frames.
- mmap de 2 MiB + 4 KiB y SYS_WRITE con buffer que cruza tablas PAE; munmap invalida acceso, rechaza regiones prohibidas y permite reutilización cero.
- Cuatro ELF concurrentes en 16/64 MiB: direcciones iguales, datos/heap/pilas distintos, yield, slots limitados y recolección.
- Fallos reales #PF: ejecución de heap/pila (NX), guard y escritura de texto RX; kernel continúa.
- OOM al crear root/tablas, al crecer heap/mmap después de cruzar una tabla PAE y al reservar pila kernel. Frames/breaks originales restaurados.

No se afirma soporte para binarios arbitrarios, libc completa, señales, kernel preemptivo ni hardware físico.

## Espera por syscall

NotOS 0.13 agrega SYS_SLEEP=9 para procesos gestionados: 0..60000 ms, cero yield y retorno 0 al despertar.
TASK_BLOCKED conserva el frame privado y no es seleccionado hasta vencer el timeout. Ver PROCESSES.md para restricciones de lotes síncronos y POLL.md para espera de FDs.

## Startup y libc

`process_spawn_elf_args` añade nombre y un argumento acotados al stack privado, seguido de argv NULL, envp vacío y auxv AT_NULL. ESP inicial alineado a 16 bytes; copy se realiza por aliases supervisor antes de ejecutar. El CRT separado conserva segmentos W^X y ejecuta arrays init/fini. La API `process_spawn_elf_split` admite hasta 256 bytes separados por espacios y 16 elementos de argv, incluido el nombre, conservando el mismo layout. Los excesos se rechazan y la creación revierte todos los recursos. Ver LIBC.md y CURL_PORT.md.

## Ejecutables VFS y reemplazo

`spawn-elf /system/libc.elf exec-source` lanza una imagen VFS. System proporciona
ELFs inmutables; tmpfs/NotFS admiten imágenes hasta 64 KiB (NotFS 60 KiB). El
parser ELF y sus límites/W^X siguen vigentes; una imagen se copia antes de cerrar
su handle, y los blobs system se leen sin asignar una copia al heap kernel.

`notos_exec(path, arguments)` usa SYS_EXEC=37, con cadena separada por espacios,
256 bytes/16 elementos incluido argv[0] (basename), envp vacío y AT_NULL. Se
preparan y validan imagen, espacio y stack antes de confirmar. Un fallo conserva
PID, espacio, cwd, offsets y FDs. Al confirmar se cambia CR3 antes de destruir
el espacio anterior, se reconstruye el frame en la pila kernel compartida y se
cierran solo los slots CLOEXEC. El éxito no retorna a la imagen antigua.
Los descriptores conservan sus referencias, flags y posiciones; no hay fork,
execve POSIX, loader dinámico ni búsqueda PATH.

El presupuesto normal conserva 250 ticks CPL3; curl embebido dispone de 2500
para criptografía/CA/limpieza, con el mismo quantum de cinco ticks y cuatro slots.
No equivale a tiempo de pared. No se elimina el límite para errores TLS.
