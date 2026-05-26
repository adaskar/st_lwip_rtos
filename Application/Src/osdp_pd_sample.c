#include "osdp_pd_sample.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cmsis_os2.h"
#include "lwip/errno.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "osdp.h"
#include "stm32h5xx_hal.h"

#define OSDP_PD_TCP_PORT       8090
#define OSDP_PD_ADDRESS        101
#define OSDP_PD_REFRESH_MS     10U
#define OSDP_PD_LISTEN_BACKLOG 1

typedef struct
{
    int fd;
    bool connected;
} osdp_tcp_channel_t;

static osThreadId_t s_osdp_pd_thread;

static const osThreadAttr_t s_osdp_pd_thread_attr = {
    .name = "osdp_pd",
    .priority = (osPriority_t)osPriorityBelowNormal,
    .stack_size = 1024 * 6,
};

uint32_t osdp_millis_now(void)
{
    return HAL_GetTick();
}

static int osdp_tcp_send(void *data, uint8_t *buf, int len)
{
    osdp_tcp_channel_t *channel = (osdp_tcp_channel_t *)data;
    int n;

    if (channel == NULL || !channel->connected || channel->fd < 0)
        return -1;

    n = lwip_send(channel->fd, buf, (size_t)len, MSG_DONTWAIT);
    if (n == len)
        return len;

    if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
        return 0;

    channel->connected = false;
    return -1;
}

static int osdp_tcp_recv(void *data, uint8_t *buf, int maxlen)
{
    osdp_tcp_channel_t *channel = (osdp_tcp_channel_t *)data;
    int n;

    if (channel == NULL || !channel->connected || channel->fd < 0)
        return -1;

    n = lwip_recv(channel->fd, buf, (size_t)maxlen, MSG_DONTWAIT);
    if (n > 0)
        return n;

    if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
        return 0;

    channel->connected = false;
    return -1;
}

static void osdp_tcp_close(void *data)
{
    osdp_tcp_channel_t *channel = (osdp_tcp_channel_t *)data;

    if (channel == NULL)
        return;

    if (channel->fd >= 0)
    {
        lwip_close(channel->fd);
        channel->fd = -1;
    }
    channel->connected = false;
}

static int osdp_pd_command_handler(void *arg, struct osdp_cmd *cmd)
{
    (void)arg;

    printf("OSDP PD command id=%d\r\n", cmd->id);
    return 0;
}

static const struct osdp_pd_cap s_osdp_pd_caps[] = {
    { OSDP_PD_CAP_OUTPUT_CONTROL,        1, 2 },
    { OSDP_PD_CAP_READER_LED_CONTROL,    1, 1 },
    { OSDP_PD_CAP_READER_AUDIBLE_OUTPUT, 1, 1 },
    { OSDP_PD_CAP_CONTACT_STATUS_MONITORING, 1, 1 },
    { (uint8_t)-1, 0, 0 },
};

static const osdp_pd_info_t s_osdp_pd_info = {
    .name = "tcp-pd",
    .baud_rate = 9600,
    .address = OSDP_PD_ADDRESS,
    .flags = 0,
    .id = {
        .version = 1,
        .model = 1,
        .vendor_code = 0x00A1B2,
        .serial_number = 0x01020304,
        .firmware_version = 0x000100,
    },
    .cap = s_osdp_pd_caps,
    .scbk = NULL,
};

static int osdp_pd_create_listener(void)
{
    struct sockaddr_in addr;
    int fd;
    int yes = 1;

    fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;

    (void)lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = PP_HTONL(INADDR_ANY);
    addr.sin_port = PP_HTONS(OSDP_PD_TCP_PORT);

    if (lwip_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        lwip_listen(fd, OSDP_PD_LISTEN_BACKLOG) != 0)
    {
        lwip_close(fd);
        return -1;
    }

    return fd;
}

static void osdp_pd_run_client(int client_fd)
{
    osdp_tcp_channel_t tcp_channel = {
        .fd = client_fd,
        .connected = true,
    };
    struct osdp_channel osdp_channel = {
        .data = &tcp_channel,
        .recv = osdp_tcp_recv,
        .send = osdp_tcp_send,
        .close = osdp_tcp_close,
    };
    osdp_t *ctx;

    (void)lwip_fcntl(client_fd, F_SETFL, O_NONBLOCK);

    ctx = osdp_pd_setup(&osdp_channel, &s_osdp_pd_info);
    if (ctx == NULL)
    {
        printf("OSDP PD setup failed\r\n");
        osdp_tcp_close(&tcp_channel);
        return;
    }

    osdp_pd_set_command_callback(ctx, osdp_pd_command_handler, NULL);
    printf("OSDP PD client connected on TCP port %d\r\n", OSDP_PD_TCP_PORT);

    while (tcp_channel.connected)
    {
        osdp_pd_refresh(ctx);
        osDelay(OSDP_PD_REFRESH_MS);
    }

    osdp_pd_teardown(ctx);
    osdp_tcp_close(&tcp_channel);
    printf("OSDP PD client disconnected\r\n");
}

static void osdp_pd_sample_task(void *argument)
{
    int listen_fd;

    (void)argument;

    for (;;)
    {
        listen_fd = osdp_pd_create_listener();
        if (listen_fd < 0)
        {
            printf("OSDP PD listen failed on TCP port %d\r\n", OSDP_PD_TCP_PORT);
            osDelay(1000);
            continue;
        }

        printf("OSDP PD listening on TCP port %d address %d\r\n",
               OSDP_PD_TCP_PORT,
               OSDP_PD_ADDRESS);

        for (;;)
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = lwip_accept(listen_fd,
                                        (struct sockaddr *)&client_addr,
                                        &client_len);

            if (client_fd < 0)
            {
                osDelay(100);
                continue;
            }

            osdp_pd_run_client(client_fd);
        }
    }
}

void osdp_pd_sample_start(void)
{
    if (s_osdp_pd_thread == NULL)
        s_osdp_pd_thread = osThreadNew(osdp_pd_sample_task,
                                       NULL,
                                       &s_osdp_pd_thread_attr);
}
