# Red de NotOS 0.4

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

`make test` inicia un servidor controlado en el host y arranca la imagen real en QEMU. Verifica tres peticiones HTTP consecutivas, una línea de estado enviada en dos partes, una respuesta mayor que el anillo RX, cierre de conexión y rechazo de hostname/puerto inválidos. No necesita internet público.

`make test-online` verifica resolución de `example.com` y una respuesta HTTP pública. Requiere conectividad saliente del host; no equivale a una prueba de HTTPS.

`QEMU_PCAP=1 make test` guarda los paquetes de la integración en `build/network.pcap`. Los logs son `build/network.log` y `build/online.log`.

## Límites

Una solicitud simultánea, red estática de QEMU y tiempo límite por solicitud. No hay reintentos/retransmisión propios, ventanas TCP dinámicas, reensamblado de datos fuera de orden, fragmentación IP, DHCP, IPv6, DNS por TCP o TLS. HTTP no expone el cuerpo a aplicaciones ni dibuja páginas. El reloj de timeouts usa PIT sondeado y exige que el bucle principal no se bloquee más de un ciclo de ~55 ms; no es un planificador ni un reloj robusto de propósito general.

Esto demuestra conectividad real del prototipo, no una pila de red completa o segura para producción.
