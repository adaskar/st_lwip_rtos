#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/*
 * Optimized lwIP configuration for:
 *
 * STM32 + FreeRTOS + LwIP + mbedTLS HTTPS server
 *
 * Suitable for:
 * - HTTP
 * - HTTPS (TLS 1.2)
 * - REST APIs
 * - Websocket
 * - MQTT
 *
 * Recommended for STM32H5/H7/F7.
 */

/* ========================================================= */
/* ================= SYSTEM OPTIONS ========================= */
/* ========================================================= */

#define NO_SYS                         0
#define SYS_LIGHTWEIGHT_PROT           1

/* ========================================================= */
/* ================= MEMORY OPTIONS ========================= */
/* ========================================================= */

#define MEM_ALIGNMENT                  4

/*
 * VERY IMPORTANT:
 * HTTPS + TLS requires large memory.
 *
 * 64KB is a good starting point.
 */
#define MEM_SIZE                       (64 * 1024)

/*
 * Make sure this region:
 *
 * - exists
 * - does not overlap ETH DMA
 * - is DMA-safe
 * - cache coherent
 */
#define LWIP_RAM_HEAP_POINTER          ((void *)0x20084000)

/* ========================================================= */
/* ================= PBUF OPTIONS =========================== */
/* ========================================================= */

#define PBUF_POOL_SIZE                 64

/*
 * 1536 = enough for full Ethernet frame.
 */
#define PBUF_POOL_BUFSIZE              1536

/*
 * Zero-copy RX support.
 */
#define LWIP_SUPPORT_CUSTOM_PBUF       1

/* ========================================================= */
/* ================= MEMP OPTIONS =========================== */
/* ========================================================= */

#define MEMP_NUM_TCP_PCB               10

/*
 * HTTPS requires more queued TCP segments.
 */
#define MEMP_NUM_TCP_SEG               64

/* ========================================================= */
/* ================= NETIF OPTIONS ========================== */
/* ========================================================= */

#define LWIP_NETIF_LINK_CALLBACK       1

/* ========================================================= */
/* ================= TCP OPTIONS ============================ */
/* ========================================================= */

#define LWIP_TCP                       1

#define TCP_TTL                        255

/*
 * Ethernet MTU 1500
 * IP header 20
 * TCP header 20
 */
#define TCP_MSS                        1460

/*
 * Larger windows improve TLS throughput.
 */
#define TCP_WND                        (8 * TCP_MSS)

#define TCP_SND_BUF                    (8 * TCP_MSS)

/*
 * Keepalive support.
 */
#define LWIP_TCP_KEEPALIVE             1

/* ========================================================= */
/* ================= UDP OPTIONS ============================ */
/* ========================================================= */

#define LWIP_UDP                       1
#define UDP_TTL                        255

/* ========================================================= */
/* ================= ICMP OPTIONS =========================== */
/* ========================================================= */

#define LWIP_ICMP                      1

/* ========================================================= */
/* ================= DHCP OPTIONS =========================== */
/* ========================================================= */

#define LWIP_DHCP                      1

/* ========================================================= */
/* ================= DNS OPTIONS ============================ */
/* ========================================================= */

#define LWIP_DNS                       1

/* ========================================================= */
/* ================= ARP OPTIONS ============================ */
/* ========================================================= */

#define ARP_QUEUEING                   1

/* ========================================================= */
/* ================= SOCKET OPTIONS ========================= */
/* ========================================================= */

#define LWIP_NETCONN                   0
#define LWIP_SOCKET                    1

/*
 * select()
 */
#define LWIP_SOCKET_SELECT             1

/*
 * SO_RCVTIMEO / SO_SNDTIMEO
 */
#define LWIP_SO_RCVTIMEO               1
#define LWIP_SO_SNDTIMEO               1

/*
 * SO_REUSEADDR
 */
#define SO_REUSE                       1
#define SO_REUSE_RXTOALL               1

/*
 * errno support
 */
//#define LWIP_PROVIDE_ERRNO             1

