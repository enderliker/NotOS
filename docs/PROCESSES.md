# Procesos en segundo plano

```text
spawn worker
spawn spin
spawn fault
ps
kill PID
```

`process.*` posee una tabla de cuatro slots y PIDs monotónicos no reutilizados (el agotamiento de 32 bits rechaza nuevas creaciones). El estado persiste entre iteraciones de la shell, **no entre reinicios**. La creación copia código/datos al espacio privado ring 3.

El planificador puede reanudar un contexto durante un pulso: al vencer el quantum de cinco muestras IRQ0 user, al yield o al terminar, recupera el frame kernel del event loop. El gestor rota slots READY y reanuda una tarea por iteración; red, serie, teclado y ratón pueden procesarse entre pulsos. Una syscall sigue siendo no preemptiva. Comandos kernel bloqueantes pueden demorar los procesos; no es un kernel preemptivo.

`kill` opera desde el loop, nunca destruye un contexto ejecutándose. Salida normal, fallo y presupuesto se reportan y recolectan automáticamente: frames y pila kernel se liberan fuera del handler. No hay zombies, pero tampoco almacenamiento persistente de resultados de procesos ya recolectados, espera/join, IPC, prioridades, bloqueo/wakeup ni PID concurrente por múltiples CPUs.

Las demos batch de fase 5 siguen siendo síncronas. Los procesos background esperan mientras se ejecuta un batch o comando kernel bloqueante. `process_spawn_image` es la interfaz usada por el cargador de la VM NotLang; no acepta imágenes mayores de una página.

Pruebas reales: cuatro procesos, límite de slots, shell viva, kill, auto-reaping, fallo de uno y progreso de otro, restauración exacta de heap/frames y convivencia con batches. HTTP por NAT se prueba en la misma VM mientras cuatro spins background están activos. QEMU únicamente.
