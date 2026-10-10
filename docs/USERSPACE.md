# Ejecución en ring 3 — fase 4, primer avance

GDT propia en el kernel: código/datos supervisor (0x08/0x10), código/datos usuario (0x1b/0x23) y TSS de 32 bits (0x28). LTR carga la TSS; ESP0 apunta a una pila supervisor dedicada de 16 KiB para cambios de privilegio. El límite del mapa de I/O de la TSS queda fuera de la estructura, por lo que ring 3 no tiene permisos de acceso a puertos.

La IDT habilita únicamente la puerta 0x80 con DPL 3. El código entra a ring 3 con IRET, EFLAGS.IF=1 e IOPL=0, y registros generales limpios. Los stubs siguen conservando segmentos/registros y limpiando DF antes de entrar a C. Cuando la CPU cambia de privilegio, el frame agrega ESP/SS de usuario; no se leen esos campos para frames de ring 0.

```text
ring3-test
```

La demo copiada en una página user de solo lectura se ejecuta realmente en CPL 3. Tiene una página de datos y una pila user, separadas por una página no presente. Solicita ticks y un probe de privilegio mediante INT 0x80, espera IRQ de timer, verifica DF y el retorno de una syscall inexistente, y termina con estado 42. El probe comprueba que el handler está en la pila ESP0 de la TSS y que el frame contiene SS=0x23 y un ESP válido.

Syscalls iniciales: 0 exit, 1 ticks, 2 probe. Números desconocidos devuelven -38. No se aceptan punteros ni se expone todavía I/O desde usuario. Al salir se restaura la pila, registros y flags del llamador supervisor y se liberan las tres páginas de la demo.

Este avance usa un único directorio de páginas y solo una demo síncrona; no es aislamiento completo de procesos. Directorios CR3 privados, terminación recuperable de fallos user y validación de buffers quedan para el próximo avance. Un fallo inesperado de usuario todavía puede entrar en el panic general en esta versión intermedia.

Al ampliar la GDT, el selector 0x20 deja de ser inválido. La prueba `fault gp` pasa a usar el selector 0x30 (fuera de los seis descriptors) y ahora espera error 0x30.
