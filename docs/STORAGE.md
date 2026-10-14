# NotFS v2 y almacenamiento ATA

ATA PIO de 28 bits limitado al disco primary-slave; nunca se selecciona el
disco de arranque para escribir. `make run` crea `data.img` de 128 KiB solo si
falta, fuera de build/Git; `make clean` lo conserva. `NOTOS_DATA_DISK=/ruta/data.img`
selecciona otro volumen. Una unidad ausente no se formatea automáticamente.

`mkfs` borra explícitamente el disco de datos. NotFS v2 ocupa dos bancos de
128 sectores (64 KiB cada uno): encabezado de 512 bytes, 24 entradas de 84 bytes,
relleno hasta 4096 bytes y 60 KiB de datos compactados. Entradas con path[64],
tamaño, offset, tipo, inode y permisos; admite directorios y archivos binarios.
El payload total y el archivo individual están limitados a 61440 bytes.

Cada publicación invalida el encabezado destino y FLUSH, escribe el cuerpo y
FLUSH, publica encabezado y FLUSH, vuelve a leer y valida CRC/layout/generación.
Solo entonces cambia el snapshot activo. El banco previo permanece válido.
Montaje selecciona la generación válida más reciente por comparación modular;
valida CRC, tipos, límites, padres, paths e inodes únicos. Depende del respeto de
FLUSH del dispositivo; CRC detecta corrupción, no autentica ni promete atomicidad
en todo hardware. Solo se prueba IDE emulado en QEMU; sin AHCI/DMA/particiones.

Si no hay banco v2 válido se reconocen los dos bancos v1 de 33 sectores. La
migración publica primero v2 en sectores 128..255 y lo verifica, conservando
ambos bancos anteriores (0..65) ante fallos. Un v2 válido siempre gana sobre v1.
No hay conversión inversa. Los inodes no se reutilizan durante el montaje y
mkfs conserva el contador para evitar que handles antiguos identifiquen otro
archivo; tras reboot no quedan handles vivos. Format puede dejar el disco sin
formato si falla mientras invalida bancos.

## Shell

Comienza en `/disk` cuando hay unidad de datos, aunque esté sin formato; si no,
en `/tmp`. `cd` sin argumento va a `/`, `pwd` muestra cwd y `ls [PATH]` enumera.

```text
mkfs
mkdir /disk/apps
cd /disk/apps
write hello.txt Hola NotOS
cat ./hello.txt
touch empty
stat hello.txt
rm hello.txt
rm empty
cd ..
rmdir apps
df
```

`write` reemplaza, `touch` preserva archivos existentes, `rm` elimina archivos,
`rmdir` exige directorio vacío. `df` distingue cuota RAM y payload persistente.
Las rutas se normalizan léxicamente; máximo 63 bytes y componentes de 23.
No hay globbing/comillas/escapes/pipes. Shell admite 159 caracteres para comandos
comunes y hasta 320 para curl/spawn-elf (los argumentos siguen limitados a 256).

Las aplicaciones tienen cwd privado, acceso de escritura `/disk` y syscalls
de directorio; ver VFS.md. `tests/storage.py` comprueba reboot/CRC/capacidad,
`tests/fs_unit.c` todos los puntos de fallo de publicación y migración, y
`tests/files.py` navegación con 16/64 MiB. La imagen de boot se compara intacta.
