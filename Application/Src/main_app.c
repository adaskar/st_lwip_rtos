#include "main.h"
#include <stdio.h>
#include <string.h>

#include "cert.h"
#include "dashboard_html.h"
#include "ethernetif.h"
#include "key.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"
#include "lwip/netifapi.h"
#include "mongoose.h"

extern UART_HandleTypeDef huart3;
extern RNG_HandleTypeDef hrng;

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
 * HTTPS server (port 443), backed by Mongoose + lwIP sockets + mbedTLS
 * ---------------------------------------------------------------------------*/
osThreadId_t ostHTTPS;
const osThreadAttr_t osaHTTPS = {
  .name = "Mongoose",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 1024 * 6
};

typedef struct
{
    const char *name;
    const char *pin_name;
    GPIO_TypeDef *port;
    uint16_t pin;
} output_t;

static const output_t outputs[] = {
    { "Output 0", "PB4",  m_OUT_0_GPIO_Port, m_OUT_0_Pin },
    { "Output 1", "PA15", m_OUT_1_GPIO_Port, m_OUT_1_Pin },
};

bool mg_random(void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;

    while (len > 0)
    {
        uint32_t random_word;
        size_t chunk;

        if (HAL_RNG_GenerateRandomNumber(&hrng, &random_word) != HAL_OK)
            return false;

        chunk = len < sizeof(random_word) ? len : sizeof(random_word);
        memcpy(p, &random_word, chunk);
        p += chunk;
        len -= chunk;
    }

    return true;
}

static uint8_t output_get(size_t id)
{
    return HAL_GPIO_ReadPin(outputs[id].port, outputs[id].pin) == GPIO_PIN_SET;
}

static void output_set(size_t id, bool on)
{
    HAL_GPIO_WritePin(outputs[id].port,
                      outputs[id].pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t input_get(void)
{
    return HAL_GPIO_ReadPin(m_IN_3_GPIO_Port, m_IN_3_Pin) == GPIO_PIN_SET;
}

static uint32_t uptime_seconds(void)
{
    return HAL_GetTick() / 1000U;
}

static size_t make_state_json(char *buf, size_t len)
{
    return (size_t)snprintf(buf,
                            len,
                            "{"
                            "\"uptime\":%lu,"
                            "\"input\":{\"name\":\"m_IN_3\",\"pin\":\"PA3\",\"active\":%s},"
                            "\"outputs\":["
                            "{\"id\":0,\"name\":\"%s\",\"pin\":\"%s\",\"on\":%s},"
                            "{\"id\":1,\"name\":\"%s\",\"pin\":\"%s\",\"on\":%s}"
                            "]"
                            "}",
                            (unsigned long)uptime_seconds(),
                            input_get() ? "true" : "false",
                            outputs[0].name,
                            outputs[0].pin_name,
                            output_get(0) ? "true" : "false",
                            outputs[1].name,
                            outputs[1].pin_name,
                            output_get(1) ? "true" : "false");
}

static void reply_state(struct mg_connection *c)
{
    char json[256];
    make_state_json(json, sizeof(json));
    mg_http_reply(c,
                  200,
                  "Content-Type: application/json\r\n"
                  "Cache-Control: no-store\r\n",
                  "%s",
                  json);
}

static void broadcast_state(struct mg_mgr *mgr)
{
    char json[256];
    size_t len = make_state_json(json, sizeof(json));
    struct mg_connection *conn;

    for (conn = mgr->conns; conn != NULL; conn = conn->next)
    {
        if (conn->is_websocket)
            mg_ws_send(conn, json, len, WEBSOCKET_OP_TEXT);
    }
}

static void handle_output_update(struct mg_connection *c,
                                 struct mg_http_message *hm,
                                 struct mg_mgr *mgr)
{
    long id = mg_json_get_long(hm->body, "$.id", -1);
    bool on = false;

    if (id < 0 || id >= (long)(sizeof(outputs) / sizeof(outputs[0])) ||
        !mg_json_get_bool(hm->body, "$.on", &on))
    {
        mg_http_reply(c,
                      400,
                      "Content-Type: application/json\r\n",
                      "{\"error\":\"expected JSON body {\\\"id\\\":0|1,\\\"on\\\":true|false}\"}\n");
        return;
    }

    output_set((size_t)id, on);
    reply_state(c);
    broadcast_state(mgr);
}

static void start_tls(struct mg_connection *c)
{
    struct mg_tls_opts opts;
    uint32_t handshake_start = HAL_GetTick();

    memset(&opts, 0, sizeof(opts));
    opts.cert = mg_str((const char *)cert_pem);
    opts.key = mg_str((const char *)key_pem);
    memcpy(c->data, &handshake_start, sizeof(handshake_start));
    printf("HTTPS client connected\r\n");
    mg_tls_init(c, &opts);
}

static void redirect_to_https(struct mg_connection *c, struct mg_http_message *hm)
{
    struct mg_str *host = mg_http_get_header(hm, "Host");
    char location[160];
    size_t host_len = host == NULL ? 14 : host->len;
    const char *host_ptr = host == NULL ? "192.168.100.10" : host->buf;
    size_t i;

    for (i = 0; i < host_len; i++)
    {
        if (host_ptr[i] == ':')
        {
            host_len = i;
            break;
        }
    }

    mg_snprintf(location,
                sizeof(location),
                "Location: https://%.*s%.*s\r\n"
                "Connection: close\r\n",
                (int)host_len,
                host_ptr,
                (int)hm->uri.len,
                hm->uri.buf);

    mg_http_reply(c,
                  301,
                  location,
                  "Redirecting to HTTPS\n");
    c->is_draining = 1;
}

static void http_ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_MSG)
        redirect_to_https(c, (struct mg_http_message *)ev_data);
}

