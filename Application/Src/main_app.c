#include "main.h"
#include <stdio.h>
#include <string.h>
#include "mbedtls.h"

#include "ethernetif.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "lwip/netifapi.h"
#include "lwip/sockets.h"

extern UART_HandleTypeDef huart3;

int _write(int fd, unsigned char *buf, int len)
{
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, 1);
    if (fd == 1 || fd == 2)
    {
        HAL_UART_Transmit(&huart3, buf, len, 1000);
    }
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
    return len;
}

int _gettimeofday(struct timeval *tv, void *tzvp)
{
    (void)tzvp;
    if (tv != NULL)
    {
        uint32_t tick = HAL_GetTick();
        tv->tv_sec  = tick / 1000;
        tv->tv_usec = (tick % 1000) * 1000;
    }
    return 0;
}

struct netif gnetif;
osThreadId_t EthLinkHandle;
const osThreadAttr_t EthLinkThread_attributes = {
  .name = "Eth Link",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 1024 * 4
};
osThreadId_t DHCPThreadHandle;
const osThreadAttr_t DHCPThread_attributes = {
  .name = "DHCP Thread",
  .priority = (osPriority_t) osPriorityBelowNormal,
  .stack_size = 1024 * 4
};

#undef LWIP_DHCP

#if LWIP_DHCP

#define DHCP_OFF              (uint8_t) 0
#define DHCP_START            (uint8_t) 1
#define DHCP_WAIT_ADDRESS     (uint8_t) 2
#define DHCP_ADDRESS_ASSIGNED (uint8_t) 3
#define DHCP_TIMEOUT          (uint8_t) 4
#define DHCP_LINK_DOWN        (uint8_t) 5

#define DHCP_MAX_TRIES  4

__IO uint8_t DHCP_state = DHCP_OFF;
#endif

void ethernet_link_status_updated(struct netif *netif)
{
    if (netif_is_up(netif))
    {
#if LWIP_DHCP
        DHCP_state = DHCP_START;
#else
        ip_addr_t ipaddr;
        char ip_str[16];
        ip4_addr_set_u32(&ipaddr, netif_ip4_addr(netif)->addr);
        ip4addr_ntoa_r(&ipaddr, ip_str, sizeof(ip_str));
        printf("Static IPv4 address: %s\r\n", ip_str);
#endif
    }
    else
    {
#if LWIP_DHCP
        DHCP_state = DHCP_LINK_DOWN;
#else
        printf("The network cable is not connected\r\n");
#endif
    }
}

#if LWIP_DHCP
void DHCP_Thread_Entry(void *argument)
{
    struct netif *netif = (struct netif *) argument;
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gw;
    struct dhcp *dhcp;
    char ip_str[16];

    for (;;)
    {
        switch (DHCP_state)
        {
        case DHCP_START:
        {
            ip_addr_set_zero_ip4(&netif->ip_addr);
            ip_addr_set_zero_ip4(&netif->netmask);
            ip_addr_set_zero_ip4(&netif->gw);
            DHCP_state = DHCP_WAIT_ADDRESS;
            printf("State: Looking for DHCP server ...\r\n");
            netifapi_dhcp_start(netif);
            break;
        }
        case DHCP_WAIT_ADDRESS:
        {
            if (dhcp_supplied_address(netif))
            {
                DHCP_state = DHCP_ADDRESS_ASSIGNED;
                ip4_addr_set_u32(&ipaddr, netif_ip4_addr(netif)->addr);
                ip4addr_ntoa_r(&ipaddr, ip_str, sizeof(ip_str));
                printf("IPv4 address assigned by DHCP: %s\n", ip_str);
            }
            else
            {
                dhcp = (struct dhcp *)netif_get_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP);
                if (dhcp->tries > DHCP_MAX_TRIES)
                {
                    DHCP_state = DHCP_TIMEOUT;
                    IP_ADDR4(&ipaddr,  192, 168, 100, 10);
                    IP_ADDR4(&netmask, 255, 255, 255,  0);
                    IP_ADDR4(&gw,      192, 168, 100,  1);
                    netifapi_netif_set_addr(netif, ip_2_ip4(&ipaddr), ip_2_ip4(&netmask), ip_2_ip4(&gw));
                    ip4_addr_set_u32(&ipaddr, netif_ip4_addr(netif)->addr);
                    ip4addr_ntoa_r(&ipaddr, ip_str, sizeof(ip_str));
                    printf("DHCP Timeout!! Static IPv4: %s\r\n", ip_str);
                }
            }
            break;
        }
        case DHCP_LINK_DOWN:
        {
            DHCP_state = DHCP_OFF;
            printf("The network cable is not connected\r\n");
            break;
        }
        default: break;
        }
        osDelay(500);
    }
}
#endif

