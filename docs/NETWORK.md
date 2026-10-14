# Kernel Networking (RTL8139, IPv4, UDP/DNS, TCP/HTTP, and HTTPS)

## Supported Environment

QEMU PC with `-netdev user,id=net0 -device rtl8139,netdev=net0`:
- **Guest IP**: `10.0.2.15` (default, reconfigurable within `/24`)
- **Gateway / NAT**: `10.0.2.2`
- **Virtual DNS Forwarder**: `10.0.2.3`

The custom PCI/DMA driver (`kernel/net.c`) enumerates PCI Bus 0, locates the
Realtek RTL8139 controller, enables I/O space and bus mastering, and configures
four static TX DMA buffers and an 8 KiB RX ring buffer (with trailing wrap space
for packets crossing the ring boundary). Packet reception is polled from the
main kernel event loop.

## First-Party Kernel Protocols

- **Ethernet & ARP**: Resolves the gateway MAC address and replies to ARP
  queries for the guest's IPv4 address.
- **IPv4**: Validates header lengths, total lengths, checksums, and destination
  addresses.
- **UDP & DNS**: Computes/verifies UDP checksums and issues DNS `A` record
  queries, handling RFC 1035 label compression and `CNAME` chains preceding the
  final `A` record.
- **TCP**: Implements `SYN` / `SYN-ACK` / `ACK` handshakes, sequence/ack
  tracking, checksums, data acknowledgment, exponential backoff retransmission,
  and `FIN` teardown using fresh ephemeral source ports (`49153..51999`) per
  connection.
- **HTTP/1.0**: Sends `GET /` with `Host` and `Connection: close`, accepting
  `HTTP/1.0` and `HTTP/1.1` responses.
- **HTTPS (TLS 1.2 / 1.3)**: Runs MbedTLS 3.6.5 over the kernel TCP transport
  with X.509 certificate verification and authenticated `close_notify` (see
  [TLS.md](TLS.md)).

## Shell Commands

```text
net
dns example.com
http example.com
https example.com
http 10.0.2.2:8080
net-config 10.0.2.16 10.0.2.2 10.0.2.3
```

To test against a local host server manually, run
`python3 -m http.server 8080 --bind 127.0.0.1` on the host and execute
`http 10.0.2.2:8080` in the NotOS shell.

## Static IPv4 Configuration & TCP Recovery

`net-config <IP> <GATEWAY> <DNS>` updates the in-memory `/24` IPv4 configuration
when both the kernel network client and the user-space lwIP stack are idle. It
validates all three addresses before applying any change, flushes the ARP cache,
and rejects multicast, loopback, network/broadcast addresses, a gateway equal to
the guest IP, or a gateway outside the guest's `/24` subnet.

For in-flight kernel TCP transmissions (`kernel/tcp_retry.c`):
- Retains the original Ethernet/TCP frame so retransmissions never advance
  `SND.NXT` a second time.
- Uses a 500 ms initial retransmission timer with exponential backoff and up to
  4 retries within the overall request deadline.
- Partial ACKs do not prematurely retire the buffered frame, and out-of-window
  ACKs are ignored.
- RX drops out-of-order future segments and trims already-acknowledged prefixes
  so duplicate bytes are never delivered to the HTTP/TLS layer.

Laboratory fault-injection commands (armed when idle for the next transfer):

```text
net-drop syn
net-drop request
net-drop synack
net-drop response
net-drop tamper
```

## Coexistence with User-Space Sockets

This document covers the single-request kernel diagnostic client (`dns`, `http`,
`https`). User-space applications (`curl`, `socket-run`) use the concurrent
lwIP TCP/IPv4 and DNS stack described in [SOCKETS.md](SOCKETS.md). Both stacks
share the RTL8139 driver via disjoint ephemeral port ranges (`49153..51999` for
the kernel client, `52000..60999` for user TCP, and `61000..61999` for user DNS)
and receive broadcast ARP frames.

## Architectural Limits

- Single active request at a time on the kernel client; static `/24` routing via
  the configured gateway (no DHCP, IPv6, IP fragmentation reassembly, SACK, or
  congestion control).
- Timeouts use the `IRQ0` monotonic millisecond clock (`clock_ticks`), while
  packets are polled in the main event loop between background process pulses.
- `QEMU_PCAP=1 make test` records packet captures in `build/network.pcap`.
