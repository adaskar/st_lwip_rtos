#ifndef MONGOOSE_CONFIG_H
#define MONGOOSE_CONFIG_H

/*
 * Project Mongoose port:
 * - STM32/CMSIS-RTOS2 environment backed by FreeRTOS
 * - lwIP sockets for TCP/IP
 * - mbedTLS for TLS
 */

#define MG_ARCH MG_ARCH_CUBE
#define MG_ENABLE_FREERTOS 1

#define MG_ENABLE_LWIP 1
#define MG_ENABLE_SOCKET 1
#define MG_ENABLE_TCPIP 0
#define MG_ENABLE_TCPIP_DRIVER_INIT 0

#define MG_TLS MG_TLS_MBED
#define MG_ENABLE_CUSTOM_RANDOM 1

#define MG_ENABLE_IPV6 0
#define MG_ENABLE_POSIX_FS 0
#define MG_ENABLE_DIRLIST 0
#define MG_ENABLE_SSI 0

#ifndef MG_IO_SIZE
#define MG_IO_SIZE 1460
#endif

#endif /* MONGOOSE_CONFIG_H */