static void InitLwip(void)
{
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gw;

    tcpip_init(NULL, NULL);

#if LWIP_DHCP
    ip_addr_set_zero_ip4(&ipaddr);
    ip_addr_set_zero_ip4(&netmask);
    ip_addr_set_zero_ip4(&gw);
#else
    IP_ADDR4(&ipaddr,  192, 168, 100, 10);
    IP_ADDR4(&netmask, 255, 255, 255,  0);
    IP_ADDR4(&gw,      192, 168, 100,  1);
#endif

    netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &tcpip_input);
    netif_set_default(&gnetif);
    netif_set_up(&gnetif);
    if (netif_is_link_up(&gnetif))
        netif_set_link_up(&gnetif);

    ethernet_link_status_updated(&gnetif);

#if LWIP_NETIF_LINK_CALLBACK
    netif_set_link_callback(&gnetif, ethernet_link_status_updated);
    EthLinkHandle = osThreadNew(ethernet_link_thread, &gnetif, &EthLinkThread_attributes);
#endif

#if LWIP_DHCP
    DHCPThreadHandle = osThreadNew(DHCP_Thread_Entry, &gnetif, &DHCPThread_attributes);
#endif
}

/* ---------------------------------------------------------------------------
 * Plain HTTP server (port 80) — unchanged, kept for reference
 * ---------------------------------------------------------------------------*/
osThreadId_t ostHTTP;
const osThreadAttr_t osaHTTP = {
  .name = "HTTP Server",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 1024 * 4
};

void http_server_task(void *argument)
{
    while (gnetif.ip_addr.addr == 0)
        osDelay(100);

    printf("Network ready\n");

    int server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd < 0)
    {
        printf("Socket failed\n");
        for (;;) osDelay(1000);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(80);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        printf("Bind failed\n");
        closesocket(server_fd);
        for (;;) osDelay(1000);
    }

    listen(server_fd, 5);
    printf("HTTP server started\n");

    while (1)
    {
        int client = accept(server_fd, NULL, NULL);
        if (client >= 0)
        {
            char buffer[1024];
            int len = recv(client, buffer, sizeof(buffer) - 1, 0);
            if (len > 0)
            {
                buffer[len] = 0;
                printf("%s\n", buffer);
                const char *resp =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "STM32 HTTP SERVER\r\n";
                send(client, resp, strlen(resp), 0);
            }
            closesocket(client);
        }
    }
}

/* ---------------------------------------------------------------------------
 * HTTPS server (port 443)
 * ---------------------------------------------------------------------------*/
osThreadId_t ostHTTPS;
const osThreadAttr_t osaHTTPS = {
  .name = "HTTPS Server",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 1024 * 32
};

#include "cert.h"
#include "key.h"
#include "mbedtls/debug.h"

/* ---- FIX 3: mbedtls contexts as file-static, NOT on the task stack --------
 *
 * mbedtls_ssl_context alone is several hundred bytes; together with the
 * config, certs, RSA key and entropy objects they easily exceed a few KB.
 * More critically, during RSA-2048 decryption (ClientKeyExchange) mbedtls
 * needs substantial additional call-stack depth.  Keeping all of these on
 * the task stack risks silent stack overflow that corrupts session-key
 * material and causes exactly the MAC failure observed in ssl_decrypt_buf.
 * --------------------------------------------------------------------- */
static mbedtls_ssl_context    ssl;
static mbedtls_ssl_config     conf;
static mbedtls_x509_crt       srvcert;
static mbedtls_pk_context     pkey;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;

static void my_debug(void *ctx, int level,
                     const char *file, int line,
                     const char *str)
{
    (void)ctx;
    (void)level;
    printf("%s:%04d: %s", file, line, str);
}

/* Error code aliases (not always exposed in lwIP build) */
#ifndef MBEDTLS_ERR_NET_CONN_RESET
#define MBEDTLS_ERR_NET_CONN_RESET  -0x0050
#endif
#ifndef MBEDTLS_ERR_NET_SEND_FAILED
#define MBEDTLS_ERR_NET_SEND_FAILED -0x004E
#endif
#ifndef MBEDTLS_ERR_NET_RECV_FAILED
#define MBEDTLS_ERR_NET_RECV_FAILED -0x004C
#endif

static int net_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd  = *(int *)ctx;
    int ret = send(fd, buf, len, 0);
    if (ret < 0)
    {
        int err = errno;
        printf("[net_send] send failed: ret=%d errno=%d\n", ret, err);
        if (err == EWOULDBLOCK || err == EAGAIN)
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        if (err == EPIPE || err == ECONNRESET)
            return MBEDTLS_ERR_NET_CONN_RESET;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return ret;
}

