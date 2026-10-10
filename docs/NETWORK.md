# Red de NotOS 0.10 (HTTP y HTTPS/TLS1.2)

## Entorno soportado

QEMU PC con `-netdev user,id=net0 -device rtl8139,netdev=net0`.

- Invitado: `10.0.2.15`.
- Gateway/NAT: `10.0.2.2`.
- Servicio DNS virtual: `10.0.2.3`, que reenvía consultas al resolver del host.

El controlador propio enumera dispositivos PCI del bus 0, encuentra RTL8139, habilita I/O y bus mastering y configura DMA. Hay cuatro buffers TX y un anillo RX de 8 KiB con espacio adicional para un paquete que cruza el límite. La recepción se sondea desde el bucle principal.

## Protocolos propios

- Ethernet y ARP para resolver el gateway y responder consultas sobre nuestra IP.
- IPv4 con validación de longitudes, checksum y dirección de destino.
- UDP con checksum y consultas DNS tipo A; se admiten nombres comprimidos al recorrer las respuestas y registros CNAME antes del A.
- TCP con handshake SYN/SYN-ACK/ACK, números de secuencia, checksums, ACK de datos y cierre FIN.
- HTTP/1.0 GET de `/`, con encabezado Host y cierre de conexión. Acepta respuestas HTTP/1.0 y 1.1 como datos TCP.

Cada conexión usa un nuevo puerto efímero para evitar colisiones con el estado de la conexión anterior en la NAT.

## Uso

```text
net
dns example.com
http example.com
http 10.0.2.2:8080
```

La salida incluye la IPv4 resuelta, estado HTTP y cantidad de bytes recibidos. `10.0.2.2:8080` permite llegar a un servidor de prueba en el host. Para probarlo manualmente, ejecuta `python3 -m http.server 8080 --bind 127.0.0.1` en otra terminal del host.

## Pruebas

`make test` inicia un servidor controlado en el host y arranca la imagen real en QEMU. Verifica siete peticiones HTTP (tres normales y cuatro con pérdidas inyectadas), una línea de estado enviada en dos partes, una respuesta mayor que el anillo RX, cierre de conexión y rechazo de hostname/puerto inválidos. No necesita internet público.

`make test-online` verifica resolución de `example.com` y respuestas HTTP y HTTPS públicas. Requiere conectividad saliente del host; incluye la prueba HTTPS de TLS.md.

`QEMU_PCAP=1 make test` guarda los paquetes de la integración en `build/network.pcap`. Los logs son `build/network.log` y `build/online.log`.

## Límites

Una solicitud simultánea, red estática de QEMU y tiempo límite por solicitud. Hay retransmisión acotada de SYN y GET. No hay retransmisión de FIN propia, ventanas TCP dinámicas, reensamblado de datos fuera de orden, fragmentación IP, DHCP, IPv6, DNS por TCP ni TLS1.3. HTTPS/TLS1.2 se documenta en TLS.md. HTTP no expone el cuerpo a aplicaciones ni dibuja páginas. Desde NotOS 0.5, los timeouts usan el reloj actualizado por IRQ0, no el antiguo contador PIT sondeado. El reloj avanza durante comandos bloqueantes, pero las solicitudes y paquetes de red solo se procesan al volver al bucle principal; el event loop sondea entre pulsos background, aunque espera mientras se ejecuta un batch o comando kernel bloqueante. Mantener IRQ desactivadas demasiado tiempo puede perder ticks.

Esto demuestra conectividad real del prototipo, no una pila de red completa o segura para producción.

## Configuración IPv4 y recuperación TCP

```text
net-config 10.0.2.16 10.0.2.2 10.0.2.3
```

IP/Gateway/DNS en ese orden; subnet fija /24. Solo en estado idle con NIC activa. Rechaza multicast, loopback, direcciones de red/broadcast, gateway igual a IP o fuera del /24 y argumentos incompletos/extras. Valida todo antes de aplicar; invalida la caché ARP. Configuración en RAM, sin DHCP ni persistencia automática. La ruta de salida usa el gateway; no hay tabla general de routing.

Una transmisión pendiente conserva su paquete Ethernet/TCP original, sin avanzar seq otra vez al reenviar. Timer inicial 500 ms, backoff exponencial y máximo cuatro retries; el timeout global de conexión/solicitud puede vencer antes. ACK parcial no descarta la transmisión; ACK fuera del límite enviado no la confirma. Aritmética serial admite wrap de 32 bits dentro de la ventana acotada. RX rechaza futuro fuera de orden y recorta prefijos ya ACKeados para no entregar bytes duplicados. No es TCP completo: sin congestion control, SACK, RTO adaptativo, reassembly fuera de orden ni TIME_WAIT completo.

Fault injection de laboratorio (una pérdida, solo armable idle):

```text
net-drop syn
net-drop request
net-drop synack
net-drop response
```

La siguiente solicitud correspondiente pierde ese paquete. El test real por NAT exige HTTP 200 y cantidad exacta de bytes después de cada pérdida, con IP alternativa 10.0.2.16; demuestra retries propios de SYN/GET y recuperación de retransmisiones del peer para RX. No sustituye tests exhaustivos de redes hostiles.

**HTTPS implementado en 0.10:** registros TLS1.2 verificados, X509 y cierre autenticado sobre TCP del invitado. Ver [TLS.md](TLS.md). No hay proxy host ni fallback a plaintext. `net-drop tamper` altera un payload después de verificación TLS para comprobar que AEAD lo rechaza.
