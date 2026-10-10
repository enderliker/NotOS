# Arquitectura y convenciones

NotOS es un kernel educativo freestanding. Las capas de ejecución se mantienen separadas:

| Módulo | Responsabilidad | No debe hacer |
| --- | --- | --- |
| `arch.*`, `user_entry.S`, `isr.S` | GDT/TSS, ABI de CPU, entrada/retorno e interrupciones | interpretar comandos |
| `physical*`, `paging*`, `space*`, `heap*` | memoria y espacios de direcciones | decidir política de planificación |
| `task.*` | recursos, estado, estadísticas y terminación de tareas | imprimir resultados de demos |
| `syscall.*` | ABI, validación y despacho de syscalls | crear comandos de shell |
| `user.*` | demos, resultados esperados y diagnóstico de shell | manejar directamente interrupciones |
| `syscall_numbers.h` | constantes ABI compartidas entre C y assembly | contener tipos C que rompan el assembler |

`scheduler_policy.*` contiene round-robin puro y probado nativamente. `scheduler.*` mantiene el lote activo, quantum, trazas y cambios de contexto; `multitask.*` crea y verifica las demos sin participar en IRQ.

Cada tarea tiene un CR3 propio y una pila kernel de 16 KiB con canario. IRQ0 entrega EOI antes de elegir el siguiente contexto; el dispatcher devuelve el frame a restaurar por assembly. Al cambiar se actualizan CR3 y ESP0 de la TSS. El kernel conserva su frame de entrada y recupera su CR3 al terminar el lote.

El quantum es de 5 ticks del PIT; el presupuesto máximo es de 250 ticks entregados a CPL3 por tarea, no de tiempo de pared ni tiempo de CPU exacto. Hasta cuatro tareas por lote; no hay SMP ni preemption del kernel. CR0.TS bloquea FPU/MMX/SSE: no se declara preservación de ese estado. Shell y red no se procesan mientras el lote ocupa la llamada síncrona.

## Módulos añadidos en 0.9

- `process.*`: propiedad de slots/PID y recolección; `scheduler_pulse` recupera el kernel después de cada quantum/yield/terminación para mantener el event loop vivo.
- `disk.*` / `fs.*`: transporte ATA primary-slave separado de snapshots/CRC/CRUD; nunca escribir el boot drive.
- `mouse_decode.*` / `mouse.*` / `gui.*`: parser puro, cola IRQ y event loop, rendering y acciones diferidas, respectivamente.
- `notlang_compile.*` / `notlang_command.*` / `user/notlang_vm.c`: compilación acotada, carga y VM ejecutada en un espacio ring3; protocolo compartido `notlang_bytecode.h`.
- `tcp_retry.*`: política pura de ACK/backoff separada del driver/protocolo; la pila sigue teniendo una solicitud activa y una transmisión pendiente a la vez.

## TLS en 0.10

`tls.*` maneja engine/application y limpieza; `tls_profile.*` define crypto/versiones; `x509_policy.*` delega validación y añade límites/SAN/EKU; `certificate_policy.*` es el parser puro probado con sanitizadores. `entropy.*`, `rtc.*`, `utc.*` encapsulan plataforma. `net.*` solo mueve bytes/registros por TCP. No se emiten datos de aplicación antes de validar el handshake ni se degrada a plaintext.

## Reglas

- Headers con guards y APIs explícitas; datos internos `static` cuando corresponda.
- No asignar memoria, imprimir diagnósticos extensos ni interpretar programas dentro de handlers IRQ.
- Validar rangos completos antes de leer buffers user; preservar el estado de IF en secciones críticas.
- Destruir recursos solo cuando ya no están activos; reportar resultados antes de liberar el espacio que los contiene.
- Errores de programa y fallos del kernel son categorías diferentes.
- Constantes ABI nombradas, compartidas por C/assembly y protegidas por dependencias de Make.
- Los tests de kernel arrancan la imagen real; los algoritmos puros se prueban también nativamente.
- Cambios funcionales en commits `feat(alcance) mensaje`, después de compilar con `-Wall -Wextra -Werror` y pasar las pruebas aplicables.

La documentación de límites es parte del contrato. Un prototipo que pasa estos tests no equivale a un sistema de producción ni a una auditoría completa de seguridad.

## TLS y memoria en 0.11

PAE y NX reemplazan tablas de 32 bits: PDPT privado y directorio por tarea; BSS reservado explícitamente desde 1 MiB. Texto RX, rodata R/NX, datos/pilas RW/NX.
`tls_profile.*` ahora configura MbedTLS 1.2/1.3; `tls.*` proporciona BIO no bloqueante, verifica el handshake y libera contextos/slots PSA.
`certificate_policy.*` sigue aplicando SAN/EKU/límites sin borrar errores de MbedTLS. `x509_policy.*` es legado BearSSL, no se enlaza en el cliente actual.
`tls_platform_port.*` adapta heap/RTC/DRBG; no es una libc POSIX. `integer_runtime.*` aporta división u64 probada para i386.

## ELF32 y VM de usuario

`elf.*` valida sin asignar; `space.*` carga segmentos y administra frames/PTE privados, brk y anónimos.
`task_create_elf` utiliza el entry/stack del espacio; `process_spawn_elf` reutiliza slots y recolección existentes.
`elf_command.*` es integración/diagnóstico, no código del parser. Las demos C se compilan y enlazan separadas del kernel.
La validación de buffers recorre PDEs de 2 MiB; la propiedad se recupera caminando tablas, no con un array grande por tarea.