static int net_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd  = *(int *)ctx;
    int ret = recv(fd, buf, len, 0);
    if (ret < 0)
    {
        int err = errno;
        printf("[net_recv] recv failed: ret=%d errno=%d\n", ret, err);
        if (err == EWOULDBLOCK || err == EAGAIN)
            return MBEDTLS_ERR_SSL_WANT_READ;
        if (err == EPIPE || err == ECONNRESET)
            return MBEDTLS_ERR_NET_CONN_RESET;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    /* ---- FIX 2: ret == 0 means the peer closed its write side (EOF). ------
     *
     * Returning 0 to mbedtls is interpreted as "no data yet", not "closed".
     * This can corrupt the handshake state machine and cause ssl_decrypt_buf
     * to fail with a MAC error on the very next encrypted record it tries to
     * read (e.g. the client's Finished message).
     * -------------------------------------------------------------------- */
    if (ret == 0)
    {
        printf("[net_recv] connection closed by peer (EOF)\n");
        return MBEDTLS_ERR_NET_CONN_RESET;
    }

    return ret;
}

static void https_server_task(void *argument)
{
    (void)argument;

    int server_fd;
    int client_fd;
    struct sockaddr_in server_addr;

    /* -----------------------------------------------------------------------
     * One-time mbedtls initialisation (run once before the accept loop)
     * -------------------------------------------------------------------- */
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&srvcert);
    mbedtls_pk_init(&pkey);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    /* --- Entropy / DRBG -------------------------------------------------- */
    extern int mbedtls_hardware_poll(void *data, unsigned char *output,
                                     size_t len, size_t *olen);
