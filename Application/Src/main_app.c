#include "main.h"
#include <stdio.h>
#include <string.h>

#include "cert.h"
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
    if (fd != 1 && fd != 2)
        return len;

    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);

    HAL_UART_Transmit(&huart3, buf, (uint16_t)len, 1000);

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

typedef struct
{
    bool dhcp;
    ip4_addr_t ip;
    ip4_addr_t netmask;
    ip4_addr_t gateway;
} network_config_t;

static network_config_t s_net_cfg;

static void net_config_init_defaults(void)
{
    s_net_cfg.dhcp = false;
    IP4_ADDR(&s_net_cfg.ip,      192, 168, 100, 10);
    IP4_ADDR(&s_net_cfg.netmask, 255, 255, 255,  0);
    IP4_ADDR(&s_net_cfg.gateway, 192, 168, 100,  1);
}

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
        if (s_net_cfg.dhcp)
        {
            DHCP_state = DHCP_START;
            return;
        }
#endif
        ip_addr_t ipaddr;
        char ip_str[16];
        ip4_addr_set_u32(&ipaddr, netif_ip4_addr(netif)->addr);
        ip4addr_ntoa_r(&ipaddr, ip_str, sizeof(ip_str));
        printf("Static IPv4 address: %s\r\n", ip_str);
    }
    else
    {
#if LWIP_DHCP
        if (s_net_cfg.dhcp)
        {
            DHCP_state = DHCP_LINK_DOWN;
            return;
        }
#endif
        printf("The network cable is not connected\r\n");
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
    net_config_init_defaults();

    if (s_net_cfg.dhcp)
    {
        ip_addr_set_zero_ip4(&ipaddr);
        ip_addr_set_zero_ip4(&netmask);
        ip_addr_set_zero_ip4(&gw);
    }
    else
    {
        ip_addr_copy_from_ip4(ipaddr, s_net_cfg.ip);
        ip_addr_copy_from_ip4(netmask, s_net_cfg.netmask);
        ip_addr_copy_from_ip4(gw, s_net_cfg.gateway);
    }

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

#define TLS_HANDSHAKE_TIMEOUT_MS 5000U
#define HTTP_KEEPALIVE_TIMEOUT_MS 5000U
#define WS_PING_INTERVAL_MS      15000U
#define WS_MAX_SEND_QUEUE        4096U
#define LOGIN_PASSWORD           "1071"
#define AUTH_COOKIE              "st_auth=1071"
#define AUTH_TOKEN               "1071"

typedef struct
{
    uint32_t handshake_started_at;
    uint32_t last_activity_at;
    uint32_t last_ws_ping_at;
    bool authenticated;
} conn_state_t;

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

static void ip4_to_str(const ip4_addr_t *addr, char *buf, size_t len)
{
    ip4addr_ntoa_r(addr, buf, (int)len);
}

static bool parse_ip4_json(struct mg_str body, const char *path, ip4_addr_t *addr)
{
    char *value = mg_json_get_str(body, path);
    bool ok = value != NULL && ip4addr_aton(value, addr) != 0;

    if (value != NULL)
        mg_free(value);

    return ok;
}

static conn_state_t *conn_state(struct mg_connection *c)
{
    return (conn_state_t *)c->data;
}

static void init_conn_state(struct mg_connection *c)
{
    conn_state_t *state = conn_state(c);
    uint32_t now = HAL_GetTick();

    memset(state, 0, sizeof(*state));
    state->handshake_started_at = now;
    state->last_activity_at = now;
    state->last_ws_ping_at = now;
}

static void mark_conn_activity(struct mg_connection *c)
{
    conn_state(c)->last_activity_at = HAL_GetTick();
}

static uint32_t elapsed_since(uint32_t since)
{
    return HAL_GetTick() - since;
}

static bool http_connection_is_idle(struct mg_connection *c,
                                    const conn_state_t *state)
{
    return !c->is_websocket &&
           !c->is_tls_hs &&
           !c->is_resp &&
           c->pfn_data == NULL &&
           c->send.len == 0 &&
           elapsed_since(state->last_activity_at) > HTTP_KEEPALIVE_TIMEOUT_MS;
}

static void mongoose_log_filter(char ch, void *param)
{
    static char line[160];
    static size_t len = 0;

    (void)param;

    if (ch == '\r')
        return;

    if (ch == '\n')
    {
        line[len] = '\0';
        if (len > 0 &&
            strstr(line, "accept failed, errno 103") == NULL &&
            strstr(line, " socket error") == NULL)
        {
            printf("%s\r\n", line);
        }
        len = 0;
        return;
    }

    if (len < sizeof(line) - 1)
        line[len++] = ch;
}

static bool request_is_authenticated(struct mg_http_message *hm)
{
    struct mg_str *cookie = mg_http_get_header(hm, "Cookie");
    struct mg_str *header = mg_http_get_header(hm, "X-ST-Auth");
    const char *needle = AUTH_COOKIE;
    size_t needle_len = strlen(needle);
    size_t i;
    char token[16];

    if (header != NULL &&
        header->len == strlen(AUTH_TOKEN) &&
        memcmp(header->buf, AUTH_TOKEN, header->len) == 0)
    {
        return true;
    }

    if (mg_http_get_var(&hm->query, "token", token, sizeof(token)) > 0 &&
        strcmp(token, AUTH_TOKEN) == 0)
    {
        return true;
    }

    if (cookie == NULL || cookie->len < needle_len)
        return false;

    for (i = 0; i + needle_len <= cookie->len; i++)
    {
        if (memcmp(cookie->buf + i, needle, needle_len) == 0)
            return true;
    }

    return false;
}

static void reply_unauthorized(struct mg_connection *c)
{
    mg_http_reply(c,
                  401,
                  "Content-Type: application/json\r\n"
                  "Cache-Control: no-store\r\n",
                  "{\"error\":\"unauthorized\"}\n");
}

static bool uri_has_prefix(struct mg_str uri, const char *prefix)
{
    size_t len = strlen(prefix);

    return uri.len >= len && memcmp(uri.buf, prefix, len) == 0;
}

static void redirect_to_path(struct mg_connection *c, const char *path)
{
    char headers[96];

    mg_snprintf(headers,
                sizeof(headers),
                "Location: %s\r\n"
                "Cache-Control: no-store\r\n",
                path);
    mg_http_reply(c, 303, headers, "");
}

static bool uri_contains_dotdot(struct mg_str uri)
{
    size_t i;

    for (i = 0; i + 1 < uri.len; i++)
    {
        if (uri.buf[i] == '.' && uri.buf[i + 1] == '.')
            return true;
    }

    return false;
}

static void serve_web_file(struct mg_connection *c,
                           struct mg_http_message *hm,
                           const char *path,
                           bool no_store)
{
    struct mg_http_serve_opts opts = {
        .root_dir = "/web_root",
        .extra_headers = no_store ?
            "Cache-Control: no-store\r\n" :
            "Cache-Control: no-cache\r\n",
        .fs = &mg_fs_packed,
    };

    mg_mem_files = mg_packed_files;
    mg_http_serve_file(c, hm, path, &opts);
}

static void serve_web_asset(struct mg_connection *c, struct mg_http_message *hm)
{
    char path[128];

    if (uri_contains_dotdot(hm->uri) ||
        mg_snprintf(path,
                    sizeof(path),
                    "/web_root%.*s",
                    (int)hm->uri.len,
                    hm->uri.buf) >= sizeof(path))
    {
        mg_http_reply(c,
                      400,
                      "Content-Type: text/plain; charset=utf-8\r\n"
                      "Cache-Control: no-store\r\n",
                      "bad request\n");
        return;
    }

    serve_web_file(c, hm, path, false);
}

static void handle_login(struct mg_connection *c, struct mg_http_message *hm)
{
    char *password = mg_json_get_str(hm->body, "$.password");
    bool ok = password != NULL && strcmp(password, LOGIN_PASSWORD) == 0;

    if (password != NULL)
        mg_free(password);

    if (ok)
    {
        mg_http_reply(c,
                      200,
                      "Content-Type: application/json\r\n"
                      "Cache-Control: no-store\r\n"
                      "Set-Cookie: " AUTH_COOKIE "; Path=/; Secure; HttpOnly; SameSite=Lax\r\n",
                      "{\"ok\":true,\"token\":\"" AUTH_TOKEN "\"}\n");
    }
    else
    {
        mg_http_reply(c,
                      403,
                      "Content-Type: application/json\r\n"
                      "Cache-Control: no-store\r\n",
                      "{\"error\":\"bad password\"}\n");
    }
}

static void handle_logout(struct mg_connection *c)
{
    mg_http_reply(c,
                  200,
                  "Content-Type: application/json\r\n"
                  "Cache-Control: no-store\r\n"
                  "Set-Cookie: st_auth=; Path=/; Secure; HttpOnly; SameSite=Lax; Max-Age=0\r\n",
                  "{\"ok\":true}\n");
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

static size_t make_network_json(char *buf, size_t len)
{
    char cfg_ip[16], cfg_mask[16], cfg_gw[16];
    char cur_ip[16], cur_mask[16], cur_gw[16];

    ip4_to_str(&s_net_cfg.ip, cfg_ip, sizeof(cfg_ip));
    ip4_to_str(&s_net_cfg.netmask, cfg_mask, sizeof(cfg_mask));
    ip4_to_str(&s_net_cfg.gateway, cfg_gw, sizeof(cfg_gw));
    ip4addr_ntoa_r(netif_ip4_addr(&gnetif), cur_ip, sizeof(cur_ip));
    ip4addr_ntoa_r(netif_ip4_netmask(&gnetif), cur_mask, sizeof(cur_mask));
    ip4addr_ntoa_r(netif_ip4_gw(&gnetif), cur_gw, sizeof(cur_gw));

    return (size_t)snprintf(buf,
                            len,
                            "{"
                            "\"dhcp\":%s,"
                            "\"ip\":\"%s\","
                            "\"netmask\":\"%s\","
                            "\"gateway\":\"%s\","
                            "\"current\":{\"ip\":\"%s\",\"netmask\":\"%s\",\"gateway\":\"%s\",\"link\":%s}"
                            "}",
                            s_net_cfg.dhcp ? "true" : "false",
                            cfg_ip,
                            cfg_mask,
                            cfg_gw,
                            cur_ip,
                            cur_mask,
                            cur_gw,
                            netif_is_link_up(&gnetif) ? "true" : "false");
}

static void reply_state(struct mg_connection *c)
{
    char json[256];
    make_state_json(json, sizeof(json));
    mg_http_reply(c,
                  200,
                  "Content-Type: application/json\r\n"
                  "Cache-Control: no-store\r\n"
                  "%s",
                  json);
}

static void reply_network(struct mg_connection *c)
{
    char json[256];
    make_network_json(json, sizeof(json));
    mg_http_reply(c,
                  200,
                  "Content-Type: application/json\r\n"
                  "Cache-Control: no-store\r\n"
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
        if (!conn->is_websocket)
            continue;

        if (conn->send.len > WS_MAX_SEND_QUEUE)
        {
            conn->is_closing = 1;
            continue;
        }

        if (conn->send.len == 0)
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
                      "Content-Type: application/json\r\n"
                      "Cache-Control: no-store\r\n",
                      "{\"error\":\"expected JSON body {\\\"id\\\":0|1,\\\"on\\\":true|false}\"}\n");
        return;
    }

    output_set((size_t)id, on);
    reply_state(c);
    broadcast_state(mgr);
}

static bool handle_output_json(struct mg_mgr *mgr, struct mg_str body)
{
    long id = mg_json_get_long(body, "$.id", -1);
    bool on = false;

    if (id < 0 || id >= (long)(sizeof(outputs) / sizeof(outputs[0])) ||
        !mg_json_get_bool(body, "$.on", &on))
    {
        return false;
    }

    output_set((size_t)id, on);
    broadcast_state(mgr);
    return true;
}

static bool apply_network_config(struct mg_str body)
{
    bool dhcp = false;
    ip4_addr_t ip, netmask, gateway;

    if (!mg_json_get_bool(body, "$.dhcp", &dhcp))
        return false;

    if (!dhcp)
    {
        if (!parse_ip4_json(body, "$.ip", &ip) ||
            !parse_ip4_json(body, "$.netmask", &netmask) ||
            !parse_ip4_json(body, "$.gateway", &gateway))
        {
            return false;
        }
    }
    else
    {
        ip4_addr_set_zero(&ip);
        ip4_addr_set_zero(&netmask);
        ip4_addr_set_zero(&gateway);
    }

    s_net_cfg.dhcp = dhcp;
    s_net_cfg.ip = ip;
    s_net_cfg.netmask = netmask;
    s_net_cfg.gateway = gateway;

    if (dhcp)
    {
        ip4_addr_t zero;
        ip4_addr_set_zero(&zero);
        netifapi_netif_set_addr(&gnetif, &zero, &zero, &zero);
        netifapi_dhcp_start(&gnetif);
    }
    else
    {
        netifapi_dhcp_stop(&gnetif);
        netifapi_netif_set_addr(&gnetif, &s_net_cfg.ip, &s_net_cfg.netmask, &s_net_cfg.gateway);
    }

    return true;
}

static void start_tls(struct mg_connection *c)
{
    struct mg_tls_opts opts;

    memset(&opts, 0, sizeof(opts));
    init_conn_state(c);
    opts.cert = mg_str((const char *)cert_pem);
    opts.key = mg_str((const char *)key_pem);
    printf("HTTPS client connected conn=%lu\r\n", c->id);
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
                "Location: https://%.*s%.*s\r\n",
                (int)host_len,
                host_ptr,
                (int)hm->uri.len,
                hm->uri.buf);

    mg_http_reply(c,
                  301,
                  location,
                  "Redirecting to HTTPS\n");
}