static void https_ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_ACCEPT && c->is_tls)
        start_tls(c);
    else if (ev == MG_EV_TLS_HS)
    {
        uint32_t handshake_start;
        struct mg_tls *tls = (struct mg_tls *)c->tls;
        memcpy(&handshake_start, c->data, sizeof(handshake_start));
        printf("TLS handshake OK time=%lu ms cipher: %s\r\n",
               (unsigned long)(HAL_GetTick() - handshake_start),
               mbedtls_ssl_get_ciphersuite(&tls->ssl));
    }
    else if (ev == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        printf("HTTPS request received: %lu bytes\r\n",
               (unsigned long)hm->message.len);

        if (mg_match(hm->uri, mg_str("/"), NULL))
        {
            mg_http_reply(c,
                          200,
                          "Content-Type: text/html; charset=utf-8\r\n"
                          "Cache-Control: no-store\r\n",
                          "%s",
                          dashboard_html);
        }
        else if (mg_match(hm->uri, mg_str("/api/state"), NULL))
        {
            reply_state(c);
        }
        else if (mg_match(hm->uri, mg_str("/api/output"), NULL))
        {
            handle_output_update(c, hm, c->mgr);
        }
        else if (mg_match(hm->uri, mg_str("/ws"), NULL))
        {
            mg_ws_upgrade(c, hm, NULL);
        }
        else
        {
            mg_http_reply(c,
                          404,
                          "Content-Type: application/json\r\n",
                          "{\"error\":\"not found\"}\n");
        }
    }
    else if (ev == MG_EV_WS_OPEN)
    {
        char json[256];
        size_t len = make_state_json(json, sizeof(json));
        mg_ws_send(c, json, len, WEBSOCKET_OP_TEXT);
    }
    else if (ev == MG_EV_WS_MSG)
    {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        if (mg_match(wm->data, mg_str("state"), NULL))
        {
            char json[256];
            size_t len = make_state_json(json, sizeof(json));
            mg_ws_send(c, json, len, WEBSOCKET_OP_TEXT);
        }
    }
    else if (ev == MG_EV_CLOSE && c->is_accepted)
    {
        printf("HTTPS client disconnected\r\n");
    }
}

static void https_server_task(void *argument)
{
    struct mg_mgr mgr;
    uint32_t last_broadcast = 0;

    (void)argument;

    while (gnetif.ip_addr.addr == 0)
        osDelay(100);

    printf("Network ready\r\n");

    mg_log_set(MG_LL_INFO);
    mg_mgr_init(&mgr);

    if (mg_http_listen(&mgr, "http://0.0.0.0:80", http_ev_handler, NULL) == NULL)
    {
        printf("Mongoose HTTP listen failed\r\n");
        for (;;)
            osDelay(1000);
    }

    if (mg_http_listen(&mgr, "https://0.0.0.0:443", https_ev_handler, NULL) == NULL)
    {
        printf("Mongoose HTTPS listen failed\r\n");
        for (;;)
            osDelay(1000);
    }

    printf("Mongoose v%s HTTPS server listening on port 443\r\n", MG_VERSION);

    for (;;)
    {
        mg_mgr_poll(&mgr, 50);

        if (HAL_GetTick() - last_broadcast >= 1000U)
        {
            last_broadcast = HAL_GetTick();
            broadcast_state(&mgr);
        }
    }
}
/* ---------------------------------------------------------------------------
 * Application entry point
 * ---------------------------------------------------------------------------*/
void main_app(void *arg)
{
    (void)arg;

    InitLwip();

    ostHTTPS = osThreadNew(https_server_task, NULL, &osaHTTPS);

    while (1)
        osDelay(1000);
}
