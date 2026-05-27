#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/*
 * Project lwIP profile:
 *
 * - STM32H573 + FreeRTOS
 * - STM32 HAL ETH target driver with zero-copy RX and driver-owned async TX
 * - Mongoose 7.x using lwIP sockets
 * - HTTPS + WebSocket dashboard on ports 80/443
 * - Static default IPv4, with runtime DHCP support available from the UI
 */

/* ========================================================= */
/* System                                                    */
/* ========================================================= */

#define NO_SYS                         0
#define SYS_LIGHTWEIGHT_PROT           1
#define LWIP_TIMERS                    1

/* ========================================================= */
/* Memory                                                    */
/* ========================================================= */

#define MEM_ALIGNMENT                  4
#define MEM_SIZE                       (48 * 1024)

/*
 * Do not define LWIP_RAM_HEAP_POINTER. Let lwIP allocate its MEM_SIZE heap as
 * a normal static object in .bss so the linker places it before _end. That
 * keeps lwIP memory naturally outside the newlib malloc/_sbrk heap.
 */

/* ========================================================= */
/* Packet buffers                                            */
/* ========================================================= */

/*
 * ethernetif.c uses a separate custom RX pool:
 *   ETH_RX_BUFFER_SIZE = 1536
 *   ETH_RX_BUFFER_CNT  = 12
 *
 * Keep PBUF_POOL_BUFSIZE at full Ethernet frame size so internally allocated
 * pbufs do not fragment normal TCP/MSS-sized traffic.
 */
#define PBUF_POOL_SIZE                 24
#define PBUF_POOL_BUFSIZE              1536
#define LWIP_SUPPORT_CUSTOM_PBUF       1

/* ========================================================= */
/* Protocol control blocks and pools                         */
/* ========================================================= */

/*
 * Mongoose uses the socket API. Size this profile from the production network
 * contract in docs/network-memory-paths.md:
 * - 4 dashboard WebSocket clients
 * - 1 OSDP TCP/TLS client
 * - bounded short-lived HTTPS/HTTP request and handshake concurrency
 */
#define MEMP_NUM_NETCONN               26
#define MEMP_NUM_NETBUF                24

#define MEMP_NUM_TCP_PCB               22
#define MEMP_NUM_TCP_PCB_LISTEN        4
#define MEMP_NUM_TCP_SEG               144

#define MEMP_NUM_UDP_PCB               6
#define MEMP_NUM_RAW_PCB               4
#define MEMP_NUM_ARP_QUEUE             8
#define MEMP_NUM_TCPIP_MSG_API         32
#define MEMP_NUM_TCPIP_MSG_INPKT       32
#define MEMP_NUM_SYS_TIMEOUT           (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 4)

/* ========================================================= */
/* Network interface                                         */
/* ========================================================= */

#define LWIP_NETIF_LINK_CALLBACK       1
#define LWIP_NETIF_API                 1

/* ========================================================= */
/* IPv4 / ARP / ICMP                                         */
/* ========================================================= */

#define LWIP_IPV4                      1
#define LWIP_ICMP                      1
#define ARP_QUEUEING                   1

/* ========================================================= */
/* TCP                                                       */
/* ========================================================= */

#define LWIP_TCP                       1
#define TCP_TTL                        255

/* Ethernet MTU 1500 - IPv4 header 20 - TCP header 20. */
#define TCP_MSS                        1460

/*
 * Moderate windows keep HTTPS responsive without letting many idle browser
 * sockets reserve desktop-sized buffers on an MCU.
 */
#define TCP_WND                        (6 * TCP_MSS)
#define TCP_SND_BUF                    (6 * TCP_MSS)
#define TCP_SND_QUEUELEN               ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / TCP_MSS)
#define TCP_OVERSIZE                   TCP_MSS

#define LWIP_TCP_KEEPALIVE             1
#define TCP_LISTEN_BACKLOG             1
#define TCP_DEFAULT_LISTEN_BACKLOG     8

/* ========================================================= */
/* UDP / DHCP / DNS                                          */
/* ========================================================= */

#define LWIP_UDP                       1
#define UDP_TTL                        255

#define LWIP_DHCP                      1
#define LWIP_DNS                       1

/* ========================================================= */
/* Socket API                                                */
/* ========================================================= */

#define LWIP_NETCONN                   1
#define LWIP_SOCKET                    1
#define LWIP_SOCKET_SELECT             1

#define LWIP_SO_RCVTIMEO               1
#define LWIP_SO_SNDTIMEO               1

#define SO_REUSE                       1
#define SO_REUSE_RXTOALL               0

/* ========================================================= */
/* Thread-safe API / core locking                            */
/* ========================================================= */

#define LWIP_TCPIP_CORE_LOCKING        1

/* ========================================================= */
/* Checksum offload                                          */
/* ========================================================= */

/*
 * Core/Src/main.c configures ETH_TX_PACKETS_FEATURES_CSUM and the STM32 ETH
 * MAC validates incoming IP/TCP/UDP checksums in hardware.
 */
#define CHECKSUM_GEN_IP                0
#define CHECKSUM_GEN_UDP               0
#define CHECKSUM_GEN_TCP               0

#define CHECKSUM_CHECK_IP              0
#define CHECKSUM_CHECK_UDP             0
#define CHECKSUM_CHECK_TCP             0

/* ICMP is small and infrequent; keep software generation enabled. */
#define CHECKSUM_GEN_ICMP              1
#define CHECKSUM_CHECK_ICMP            0

/* ========================================================= */
/* Statistics / debug                                        */
/* ========================================================= */

#define LWIP_STATS                     0

/*
 * Useful while chasing pool pressure:
 *
 * #define LWIP_STATS                   1
 * #define LWIP_STATS_DISPLAY           1
 * #define LWIP_DEBUG                   1
 * #define MEM_DEBUG                    LWIP_DBG_ON
 * #define MEMP_DEBUG                   LWIP_DBG_ON
 * #define TCP_DEBUG                    LWIP_DBG_ON
 */

/* ========================================================= */
/* RTOS sizing                                               */
/* ========================================================= */

#define TCPIP_THREAD_NAME              "TCP/IP"
#define TCPIP_THREAD_STACKSIZE         4096
#define TCPIP_THREAD_PRIO              osPriorityHigh

#define TCPIP_MBOX_SIZE                32
#define DEFAULT_UDP_RECVMBOX_SIZE      8
#define DEFAULT_TCP_RECVMBOX_SIZE      16
#define DEFAULT_ACCEPTMBOX_SIZE        12
#define DEFAULT_THREAD_STACKSIZE       4096

/* ========================================================= */
/* Multithreading diagnostics                                */
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
