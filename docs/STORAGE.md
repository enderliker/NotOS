# NotFS y almacenamiento ATA

Driver ATA PIO propio de 28 bits, limitado al **disco primary-slave**. Nunca se selecciona el disco de arranque como destino de escritura. Una unidad ausente no se formatea automáticamente.

## Arranque y persistencia

`make run` conecta el disco de arranque y un disco de datos separado en
primary-slave. El lanzador crea `data.img` de 128 KiB **solo si no existe**;
no lo trunca al reiniciar ni lo formatea. Está excluido de Git y fuera de
`build/`, por lo que `make clean` conserva los datos.

```sh
make run
# Sin escritorio gráfico, por ejemplo en Codespaces:
python3 tools/run.py -display none
# Seleccionar otro archivo persistente:
NOTOS_DATA_DISK=/ruta/data.img python3 tools/run.py -display none
```

Una opción QEMU `-drive` explícita sustituye la conexión automática del disco
de datos; permite conservar el uso anterior:

```sh
python3 tools/run.py -drive if=ide,format=raw,file=build/data.img,index=1
```

`mkfs` **borra el disco de datos**. Se ejecuta explícitamente en el invitado
para preparar un disco nuevo; no es necesario repetirlo en cada arranque.
Nunca formatea ni escribe el disco de arranque. El VFS de aplicaciones sigue
abriendo `/disk` mediante snapshots de solo lectura.

## Navegación y comandos de archivos

La shell navega por los directorios fijos `/`, `/tmp`, `/disk` y `/dev`.
Empieza en `/disk` cuando se detecta un disco de datos, incluso sin formato;
si no hay disco, empieza en `/tmp`. `cd` sin argumentos va a `/`.
`pwd` muestra el directorio actual. `ls` sin argumentos lista ese directorio;
`ls PATH` lista un directorio o muestra el nombre de un archivo existente.

```text
mkfs
cd /disk
write hello.txt Hola NotOS
cat hello.txt
touch empty.txt
ls
stat hello.txt
df
cd ../tmp
write note.txt Archivo temporal
cat ./note.txt
rm note.txt
ls /
pwd
```

`write PATH TEXT` reemplaza el contenido; `touch PATH` crea un archivo vacío
si no existe y conserva el contenido si existe. `cat` lee y `rm` borra.
`stat` muestra tamaño e inode de archivos, o identifica un directorio fijo.
`df` informa asignación/cuota RAM compartida del VFS, sectores del disco y
límites de NotFS; no interpreta sectores libres como capacidad de archivos.
La acción gráfica FILES usa el mismo `ls` del directorio actual.

Las rutas de shell aceptan nombres relativos, rutas absolutas, separadores
repetidos y normalización léxica de `.`/`..`; no suben por encima de `/`.
Máximo 63 bytes de ruta normalizada. Los nombres de archivo conservan los
límites del backend: 1..23 caracteres alfanuméricos, punto, guion o underscore.
No hay directorios nuevos, `mkdir`, globbing, comillas, escapes ni pipes.
El directorio actual es estado de la shell: las aplicaciones, incluido curl,
siguen usando rutas absolutas; no se añade una syscall chdir.

NotFS mantiene su formato: ocho archivos planos de hasta 1536 bytes por
archivo. `/tmp` admite 16 nombres y archivos hasta 64 KiB con cuota compartida
RAM de 256 KiB; desaparece al reiniciar. `write` queda limitado también por
los 159 caracteres de entrada de shell. Ver [VFS.md](VFS.md).

## Formato y recuperación

Dos bancos de 33 sectores contienen snapshots versionados, con CRC32 sobre toda la imagen. Una actualización invalida el banco destino, escribe datos, FLUSH, publica su encabezado y vuelve a hacer FLUSH. El banco anterior permanece válido hasta la siguiente actualización. Montaje valida tamaños, nombres, duplicados y CRC, y selecciona la generación válida más reciente (comparación modular).

Esto detecta corrupción y permite recuperar el snapshot anterior; no prueba atomicidad en todos los discos físicos ni convierte CRC en autenticación. La consistencia depende de que el controlador respete FLUSH. Formatear invalida ambos bancos; un fallo durante format puede dejar el volumen sin formato.

`tests/storage.py` arranca y reinicia VMs reales, verifica CRUD persistente, corrompe el último snapshot y recupera el anterior, prueba capacidad, format/reboot y que la imagen de arranque no cambia. Solo probado con ATA IDE emulado en QEMU; sin SATA/AHCI, DMA, particiones ni recovery de discos físicos.


`tests/path_unit.c` verifica normalización, límites y rollback con ASAN/UBSAN.
`tests/files.py` prueba navegación, tmpfs CRUD, touch sin truncar y liberación
en 16/64 MiB; reinicia con disco para comprobar persistencia y boot intacto.
`tests/curl.py` comprueba que la shell lista y consulta una descarga binaria
real de 32768 bytes.
