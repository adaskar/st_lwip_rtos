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
        HAL_UART_Transmit(&huart3, buf, len, 999);
    }
    return len;
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

#define DHCP_OFF                   (uint8_t) 0
#define DHCP_START                 (uint8_t) 1
#define DHCP_WAIT_ADDRESS          (uint8_t) 2
#define DHCP_ADDRESS_ASSIGNED      (uint8_t) 3
#define DHCP_TIMEOUT               (uint8_t) 4
#define DHCP_LINK_DOWN             (uint8_t) 5

#define DHCP_MAX_TRIES  4

__IO uint8_t DHCP_state = DHCP_OFF;
#endif

void ethernet_link_status_updated(struct netif *netif)
{
  if (netif_is_up(netif))
 {
#if LWIP_DHCP
    /* Update DHCP state machine */
    DHCP_state = DHCP_START;
#else
    ip_addr_t ipaddr;
    char ip_str[16];
    ip4_addr_set_u32(&ipaddr, netif_ip4_addr(netif)->addr);
    ip4addr_ntoa_r(&ipaddr, ip_str, sizeof(ip_str));
    printf("Static IPv4 address: %s\r\n", ip_str);
    // BSP_LED_On(LED_YELLOW);
    // BSP_LED_Off(LED_RED);
#endif /* LWIP_DHCP */
  }
  else
  {
#if LWIP_DHCP
    /* Update DHCP state machine */
    DHCP_state = DHCP_LINK_DOWN;
#else
    printf ("The network cable is not connected \r\n");
    // BSP_LED_Off(LED_YELLOW);
    // BSP_LED_On(LED_RED);
#endif /* LWIP_DHCP */
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

        // BSP_LED_Off(LED_YELLOW);
        // BSP_LED_Off(LED_RED);
        printf ("State: Looking for DHCP server ...\r\n");

        netifapi_dhcp_start(netif);
      }
      break;
    case DHCP_WAIT_ADDRESS:
      {
        if (dhcp_supplied_address(netif))
        {
          DHCP_state = DHCP_ADDRESS_ASSIGNED;
        //   BSP_LED_On(LED_YELLOW);
        //   BSP_LED_Off(LED_RED);

          ip4_addr_set_u32(&ipaddr, netif_ip4_addr(netif)->addr);
          ip4addr_ntoa_r(&ipaddr, ip_str, sizeof(ip_str));
          printf("IPv4 address assigned by a DHCP server: %s\n", ip_str);
        }
        else
        {
          dhcp = (struct dhcp *)netif_get_client_data(netif, LWIP_NETIF_CLIENT_DATA_INDEX_DHCP);

          /* DHCP timeout */
          if (dhcp->tries > DHCP_MAX_TRIES)
          {
            DHCP_state = DHCP_TIMEOUT;

            /* Static address used */
            IP_ADDR4(&ipaddr, 192 ,168 , 100 , 10 );
            IP_ADDR4(&netmask, 255 ,255 , 255 , 0 );
            IP_ADDR4(&gw, 192 ,168 , 100 , 1 );
            netifapi_netif_set_addr(netif, ip_2_ip4(&ipaddr), ip_2_ip4(&netmask), ip_2_ip4(&gw));

            ip4_addr_set_u32(&ipaddr, netif_ip4_addr(netif)->addr);
            ip4addr_ntoa_r(&ipaddr, ip_str, sizeof(ip_str));
            printf ("DHCP Timeout !! \r\n");
            printf("Static IPv4 address: %s\r\n", ip_str);
            // BSP_LED_On(LED_YELLOW);
            // BSP_LED_Off(LED_RED);
          }
        }
      }
      break;
  case DHCP_LINK_DOWN:
    {
      DHCP_state = DHCP_OFF;

      printf ("The network cable is not connected \r\n");
    //   BSP_LED_Off(LED_YELLOW);
    //   BSP_LED_On(LED_RED);
    }
    break;
    default: break;
    }

    /* wait 500 ms */
    osDelay(500);
  }
}
#endif  /* LWIP_DHCP */