/* ========================================================= */
/* ================= NETIF API ============================== */
/* ========================================================= */

#define LWIP_NETIF_API                 1

/* ========================================================= */
/* ================= CORE LOCKING =========================== */
/* ========================================================= */

#define LWIP_TCPIP_CORE_LOCKING        1

/* ========================================================= */
/* ================= CHECKSUM OPTIONS ======================= */
/* ========================================================= */

/*
 * STM32 ETH hardware checksum offload.
 */
#define CHECKSUM_BY_HARDWARE

#ifdef CHECKSUM_BY_HARDWARE

#define CHECKSUM_GEN_IP                0
#define CHECKSUM_GEN_UDP               0
#define CHECKSUM_GEN_TCP               0

#define CHECKSUM_CHECK_IP              0
#define CHECKSUM_CHECK_UDP             0
#define CHECKSUM_CHECK_TCP             0

#define CHECKSUM_GEN_ICMP              1
#define CHECKSUM_CHECK_ICMP            0

#else

#define CHECKSUM_GEN_IP                1
#define CHECKSUM_GEN_UDP               1
#define CHECKSUM_GEN_TCP               1

#define CHECKSUM_CHECK_IP              1
#define CHECKSUM_CHECK_UDP             1
#define CHECKSUM_CHECK_TCP             1

#define CHECKSUM_GEN_ICMP              1
#define CHECKSUM_CHECK_ICMP            1

#endif

/* ========================================================= */
/* ================= HTTPD OPTIONS ========================== */
/* ========================================================= */

#define HTTPD_USE_CUSTOM_FSDATA        1

/*
 * HTTP/1.1 keepalive
 */
#define LWIP_HTTPD_SUPPORT_11_KEEPALIVE 1

/* ========================================================= */
/* ================= DEBUG OPTIONS ========================== */
/* ========================================================= */

/*
 * Disable in production.
 */
#define LWIP_STATS                     0

/*
 * Enable only while debugging.
 */
// #define LWIP_DEBUG                   1
// #define MEM_DEBUG                    LWIP_DBG_ON
// #define MEMP_DEBUG                   LWIP_DBG_ON
// #define TCP_DEBUG                    LWIP_DBG_ON
// #define ETHARP_DEBUG                 LWIP_DBG_ON

/* ========================================================= */
/* ================= OS OPTIONS ============================= */
/* ========================================================= */

#define TCPIP_THREAD_NAME              "TCP/IP"

/*
 * HTTPS needs larger stack.
 */
#define TCPIP_THREAD_STACKSIZE         4096

/*
 * Larger mailbox sizes prevent congestion.
 */
#define TCPIP_MBOX_SIZE                16

#define DEFAULT_UDP_RECVMBOX_SIZE      16
#define DEFAULT_TCP_RECVMBOX_SIZE      16
#define DEFAULT_ACCEPTMBOX_SIZE        16

/*
 * Default worker stack.
 */
#define DEFAULT_THREAD_STACKSIZE       4096

/*
 * High priority for networking.
 */
#define TCPIP_THREAD_PRIO              osPriorityHigh

/* ========================================================= */
/* ================= MULTITHREAD CHECK ====================== */
/* ========================================================= */

#define LWIP_CHECK_MULTITHREADING      0

#if LWIP_CHECK_MULTITHREADING

#define LWIP_MARK_TCPIP_THREAD()       sys_mark_tcpip_thread()
void sys_mark_tcpip_thread(void);

#define LWIP_ASSERT_CORE_LOCKED()      sys_check_core_locking()
void sys_check_core_locking(void);

#if LWIP_TCPIP_CORE_LOCKING

#define LOCK_TCPIP_CORE()              sys_lock_tcpip_core()
void sys_lock_tcpip_core(void);

#define UNLOCK_TCPIP_CORE()            sys_unlock_tcpip_core()
void sys_unlock_tcpip_core(void);

#endif
#endif

#endif /* __LWIPOPTS_H__ */