static void http_ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_MSG)
        redirect_to_https(c, (struct mg_http_message *)ev_data);
}

static void https_ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_ACCEPT && c->is_tls)
    {
        start_tls(c);
    }
    else if (ev == MG_EV_TLS_HS)
    {
        conn_state_t *state = conn_state(c);
        struct mg_tls *tls = (struct mg_tls *)c->tls;
        printf("TLS handshake OK conn=%lu time=%lu ms cipher: %s\r\n",
               c->id,
               (unsigned long)elapsed_since(state->handshake_started_at),
               mbedtls_ssl_get_ciphersuite(&tls->ssl));
        mark_conn_activity(c);
    }
    else if (ev == MG_EV_HTTP_MSG)
    {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        bool authed = request_is_authenticated(hm);

        mark_conn_activity(c);

        printf("HTTPS request conn=%lu: %.*s %.*s (%lu bytes) sendq=%lu recvq=%lu\r\n",
               c->id,
               (int)hm->method.len,
               hm->method.buf,
               (int)hm->uri.len,
               hm->uri.buf,
               (unsigned long)hm->message.len,
               (unsigned long)c->send.len,
               (unsigned long)c->recv.len);

        if (mg_match(hm->uri, mg_str("/"), NULL))
        {
            serve_web_file(c,
                           hm,
                           authed ? "/web_root/dashboard.html" : "/web_root/login.html",
                           true);
        }
        else if (mg_match(hm->uri, mg_str("/api/login"), NULL))
        {
            handle_login(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/login.html"), NULL))
        {
            if (authed)
                redirect_to_path(c, "/");
            else
                serve_web_file(c, hm, "/web_root/login.html", true);
        }
        else if (uri_has_prefix(hm->uri, "/assets/"))
        {
            serve_web_asset(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/favicon.ico"), NULL) ||
                 mg_match(hm->uri, mg_str("/apple-touch-icon*"), NULL))
        {
            mg_http_reply(c,
                          204,
                          "Cache-Control: max-age=86400\r\n",
                          "");
        }
        else if (!authed)
        {
            printf("Unauthorized HTTPS request: %.*s\r\n",
                   (int)hm->uri.len,
                   hm->uri.buf);
            if (mg_match(hm->uri, mg_str("/dashboard.html"), NULL) ||
                uri_has_prefix(hm->uri, "/config/"))
            {
                redirect_to_path(c, "/login.html");
            }
            else
            {
                reply_unauthorized(c);
            }
        }
        else if (mg_match(hm->uri, mg_str("/dashboard.html"), NULL) ||
                 uri_has_prefix(hm->uri, "/config/"))
        {
            serve_web_file(c, hm, "/web_root/dashboard.html", true);
        }
        else if (mg_match(hm->uri, mg_str("/api/logout"), NULL))
        {
            handle_logout(c);
        }
        else if (mg_match(hm->uri, mg_str("/api/state"), NULL))
        {
            reply_state(c);
        }
        else if (mg_match(hm->uri, mg_str("/api/output"), NULL))
        {
            handle_output_update(c, hm, c->mgr);
        }
        else if (mg_match(hm->uri, mg_str("/api/network"), NULL))
        {
            if (mg_strcasecmp(hm->method, mg_str("POST")) == 0)
            {
                if (!apply_network_config(hm->body))
                {
                    mg_http_reply(c,
                                  400,
                                  "Content-Type: application/json\r\n"
                                  "Cache-Control: no-store\r\n",
                                  "{\"error\":\"invalid network config\"}\n");
                    return;
                }
            }
            reply_network(c);
        }
        else if (mg_match(hm->uri, mg_str("/ws"), NULL))
        {
            conn_state(c)->authenticated = true;
            mg_ws_upgrade(c, hm, NULL);
        }
        else
        {
            mg_http_reply(c,
                          404,
                          "Content-Type: application/json\r\n"
                          "Cache-Control: no-store\r\n",
                          "{\"error\":\"not found\"}\n");
        }
    }
    else if (ev == MG_EV_WS_OPEN)
    {
        char json[256];
        size_t len = make_state_json(json, sizeof(json));
        mark_conn_activity(c);
        mg_ws_send(c, json, len, WEBSOCKET_OP_TEXT);
        len = make_network_json(json, sizeof(json));
        mg_ws_send(c, json, len, WEBSOCKET_OP_TEXT);
    }
    else if (ev == MG_EV_WS_MSG)
    {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        mark_conn_activity(c);

        if (!conn_state(c)->authenticated)
        {
            static const char unauthorized[] = "{\"error\":\"unauthorized\"}";
            mg_ws_send(c, unauthorized, strlen(unauthorized), WEBSOCKET_OP_TEXT);
            c->is_draining = 1;
        }
        else if (mg_match(wm->data, mg_str("state"), NULL))
        {
            char json[256];
            size_t len = make_state_json(json, sizeof(json));
            mg_ws_send(c, json, len, WEBSOCKET_OP_TEXT);
        }
        else if (mg_match(wm->data, mg_str("network"), NULL))
        {
            char json[256];
            size_t len = make_network_json(json, sizeof(json));
            mg_ws_send(c, json, len, WEBSOCKET_OP_TEXT);
        }
        else
        {
            (void)handle_output_json(c->mgr, wm->data);
        }
    }
    else if (ev == MG_EV_WS_CTL)
    {
        mark_conn_activity(c);
    }
    else if (ev == MG_EV_CLOSE && c->is_accepted)
    {
        conn_state_t *state = conn_state(c);

        printf("HTTPS client disconnected conn=%lu websocket=%u draining=%u closing=%u sendq=%lu recvq=%lu age=%lu ms\r\n",
               c->id,
               c->is_websocket ? 1U : 0U,
               c->is_draining ? 1U : 0U,
               c->is_closing ? 1U : 0U,
               (unsigned long)c->send.len,
               (unsigned long)c->recv.len,
               (unsigned long)elapsed_since(state->handshake_started_at));
    }
    else if (ev == MG_EV_POLL && c->is_accepted)
    {
        conn_state_t *state = conn_state(c);

        if (c->is_websocket)
        {
            if (elapsed_since(state->last_ws_ping_at) >= WS_PING_INTERVAL_MS)
            {
                state->last_ws_ping_at = HAL_GetTick();
                if (c->send.len == 0)
                    mg_ws_send(c, "", 0, WEBSOCKET_OP_PING);
            }

            if (c->send.len > WS_MAX_SEND_QUEUE)
                c->is_closing = 1;
        }
        else if (c->is_tls_hs &&
                 elapsed_since(state->handshake_started_at) > TLS_HANDSHAKE_TIMEOUT_MS)
        {
            c->is_closing = 1;
        }
        else if (http_connection_is_idle(c, state))
        {
            c->is_draining = 1;
        }
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

    mg_log_set_fn(mongoose_log_filter, NULL);
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
        mg_mgr_poll(&mgr, 10);

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
