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

El planificador se añadirá como módulo separado de la política de selección y de la gestión de recursos. En el avance de preparación todavía se ejecuta una tarea a la vez.

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
