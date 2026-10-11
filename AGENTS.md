# Guía de contribución

NotOS es un kernel educativo x86 de 32 bits para BIOS y QEMU. Usa C11
freestanding, assembly GAS (`.S`) y pruebas en Python y C nativo.

## Flujo de trabajo

1. Lee `handoff.md`, revisa `git status` y consulta la documentación del módulo.
   Conserva el trabajo local existente; no restaures ni borres cambios ajenos.
2. Define un alcance concreto. Para cambios en más de dos archivos, escribe un
   plan breve antes de editar. Evita refactorizaciones fuera de ese alcance.
3. Si una decisión pendiente cambia la API o el ABI, plantea una sola pregunta.
   Para decisiones internas, decide y declara la suposición relevante.
4. Implementa el comportamiento y actualiza sus límites documentados juntos.
5. Ejecuta `make` y `make test` antes de entregar o crear un commit funcional.
   Si cambias la construcción, verifica también una compilación limpia.
6. Reporta únicamente comandos ejecutados y resultados observados. Distingue
   pruebas locales, fixtures de red, internet público y CI remota.

Corrige los fallos en su causa. No silencies warnings, retires `-Werror` del
código propio ni desactives pruebas. Las dependencias upstream conservan su
política de warnings explícita; documenta los avisos observados.

## Comandos

| Comando | Propósito |
| --- | --- |
| `make` | Construir `build/notos.img` |
| `make test` | Pruebas nativas y ejecución de imágenes reales en QEMU |
| `make clean test` | Reconstrucción limpia usada por CI |
| `make test-online` | Pruebas opcionales con DNS/HTTP/HTTPS públicos |
| `make run` | QEMU con pantalla y consola serie |

El código propio usa `-m32 -march=i486 -std=c11 -ffreestanding`,
`-mno-sse -mno-sse2 -mno-mmx -msoft-float` y `-Wall -Wextra -Werror`.
No se enlazan libc del host, `libgcc` ni semihosting en las aplicaciones.

## Documentación por tema

Los documentos están en `docs/`. Lee `ARCHITECTURE.md` antes de un cambio
estructural y los documentos pertinentes antes de tocar un módulo.

| Tema | Documentos |
| --- | --- |
| Memoria, paginación y heap | `MEMORY.md`, `ELF.md` |
| IDT, IRQ y excepciones | `INTERRUPTS.md` |
| Tareas y scheduler | `TASKS.md`, `PROCESSES.md` |
| Syscalls, ring 3 y libc | `USERSPACE.md`, `ELF.md`, `LIBC.md` |
| Descriptores y archivos | `VFS.md`, `STORAGE.md` |
| Red y TLS | `NETWORK.md`, `SOCKETS.md`, `POLL.md`, `TLS.md` |
| Cliente curl | `CURL_PORT.md`, `LIBC.md` |
| Interfaz y lenguaje | `UI.md`, `NOTLANG.md`, `NOTLANG_USER.md` |
| Validación y próximos hitos | `TESTING.md`, `ROADMAP.md` |

Los límites son parte del contrato. No declares SMP, hardware físico,
conformidad POSIX completa, FPU/SSE ni otras capacidades sin implementación y
pruebas. No conviertas un éxito de compilación en evidencia de ejecución.

## Invariantes

- En IRQ: no asignar/liberar recursos, interpretar programas ni imprimir
  diagnósticos extensos. Entregar EOI antes de cambiar de contexto.
- Validar el rango completo y los permisos de los buffers de usuario antes de
  leerlos, escribirlos o consumir estado. Copiar estructuras wire desalineadas
  de forma segura. Conservar IF en secciones críticas.
- Liberar recursos solo cuando ya no estén activos. Revertir adquisiciones
  parciales; exit, kill y fallos deben liberar los recursos del proceso.
- Distinguir errores de aplicaciones de fallos del kernel.
- Mantener los cuatro slots del scheduler salvo una tarea explícita de cambio.
- Nunca escribir el disco de arranque desde `disk.*` o `fs.*`: NotFS usa el
  disco de datos separado.
- No devolver éxito ficticio para funciones ausentes: usar un error definido.
- No modificar fuentes, versiones ni hashes de terceros sin autorización.

## Organización y estilo

Un módulo tiene una responsabilidad. La lógica sin I/O, asignación ni registros
CPU pertenece a `*_policy.*` o `*_core.*`, con pruebas nativas cuando proceda.
Separa algoritmos, adaptadores de plataforma y comandos de diagnóstico.

- Headers con guard `NOTOS_<NOMBRE>_H`, API explícita e internos `static`.
- Constantes ABI en `*_abi.h` o `syscall_numbers.h`; los headers compartidos con
  assembly no incluyen tipos C incompatibles.
- Funciones y variables en `snake_case`; prefijo del módulo en APIs.
- Macros y enumeraciones en `UPPER_SNAKE`, con prefijo del módulo.
- Tipos enteros del proyecto: `u8`, `u16`, `u32`, `u64`; struct/enum en
  `snake_case`. En adaptadores libc, usar los tipos de su interfaz pública.
- Nombres que expresen propósito y unidad: `timeout_ms`, `frame_count`.
  Nombres de una letra solo para índices; abreviaturas técnicas convencionales.
- Funciones con un nivel de abstracción y retornos tempranos; unas 40 líneas
  como orientación, no como motivo para fragmentar código artificialmente.
- Código nuevo: cuatro espacios, una sentencia por línea, llaves en la misma
  línea. En código existente, respetar su estilo sin reformatear todo el archivo.

## Comentarios y pendientes

Comentarios en inglés, `/* ... */` en C/assembly, breves y encima del código.
Usa sintaxis propia del lenguaje en Python y Make. Comenta invariantes de
seguridad, restricciones de hardware/orden, layouts ABI y decisiones que no
sean evidentes. Headers públicos explican contrato, errores y propiedad;
rutinas assembly explican registros/pila cuando sean relevantes.

No comentes instrucciones evidentes, historial ni código muerto. Actualiza un
comentario si cambias el comportamiento que describe.

Un TODO es excepcional y tiene formato
`/* TODO(modulo): acción concreta, hito o condición */`. Solo representa trabajo
real fuera del alcance; nunca una implementación incompleta o una validación de
seguridad pendiente. Lista cada TODO nuevo al entregar y, si es visible, en
`docs/ROADMAP.md`.

## Commits y entrega

- Formato solicitado: `feature(alcance) descripción`, con descripción en español.
- Un cambio lógico por commit, después de compilar y pasar las pruebas aplicables.
- Selecciona los archivos explícitamente. No incluyas `build/`, `.cache/`, claves
  de fixtures, credenciales ni archivos temporales.
- No hagas push ni force-push salvo petición explícita. Una receta de handoff no
  autoriza publicar cambios ni permite afirmar resultados de CI.
- Respuesta final breve en español: cambios, comandos y resultados reales,
  commits creados, límites o TODOs nuevos.
