# LwIP CMake configuration
# This file adds LwIP sources and includes to the main project

set(LWIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/stm32-mw-lwip)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # LwIP Core
    ${LWIP_DIR}/src/core/def.c
    ${LWIP_DIR}/src/core/dns.c
    ${LWIP_DIR}/src/core/inet_chksum.c
    ${LWIP_DIR}/src/core/init.c
    ${LWIP_DIR}/src/core/ip.c
    ${LWIP_DIR}/src/core/mem.c
    ${LWIP_DIR}/src/core/memp.c
    ${LWIP_DIR}/src/core/netif.c
    ${LWIP_DIR}/src/core/pbuf.c
    ${LWIP_DIR}/src/core/raw.c
    ${LWIP_DIR}/src/core/stats.c
    ${LWIP_DIR}/src/core/sys.c
    ${LWIP_DIR}/src/core/tcp.c
    ${LWIP_DIR}/src/core/tcp_in.c
    ${LWIP_DIR}/src/core/tcp_out.c
    ${LWIP_DIR}/src/core/timeouts.c
    ${LWIP_DIR}/src/core/udp.c

    # LwIP IPv4
    ${LWIP_DIR}/src/core/ipv4/autoip.c
    ${LWIP_DIR}/src/core/ipv4/dhcp.c
    ${LWIP_DIR}/src/core/ipv4/etharp.c
    ${LWIP_DIR}/src/core/ipv4/icmp.c
    ${LWIP_DIR}/src/core/ipv4/igmp.c
    ${LWIP_DIR}/src/core/ipv4/ip4.c
    ${LWIP_DIR}/src/core/ipv4/ip4_addr.c
    ${LWIP_DIR}/src/core/ipv4/ip4_frag.c

    # LwIP API (for RTOS)
    ${LWIP_DIR}/src/api/api_lib.c
    ${LWIP_DIR}/src/api/api_msg.c
    ${LWIP_DIR}/src/api/err.c
    ${LWIP_DIR}/src/api/if_api.c
    ${LWIP_DIR}/src/api/netbuf.c
    ${LWIP_DIR}/src/api/netdb.c
    ${LWIP_DIR}/src/api/netifapi.c
    ${LWIP_DIR}/src/api/sockets.c
    ${LWIP_DIR}/src/api/tcpip.c

    # LwIP Netif
    ${LWIP_DIR}/src/netif/ethernet.c

    # LwIP System (OS Port)
    ${LWIP_DIR}/system/OS/sys_arch.c

    # User Port
    ${CMAKE_CURRENT_SOURCE_DIR}/Target/ethernetif.c
    
    # Components
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Components/lan8742/lan8742.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${LWIP_DIR}/src/include
    ${LWIP_DIR}/system
    ${CMAKE_CURRENT_SOURCE_DIR}/Target
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Components/lan8742
)
