#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* Pull in FreeRTOS config so macros like configMAX_PRIORITIES are visible
   to lwIP sources that include this header (e.g. for TCPIP_THREAD_PRIO). */
#include "FreeRTOSConfig.h"

/* ── System integration ───────────────────────────────────────────────────── */
#define NO_SYS                      0   /* FreeRTOS sys_arch                  */
#define LWIP_SOCKET                 0   /* use raw/netconn API, not sockets   */
#define LWIP_NETCONN                0

/* ── Memory ───────────────────────────────────────────────────────────────── */
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (12 * 1024)
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24

/* ── Protocols ────────────────────────────────────────────────────────────── */
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1   /* raw PCB for ICMP ping/traceroute   */
#define LWIP_DHCP                   1
#define LWIP_DNS                    1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_IPV6                   0

/* ── TCP tuning ───────────────────────────────────────────────────────────── */
#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * TCP_SND_BUF) / TCP_MSS)

/* ── DNS ──────────────────────────────────────────────────────────────────── */
#define LWIP_DNS_MAX_SERVERS        3
#define DNS_MAX_NAME_LENGTH         256

/* ── Callbacks & misc ─────────────────────────────────────────────────────── */
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP_MAX_NTP_SERVERS   1
#define LWIP_TIMEVAL_PRIVATE        0

/* ── Stats (disabled to save RAM) ────────────────────────────────────────── */
#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0

/* ── FreeRTOS sys_arch thread settings ───────────────────────────────────── */
#define TCPIP_THREAD_STACKSIZE      1024
#define TCPIP_THREAD_PRIO           (configMAX_PRIORITIES - 1)
#define TCPIP_MBOX_SIZE             16
#define DEFAULT_THREAD_STACKSIZE    512
#define DEFAULT_RAW_RECVMBOX_SIZE   8
#define DEFAULT_UDP_RECVMBOX_SIZE   8
#define DEFAULT_TCP_RECVMBOX_SIZE   8
#define DEFAULT_ACCEPTMBOX_SIZE     8

/* ── Debugging (set to LWIP_DBG_ON to enable) ────────────────────────────── */
#define LWIP_DEBUG                  0

#endif /* LWIPOPTS_H */
