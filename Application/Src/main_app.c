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


osThreadId_t ostHTTP;
const osThreadAttr_t osaHTTP = {
  .name = "HTTP Server",
  .priority = (osPriority_t) osPriorityNormal,
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

void main_app(void *arg)
{
    (void)arg;
    int ret = 0;
    int v = 1; /* v=1 for verbose mode */
    
    InitLwip();

    ostHTTP = osThreadNew(http_server_task, NULL, &osaHTTP);

    while (1) {
        osDelay(1000);
    }
    return;
}