/**
  * @brief  Initializes the lwIP stack
  * @param  None
  * @retval None
  */
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
    IP_ADDR4(&ipaddr, 192 ,168 , 100 , 10 );
    IP_ADDR4(&netmask, 255 ,255 , 255 , 0 );
    IP_ADDR4(&gw, 192 ,168 , 100 , 1 );
#endif /* LWIP_DHCP */

    /* add the network interface */
    netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &tcpip_input);

    /*  Registers the default network interface. */
    netif_set_default(&gnetif);

    ethernet_link_status_updated(&gnetif);

#if LWIP_NETIF_LINK_CALLBACK
    netif_set_link_callback(&gnetif, ethernet_link_status_updated);

    EthLinkHandle = osThreadNew(ethernet_link_thread, &gnetif, &EthLinkThread_attributes);
#endif

#if LWIP_DHCP
    /* Start DHCPClient */
    DHCPThreadHandle = osThreadNew(DHCP_Thread_Entry, &gnetif, &DHCPThread_attributes);
#endif
}


/// HTTP
osThreadId_t ostHTTP;
const osThreadAttr_t osaHTTP = {
  .name = "HTTP Server",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 1024 * 4
};

void http_server_task(void *argument)
{
    while(gnetif.ip_addr.addr == 0)
    {
        osDelay(100);
    }

    printf("Network ready\n");

    int server_fd;

    struct sockaddr_in addr;

    server_fd = socket(AF_INET,
                       SOCK_STREAM,
                       IPPROTO_TCP);

    if(server_fd < 0)
    {
        printf("Socket failed\n");

        for(;;)
            osDelay(1000);
    }

    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(server_fd,
            (struct sockaddr *)&addr,
            sizeof(addr)) < 0)
    {
        printf("Bind failed\n");

        closesocket(server_fd);

        for(;;)
            osDelay(1000);
    }

    listen(server_fd, 5);

    printf("HTTP server started\n");

    while(1)
    {
        int client;

        client = accept(server_fd,
                        NULL,
                        NULL);

        if(client >= 0)
        {
            char buffer[1024];

            int len;

            len = recv(client,
                       buffer,
                       sizeof(buffer)-1,
                       0);

            if(len > 0)
            {
                buffer[len] = 0;

                printf("%s\n", buffer);

                const char *resp =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "STM32 HTTP SERVER\r\n";

                send(client,
                     resp,
                     strlen(resp),
                     0);
            }

            closesocket(client);
        }
    }
}

/// HTTPS
osThreadId_t ostHTTPS;
const osThreadAttr_t osaHTTPS = {
  .name = "HTTPS Server",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 1024 * 8
};

#include "cert.h"
#include "key.h"

int dummy_rng(void *p_rng, unsigned char *output, size_t len)
{
    (void)p_rng;

    uint32_t random_val;

    extern RNG_HandleTypeDef hrng;

    for(size_t i = 0; i < len; i += 4)
    {
        if(HAL_RNG_GenerateRandomNumber(&hrng,
                                        &random_val) != HAL_OK)
        {
            return -1;
        }

        size_t to_copy =
            ((len - i) >= 4) ? 4 : (len - i);

        memcpy(output + i,
               &random_val,
               to_copy);
    }

    return 0;
}

static int net_send(void *ctx,
                    const unsigned char *buf,
                    size_t len)
{
    int fd = *(int *)ctx;

    return send(fd, buf, len, 0);
}

static int net_recv(void *ctx,
                    unsigned char *buf,
                    size_t len)
{
    int fd = *(int *)ctx;

    return recv(fd, buf, len, 0);
}

static void https_server_task(void *argument)
{
    (void)argument;
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;

    mbedtls_x509_crt srvcert;
    mbedtls_pk_context pkey;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    const char *pers = "stm32_https";

    char rx_buffer[1024];

    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);

    mbedtls_x509_crt_init(&srvcert);
    mbedtls_pk_init(&pkey);

    mbedtls_entropy_init(&entropy);

    extern int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);
