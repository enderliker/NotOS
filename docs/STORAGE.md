# NotFS y almacenamiento ATA

Driver ATA PIO propio de 28 bits, limitado al **disco primary-slave**. Nunca se selecciona el disco de arranque como destino de escritura. Una unidad ausente no se formatea automáticamente.

Para arrancar con persistencia:

```sh
truncate -s 128K build/data.img
qemu-system-i386 -drive format=raw,file=build/notos.img,index=0 \
  -drive format=raw,file=build/data.img,index=1 -serial stdio \
  -netdev user,id=net0 -device rtl8139,netdev=net0
```

Ejecutar `mkfs` **borra el disco de datos**. No volver a crear/truncar data.img al reiniciar si se quieren conservar archivos.

```text
mkfs
write hello.txt Hola NotOS
cat hello.txt
ls
rm hello.txt
```

Formato propio, sin FAT/Linux: ocho archivos planos, nombres de 1..23 caracteres, datos de hasta 1536 bytes por archivo. Sin directorios, permisos ni fechas. El comando write está además limitado por los 159 caracteres de entrada de shell.

Dos bancos de 33 sectores contienen snapshots versionados, con CRC32 sobre toda la imagen. Una actualización invalida el banco destino, escribe datos, FLUSH, publica su encabezado y vuelve a hacer FLUSH. El banco anterior permanece válido hasta la siguiente actualización. Montaje valida tamaños, nombres, duplicados y CRC, y selecciona la generación válida más reciente (comparación modular).

Esto detecta corrupción y permite recuperar el snapshot anterior; no prueba atomicidad en todos los discos físicos ni convierte CRC en autenticación. La consistencia depende de que el controlador respete FLUSH. Formatear invalida ambos bancos; un fallo durante format puede dejar el volumen sin formato.

`tests/storage.py` arranca y reinicia VMs reales, verifica CRUD persistente, corrompe el último snapshot y recupera el anterior, prueba capacidad, format/reboot y que la imagen de arranque no cambia. Solo probado con ATA IDE emulado en QEMU; sin SATA/AHCI, DMA, particiones ni recovery de discos físicos.