#if !defined(MBEDTLS_ENTROPY_MIN_HARDWARE)
#define MBEDTLS_ENTROPY_MIN_HARDWARE 32
#endif
    int ret = mbedtls_entropy_add_source(&entropy,
                                         mbedtls_hardware_poll,
                                         NULL,
                                         MBEDTLS_ENTROPY_MIN_HARDWARE,
                                         MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (ret != 0)
    {
        printf("entropy_add_source failed: -0x%04X\n", (unsigned int)-ret);
        return;
    }

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg,
                                 mbedtls_entropy_func,
                                 &entropy,
                                 (const unsigned char *)"stm32_https",
                                 11);
    if (ret != 0)
    {
        printf("DRBG seed failed: -0x%04X\n", (unsigned int)-ret);
        return;
    }

    /* --- Certificate & key ----------------------------------------------- */
    ret = mbedtls_x509_crt_parse(&srvcert,
                                  (const unsigned char *)cert_pem,
                                  cert_pem_len);
    if (ret != 0)
    {
        printf("Certificate parse failed: -0x%04X\n", (unsigned int)-ret);
        return;
    }

    ret = mbedtls_pk_parse_key(&pkey,
                                (const unsigned char *)key_pem,
                                key_pem_len,
                                NULL, 0,
                                mbedtls_ctr_drbg_random,
                                &ctr_drbg);
    if (ret != 0)
    {
        printf("Key parse failed: -0x%04X\n", (unsigned int)-ret);
        return;
    }

    /* ---- FIX 1: call config_defaults FIRST, then apply all settings. ------
     *
     * mbedtls_ssl_config_defaults() resets the conf struct to preset values,
     * overwriting anything written before it.  Previously authmode was set
     * on a zero-initialised struct and then silently discarded when
     * config_defaults ran.  Now every option is set in the correct order.
     * -------------------------------------------------------------------- */
    ret = mbedtls_ssl_config_defaults(&conf,
                                       MBEDTLS_SSL_IS_SERVER,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
    {
        printf("ssl_config_defaults failed: -0x%04X\n", (unsigned int)-ret);
        return;
    }

    /* Now set options — everything here is applied AFTER defaults */
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);

    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    /* Set threshold to 1 during debugging to see handshake progress.
     * Set to 0 in production to silence the output.                   */
    // mbedtls_debug_set_threshold(1);
    // mbedtls_ssl_conf_dbg(&conf, my_debug, NULL);

    ret = mbedtls_ssl_conf_own_cert(&conf, &srvcert, &pkey);
    if (ret != 0)
    {
        printf("ssl_conf_own_cert failed: -0x%04X\n", (unsigned int)-ret);
        return;
    }

    ret = mbedtls_ssl_setup(&ssl, &conf);
    if (ret != 0)
    {
        printf("ssl_setup failed: -0x%04X\n", (unsigned int)-ret);
        return;
    }

    /* --- Listening socket ------------------------------------------------- */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        printf("HTTPS socket failed\n");
        return;
    }

    /* ---- FIX 4: SO_REUSEADDR so bind succeeds after a device reset ------*/
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(443);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("HTTPS bind failed\n");
        closesocket(server_fd);
        return;
    }

    if (listen(server_fd, 5) < 0)
    {
        printf("HTTPS listen failed\n");
        closesocket(server_fd);
        return;
    }

    printf("HTTPS server listening on port 443\n");

    /* -----------------------------------------------------------------------
     * Accept loop
     * -------------------------------------------------------------------- */
    while (1)
    {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
            continue;

        printf("HTTPS client connected\n");

        /* ---- FIX 4 (cont): socket receive timeout -----------------------
         *
         * Without a timeout, a client that connects but goes silent stalls
         * this task indefinitely inside mbedtls_ssl_read / net_recv.
         * ---------------------------------------------------------------- */
        struct timeval tv_timeout = { .tv_sec = 10, .tv_usec = 0 };
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO,
                   &tv_timeout, sizeof(tv_timeout));

        /* Reset the ssl context for this new connection */
        mbedtls_ssl_session_reset(&ssl);

        mbedtls_ssl_set_bio(&ssl,
                            &client_fd,
                            net_send,
                            net_recv,
                            NULL);

        /* TLS handshake */
        do {
            ret = mbedtls_ssl_handshake(&ssl);
        } while (ret == MBEDTLS_ERR_SSL_WANT_READ  ||
                 ret == MBEDTLS_ERR_SSL_WANT_WRITE);

        if (ret != 0)
        {
            printf("TLS handshake failed: -0x%04X\n", (unsigned int)-ret);
            mbedtls_ssl_close_notify(&ssl);
            closesocket(client_fd);
            continue;
        }

        printf("TLS handshake OK  cipher: %s\n",
               mbedtls_ssl_get_ciphersuite(&ssl));

        /* Read HTTP request */
        char rx_buffer[1024];
        ret = mbedtls_ssl_read(&ssl,
                               (unsigned char *)rx_buffer,
                               sizeof(rx_buffer) - 1);
        if (ret > 0)
        {
            rx_buffer[ret] = '\0';
            printf("HTTPS Request:\n%s\n", rx_buffer);

            const char *response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Hello from STM32 HTTPS server\r\n";

            /* ---- FIX 5: loop mbedtls_ssl_write until all bytes are sent --
             *
             * mbedtls_ssl_write may return WANT_WRITE or a partial length.
             * Without a loop the HTTP response can be silently truncated.
             * ------------------------------------------------------------ */
            const unsigned char *p   = (const unsigned char *)response;
            size_t               left = strlen(response);
            while (left > 0)
            {
                ret = mbedtls_ssl_write(&ssl, p, left);
                if (ret == MBEDTLS_ERR_SSL_WANT_WRITE)
                    continue;
                if (ret <= 0)
                {
                    printf("ssl_write failed: -0x%04X\n", (unsigned int)-ret);
                    break;
                }
                p    += ret;
                left -= ret;
            }
        }
        else if (ret == 0)
        {
            printf("Connection closed by client before request\n");
        }
        else
        {
            printf("ssl_read failed: -0x%04X\n", (unsigned int)-ret);
        }

        mbedtls_ssl_close_notify(&ssl);
        closesocket(client_fd);
        printf("HTTPS client disconnected\n");
    }
}

/* ---------------------------------------------------------------------------
 * Application entry point
 * ---------------------------------------------------------------------------*/
void main_app(void *arg)
{
    (void)arg;

    InitLwip();

    unsigned char hash[32];
    mbedtls_sha256_context test_ctx;
    mbedtls_sha256_init( &test_ctx );
    mbedtls_sha256_starts( &test_ctx, 0 );
    mbedtls_sha256_update( &test_ctx, (const unsigned char*)"abc", 3 );
    mbedtls_sha256_finish( &test_ctx, hash );
    mbedtls_sha256_free( &test_ctx );
    printf("SHA256: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n", 
           hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7], 
           hash[8], hash[9], hash[10], hash[11], hash[12], hash[13], hash[14], hash[15], 
           hash[16], hash[17], hash[18], hash[19], hash[20], hash[21], hash[22], hash[23], 
           hash[24], hash[25], hash[26], hash[27], hash[28], hash[29], hash[30], hash[31]);

    /* ostHTTP  = osThreadNew(http_server_task,  NULL, &osaHTTP);  */
    ostHTTPS = osThreadNew(https_server_task, NULL, &osaHTTPS);

    while (1)
        osDelay(1000);
}