#if !defined(MBEDTLS_ENTROPY_MIN_HARDWARE)
#define MBEDTLS_ENTROPY_MIN_HARDWARE 32
#endif
    int add_ret = mbedtls_entropy_add_source(&entropy,
                                             mbedtls_hardware_poll,
                                             NULL,
                                             MBEDTLS_ENTROPY_MIN_HARDWARE,
                                             MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (add_ret != 0)
    {
        printf("mbedtls_entropy_add_source failed: -0x%04X\n", (unsigned int)-add_ret);
        return;
    }

    mbedtls_ctr_drbg_init(&ctr_drbg);

    int seed_ret = mbedtls_ctr_drbg_seed(&ctr_drbg,
                             mbedtls_entropy_func,
                             &entropy,
                             (const unsigned char *)pers,
                             strlen(pers));
    if(seed_ret != 0)
    {
        printf("DRBG seed failed: -0x%04X\n", (unsigned int)-seed_ret);
        return;
    }

    int cert_ret = mbedtls_x509_crt_parse(
            &srvcert,
            (const unsigned char *)cert_pem,
            cert_pem_len);
    if(cert_ret != 0)
    {
        printf("Certificate parse failed: -0x%04X\n", (unsigned int)-cert_ret);
        return;
    }

    int key_ret = mbedtls_pk_parse_key(
            &pkey,
            (const unsigned char *)key_pem,
            key_pem_len,
            NULL,
            0,
            dummy_rng,
            NULL);
    if(key_ret != 0)
    {
        printf("Key parse failed: -0x%04X\n", (unsigned int)-key_ret);
        return;
    }

    if(mbedtls_ssl_config_defaults(
            &conf,
            MBEDTLS_SSL_IS_SERVER,
            MBEDTLS_SSL_TRANSPORT_STREAM,
            MBEDTLS_SSL_PRESET_DEFAULT) != 0)
    {
        printf("SSL config failed\n");
        return;
    }

    mbedtls_ssl_conf_rng(&conf,
                         dummy_rng,
                         NULL);

    if(mbedtls_ssl_conf_own_cert(
            &conf,
            &srvcert,
            &pkey) != 0)
    {
        printf("SSL own cert failed\n");
        return;
    }

    if(mbedtls_ssl_setup(&ssl,
                         &conf) != 0)
    {
        printf("SSL setup failed\n");
        return;
    }

    server_fd = socket(AF_INET,
                       SOCK_STREAM,
                       0);

    if(server_fd < 0)
    {
        printf("HTTPS socket failed\n");
        return;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(443);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(server_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) < 0)
    {
        printf("HTTPS bind failed\n");
        return;
    }

    if(listen(server_fd, 5) < 0)
    {
        printf("HTTPS listen failed\n");
        return;
    }

    printf("HTTPS server listening on port 443\n");

    while(1)
    {
        client_fd = accept(server_fd,
                           NULL,
                           NULL);

        if(client_fd < 0)
        {
            continue;
        }

        printf("HTTPS client connected\n");

        mbedtls_ssl_session_reset(&ssl);

        mbedtls_ssl_set_bio(&ssl,
                            &client_fd,
                            net_send,
                            net_recv,
                            NULL);

        int ret;

        ret = mbedtls_ssl_handshake(&ssl);

        if(ret != 0)
        {
            printf("TLS handshake failed: %d\n", ret);

            closesocket(client_fd);

            continue;
        }

        ret = mbedtls_ssl_read(
            &ssl,
            (unsigned char *)rx_buffer,
            sizeof(rx_buffer) - 1);

        if(ret > 0)
        {
            rx_buffer[ret] = 0;

            printf("HTTPS Request:\n%s\n",
                   rx_buffer);

            const char *response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Hello from STM32 HTTPS server\r\n";

            mbedtls_ssl_write(
                &ssl,
                (const unsigned char *)response,
                strlen(response));
        }

        mbedtls_ssl_close_notify(&ssl);

        closesocket(client_fd);

        printf("HTTPS client disconnected\n");
    }
}

void main_app(void *arg)
{
    (void)arg;
    
    InitLwip();

    //ostHTTP = osThreadNew(http_server_task, NULL, &osaHTTP);
    ostHTTPS = osThreadNew(https_server_task, NULL, &osaHTTPS);

    while (1) {
        osDelay(1000);
    }
    return;
}