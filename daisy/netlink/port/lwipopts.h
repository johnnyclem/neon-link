#ifndef NEON_NETLINK_LWIPOPTS_H
#define NEON_NETLINK_LWIPOPTS_H

/* lwIP configuration for the Daisy netlink build: NO_SYS mainloop
 * polling (no RTOS), IPv4 only, DHCP client + AutoIP link-local
 * fallback + IGMP (Link discovery is multicast) + mDNS responder so
 * <device-name>.local resolves on the host that bridges us. Sized for
 * one UDP-heavy peer (Ableton Link) and one HTTP connection (the web
 * editor) on a 480 KB-of-SRAM app. */

#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_TIMERS 1

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ICMP 1
#define LWIP_IGMP 1
#define LWIP_DHCP 1
#define LWIP_AUTOIP 1
#define LWIP_DHCP_AUTOIP_COOP 1
#define LWIP_DHCP_AUTOIP_COOP_TRIES 3
#define LWIP_DNS 0

#define LWIP_UDP 1
#define LWIP_TCP 1
#define LWIP_RAW 0

/* Callback (raw) API only. */
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0

#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
/* mDNS hooks netif up/down/address changes through the ext callback
 * chain to (re)announce on its own. */
#define LWIP_NETIF_EXT_STATUS_CALLBACK 1

/* mDNS responder (answers <hostname>.local). */
#define LWIP_MDNS_RESPONDER 1
#define LWIP_NUM_NETIF_CLIENT_DATA 1
#define MDNS_MAX_SERVICES 1
/* The responder wants a decent UDP PCB count on top of ours. */
#define MEMP_NUM_UDP_PCB 8

/* All statically-declared lwIP memory — the byte heap and every memp
 * pool, pbuf pool included — lives in SDRAM: it does not fit next to
 * the app's .bss in the 128 KB DTCM, and the Seed's 64 MB chip is
 * initialized (DaisySeed::Init) long before lwip_init() touches any of
 * it. CPU-only access, so cacheability is not a hazard — no DMA engine
 * ever sees these buffers (the USB FIFO is drained by the HAL in
 * ISR-context memcpy). */
#define LWIP_DECLARE_MEMORY_ALIGNED(variable_name, size) \
  u8_t variable_name[LWIP_MEM_ALIGN_BUFFER(size)] \
      __attribute__((section(".sdram_bss")))

#define MEM_ALIGNMENT 4
#define MEM_SIZE (24 * 1024)
#define MEMP_NUM_PBUF 24
#define MEMP_NUM_TCP_PCB 4
#define MEMP_NUM_TCP_PCB_LISTEN 2
#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_SYS_TIMEOUT 12
#define PBUF_POOL_SIZE 24
#define PBUF_POOL_BUFSIZE 1536

#define TCP_MSS 1460
#define TCP_WND (4 * TCP_MSS)
#define TCP_SND_BUF (4 * TCP_MSS)
#define TCP_SND_QUEUELEN (4 * TCP_SND_BUF / TCP_MSS)
#define LWIP_TCP_KEEPALIVE 1

/* One writer, one reader, one thread: stats and asserts off for size;
 * LWIP_ASSERT stays on in debug builds via LWIP_NOASSERT. */
#define LWIP_STATS 0
#define LWIP_PROVIDE_ERRNO 1

#define LWIP_RAND() neon_netlink_rand()
#ifdef __cplusplus
extern "C" unsigned int neon_netlink_rand(void);
#else
unsigned int neon_netlink_rand(void);
#endif

/* Checksums in software (no offload on a USB pipe). Defaults are fine. */

#endif /* NEON_NETLINK_LWIPOPTS_H */
