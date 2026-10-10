#ifndef NOTOS_LWIP_OPTIONS_H
#define NOTOS_LWIP_OPTIONS_H
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_NETIF_API 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_RAW 0
#define LWIP_DNS 1
#define LWIP_DHCP 0
#define LWIP_AUTOIP 0
#define IP_FORWARD 0
#define IP_REASSEMBLY 0
#define IP_FRAG 0
#define MEM_ALIGNMENT 4
#define MEM_SIZE (64*1024)
#define MEMP_NUM_TCP_PCB 8
#define MEMP_NUM_TCP_PCB_LISTEN 0
#define MEMP_NUM_TCP_SEG 96
#define MEMP_NUM_UDP_PCB 4
#define MEMP_NUM_PBUF 32
#define PBUF_POOL_SIZE 32
#define PBUF_POOL_BUFSIZE 1600
#define TCP_MSS 1460
#define TCP_WND 8192
#define TCP_SND_BUF 8192
#define TCP_SND_QUEUELEN 32
#define TCP_QUEUE_OOSEQ 1
#define TCP_LISTEN_BACKLOG 0
#define LWIP_TCP_TIMESTAMPS 0
#define LWIP_TCP_KEEPALIVE 0
#define DNS_TABLE_SIZE 4
#define DNS_MAX_REQUESTS 4
#define DNS_MAX_NAME_LENGTH 96
#define DNS_PORT_ALLOWED(port) ((port)>=61000 && (port)<62000)
#ifdef NOTOS_DNS_PORT
#define DNS_SERVER_PORT NOTOS_DNS_PORT
#endif
#define LWIP_HOOK_TCP_ISN(local,local_port,remote,remote_port) socket_stack_random()
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 0
#endif
