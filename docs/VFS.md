# VFS, descriptores y directorios

El invitado opera sobre archivos propios, con 16 FDs por tarea y 64 descripciones
abiertas globales. `dup` comparte posición/flags; CLOEXEC pertenece al descriptor.
No se declara conformidad POSIX completa.

| Ruta | Contrato |
| --- | --- |
| `/tmp` | Directorios y archivos RAM, 16 nombres y 24 nodos, hasta 64 KiB por archivo |
| `/disk` | NotFS v2 persistente, 24 nodos y 60 KiB de payload total |
| `/system` | Imágenes inmutables `curl.elf`, `libc.elf` y `cacert.pem` |
| `/dev` | stdin privado, stdout/stderr/console y null |

Las rutas absolutas o relativas se normalizan respecto al cwd privado del
proceso: `.`, `..` y separadores repetidos; máximo 63 bytes. Los componentes
para tmpfs/NotFS tienen 1..23 caracteres alfanuméricos, punto, guion o underscore.
Cada tarea nueva comienza en `/`; exec conserva cwd. El cwd de shell es separado.
`mkdir`, `rmdir`, `chdir`, `getcwd`, stat y apertura readonly de directorios están
implementados. `notos_readdir` devuelve una entrada wire y avanza la descripción
compartida; los cambios concurrentes de nombres pueden alterar la enumeración.
No hay rename, symlinks, hardlinks, montaje configurable, permisos multiusuario,
timestamps mantenidos ni API POSIX completa de dirent.

Los opens del mismo inode `/disk` comparten un nodo coherente; los cambios de
shell y syscalls son visibles en las lecturas siguientes. Unlink conserva una
copia RAM del contenido para los handles abiertos, reservada antes de publicar
la eliminación. Un inode recreado no cambia esos handles. Tras mkfs, los handles
antiguos de disco devuelven ENOENT: format es una operación administrativa
explícita, no unlink. Inodes persistentes no se reutilizan durante el montaje;
se agotan tras 65280 creaciones, aunque se borren nombres. IDs tmpfs 16..255
se reutilizan evitando colisiones con todos los nodos vivos; IDs de disco
256..65535 son compatibles con ino_t i386.

`open/read/write/close/lseek/stat/fstat/unlink/ftruncate/dup/dup2/fcntl/isatty`
usan syscalls y Picolibc proporciona stdio. Create/exclusive/truncate/append,
nonblock y CLOEXEC están soportados. Un write tiene hasta 4096 bytes; stdio/libc
puede dividir transferencias. Append toma EOF en cada syscall. Seek/write crea
huecos a cero; truncate borra datos descartados. Cuota RAM: 64 páginas (256 KiB)
para tmpfs y handles de disco sin nombre. System y disco enlazado no consumen
esa cuota. Las reservas parciales tmpfs se revierten ante OOM/ENOSPC.

NotFS publica cada write/truncate en un snapshot completo; una transferencia
multisyscall puede quedar parcialmente escrita. La shell `write` reemplaza un
archivo de disco en una publicación. No hay mmap de archivos ni journaling.
Las imágenes system son readonly/NX y no se pueden borrar o reemplazar.

Las colas stdin son privadas de 256 bytes. `feed PID TEXT` añade newline;
`feed-eof PID` marca EOF. Read bloquea sin detener shell, y NONBLOCK devuelve
EAGAIN. Exit/kill/fault cierran descripciones y cancelan consultas de red.

## ABI incremental

Se conservan las operaciones 14..25 y poll=36. Nuevas:

| Número | Operación | Registros |
| --- | --- | --- |
| 37 | exec | EBX path, ECX texto de argumentos |
| 38 | mkdir | EBX path, ECX mode |
| 39 | rmdir | EBX path |
| 40 | chdir | EBX path |
| 41 | getcwd | EBX salida, ECX capacidad; retorna 0 |
| 42 | readdir | EBX fd, ECX salida; 1 entrada, 0 EOF |

`notos_file_info` tiene 20 bytes; `notos_directory_entry` 36 bytes con inode,
tipo, tamaño y nombre[24]. Los buffers completos y permisos se validan antes
de consumir offsets o mutar estado; wire desalineado se copia por bytes.

Pruebas: `tests/vfs_unit.c`, `tests/fs_unit.c`, `tests/vfs.py`,
`tests/continuation.py` y `tests/files.py`. Consultar STORAGE.md y ELF.md.
