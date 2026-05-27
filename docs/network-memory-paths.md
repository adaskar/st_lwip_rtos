# STM32H573 Network Packet And Memory Path Guide

This document describes how packets move through this firmware and where memory
is owned, allocated, copied, and released. Use it as the baseline before changing
lwIP, Ethernet DMA, Mongoose, mbedTLS, or FreeRTOS memory settings.

## Current Scope

The active stack is:

- STM32 HAL Ethernet MAC/DMA driver
- Custom `ethernetif.c` lwIP netif glue
- lwIP with `NO_SYS == 0`, `tcpip_thread`, netconn/socket APIs
- Mongoose using lwIP sockets
- mbedTLS behind Mongoose TLS
- FreeRTOS/CMSIS-RTOS2

Important files:

- `Core/Src/main.c`: owns the global ETH handle and DMA descriptor arrays.
- `Custom_Middlewares/Third_Party/lwip/Target/ethernetif.c`: owns Ethernet RX/TX buffer pools and netif glue.
- `Custom_Middlewares/Third_Party/lwip/Target/lwipopts.h`: owns lwIP heap, pbuf, TCP, mailbox, and pool sizing.
- `Application/Src/main_app.c`: owns lwIP startup, Mongoose server, request handling, websocket fanout, and public heap/ETH stats.
- `Custom_Middlewares/Third_Party/mongoose/Target/mongoose_config.h`: selects Mongoose architecture, lwIP sockets, FreeRTOS allocator path, and mbedTLS.
- `Custom_Middlewares/Third_Party/mbedtls/Target/mbedtls_config.h`: selects mbedTLS allocator and TLS buffer sizes.

## Static Memory Reservoirs

These are separate reservoirs. Treat them as separate ownership domains.

| Reservoir | Config/source | Main users | Notes |
| --- | --- | --- | --- |
| FreeRTOS heap | `configTOTAL_HEAP_SIZE` in `Core/Inc/FreeRTOSConfig.h` | RTOS objects, task stacks, Mongoose allocations, mbedTLS allocations | Implemented by `heap_4.c` as `ucHeap`. |
| lwIP heap | `MEM_SIZE` in `lwipopts.h` | lwIP `mem_malloc`, some pbuf and protocol allocations | Appears as `ram_heap` in the map. |
| lwIP fixed pools | `MEMP_NUM_*`, `PBUF_POOL_SIZE`, `PBUF_POOL_BUFSIZE` in `lwipopts.h` | PCBs, netconns, TCP segments, pbuf pool, messages | Appear as `memp_memory_*_base` in the map. |
| Ethernet RX pool | `ETH_RX_BUFFER_CNT`, `ETH_RX_BUFFER_SIZE` in `ethernetif.c` | ETH DMA receive buffers exposed to lwIP as custom pbufs | Declared with `LWIP_MEMPOOL_DECLARE(RX_POOL, ...)`. |
| Ethernet TX buffers | `ETH_TX_BUFFER_CNT`, `ETH_RX_BUFFER_SIZE` in `ethernetif.c` | Driver-owned transmit copies for async DMA TX | Static `TxBuffers[]`; independent from lwIP pbufs. |
| ETH DMA descriptors | `ETH_RX_DESC_CNT`, `ETH_TX_DESC_CNT` in `stm32h5xx_hal_conf.h` | HAL DMA rings | Arrays live in `Core/Src/main.c`. |
| newlib heap | `_sbrk`/libc heap | Should be avoided for main networking stack | Mongoose and mbedTLS are intended to use FreeRTOS heap, not newlib heap. |

## Initialization Route

1. `main()` initializes HAL and peripherals, including ETH, then starts the RTOS.
2. `main_app()` creates app synchronization objects, calls `InitLwip()`, then starts the Mongoose HTTPS task.
3. `InitLwip()` calls `tcpip_init()`, configures addresses, and calls:

   ```c
   netif_add(&gnetif, ..., &ethernetif_init, &tcpip_input);
   ```

4. `ethernetif_init()` sets lwIP netif callbacks:

   ```c
   netif->output = etharp_output;
   netif->linkoutput = low_level_output;
   ```

5. `low_level_init()`:

   - connects HAL ETH to `DMARxDscrTab` and `DMATxDscrTab`;
   - sets `EthHandle.Init.RxBuffLen = ETH_RX_BUFFER_SIZE`;
   - initializes `RX_POOL`;
   - creates RX/TX semaphores;
   - starts the `ethernetif_input` task;
   - starts the MAC if the PHY link is up.

## Incoming Packet Route

### 1. DMA buffer allocation

When HAL needs an RX buffer, it calls:

```c
HAL_ETH_RxAllocateCallback(uint8_t **buff)
```

Ownership and memory:

- Allocates one `RxBuff_t` from `RX_POOL`.
- `RxBuff_t` contains:
  - `struct pbuf_custom pbuf_custom`
  - a 32-byte-aligned 1536-byte payload buffer
- Calls `pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, ...)`.
- The pbuf is custom; its free callback is `pbuf_free_custom()`.

If `RX_POOL` is empty:

- `rx_alloc_status` becomes `RX_ALLOC_ERROR`.
- HAL receives `NULL`.
- `EthStats.rx_alloc_errors` increments.

### 2. DMA receives an Ethernet frame

The ETH DMA writes bytes directly into the `RxBuff_t.buff` payload buffer.
No `PBUF_POOL` buffer is used for normal Ethernet RX in this driver.

### 3. HAL links received buffers into pbuf chain

HAL calls:

```c
HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
```

This recovers the `struct pbuf` from the payload pointer, sets `len`, updates
`tot_len`, and chains fragments if a frame spans multiple buffers.

For normal 1500-byte Ethernet frames and 1536-byte RX buffers, this should
usually be a single pbuf.

### 4. RX task hands packet to lwIP

The Ethernet ISR/driver path releases `RxPktSemaphore`. The `ethernetif_input`
task wakes and calls:

```c
HAL_ETH_ReadData(&EthHandle, (void **)&p);
netif->input(p, netif);
```

`netif->input` is `tcpip_input`, configured by `netif_add()`. That transfers
the pbuf to the lwIP TCP/IP thread.

If lwIP rejects the packet, `ethernetif_input()` calls `pbuf_free(p)`.

### 5. lwIP protocol handling

lwIP consumes the pbuf through Ethernet, ARP/IP, TCP/UDP, and socket/netconn
layers.

Memory used in this phase can include:

- `RX_POOL` pbuf payloads, still referenced until lwIP/application release;
- `memp_memory_TCP_PCB_base` for TCP connections;
- `memp_memory_TCP_SEG_base` for queued TCP segments;
- `memp_memory_NETCONN_base` and socket tables for socket API state;
- `ram_heap` for lwIP heap allocations;
- `PBUF_POOL` only when lwIP explicitly allocates pool pbufs internally.

### 6. Application receive

Mongoose polls sockets in:

```c
mg_mgr_poll(&mgr, 10);
```

The socket read path copies data out of lwIP/socket receive state into
Mongoose connection buffers. Mongoose buffers are allocated through its
`mg_calloc`/`mg_free` path. In this project, `MG_ENABLE_FREERTOS == 1`, so
Mongoose allocations resolve to FreeRTOS `pvPortMalloc`/`vPortFree`.

### 7. RX buffer release

When lwIP and the socket/application stack are done with the custom pbuf,
`pbuf_free()` eventually invokes:

```c
pbuf_free_custom(struct pbuf *p)
```

That returns the `RxBuff_t` to `RX_POOL`.

If RX allocation had previously failed, freeing a pbuf sets allocation status
back to OK and releases the RX semaphore so descriptors can be rebuilt.

## Outgoing Packet Route

### 1. Application creates output

Application code calls Mongoose APIs such as:

- `mg_http_reply()`
- `mg_http_serve_file()`
- `mg_ws_send()`

Mongoose stores outgoing bytes in per-connection send buffers before the socket
write path drains them.

Memory used here:

- FreeRTOS heap for Mongoose connection and I/O buffers.
- Stack buffers in `main_app.c` for temporary JSON/header formatting.
- For HTTPS, mbedTLS buffers and contexts are also on FreeRTOS heap.

### 2. TLS encryption, if HTTPS

HTTPS connections call:

```c
mg_tls_init(c, &opts);
```

Mongoose uses mbedTLS. This project routes mbedTLS dynamic allocation through:

```c
MBEDTLS_PLATFORM_CALLOC_MACRO -> mbedtls_freertos_calloc
MBEDTLS_PLATFORM_FREE_MACRO   -> mbedtls_freertos_free
```

Those wrappers use `pvPortMalloc` and `vPortFree`.

Current TLS content buffers are:

```c
MBEDTLS_SSL_IN_CONTENT_LEN  4096
MBEDTLS_SSL_OUT_CONTENT_LEN 4096
```

These affect runtime heap pressure during handshakes and active TLS sessions.

### 3. lwIP socket/TCP output

Mongoose writes to lwIP sockets. lwIP may allocate or reference:

- TCP PCBs from `MEMP_NUM_TCP_PCB`;
- TCP segments from `MEMP_NUM_TCP_SEG`;
- pbufs from lwIP heap or pbuf pools depending on the API/path;
- netconn/API messages and mailboxes.

The exact pbuf type on TX depends on lwIP internals and socket write flags, but
the Ethernet driver does not transmit directly from those pbuf payloads.

### 4. netif output

lwIP calls:

```c
etharp_output()
low_level_output()
```

`low_level_output()` receives a pbuf chain from lwIP. It does not take ownership
of that pbuf.

### 5. Driver-owned TX copy

`low_level_output()`:

1. gets a `TxBuff_t` from static `TxBuffers[]`;
2. copies the full pbuf chain into `tx->buff`;
3. starts async DMA TX with `HAL_ETH_Transmit_IT()`;
4. returns to lwIP.

Important consequence:

- lwIP can free/reuse its pbufs after `low_level_output()` returns.
- The DMA owns only the copied `TxBuff_t.buff`.
- This is not zero-copy TX; it is async copy-TX.

If no TX buffer is available:

- `tx_busy_drops` increments;
- `ERR_BUF` is returned to lwIP.

### 6. TX completion

When the HAL releases TX packets, `HAL_ETH_TxFreeCallback()` receives the
`TxConfig.pData` pointer, validates that it points inside `TxBuffers[]`, and
marks that buffer free.

`low_level_output()` and `ethernetif_input()` both call `tx_release_completed()`
opportunistically to return completed buffers to the local free list.

## What Is Duplicated And What Is Not

There are multiple full-frame reservoirs:

- `RX_POOL`: driver-owned zero-copy receive buffers.
- `TxBuffers[]`: driver-owned async transmit copy buffers.
- `PBUF_POOL`: lwIP-owned generic pbuf pool.

This is not automatically wrong. They serve different ownership/lifetime
requirements:

- RX DMA needs buffers before packets arrive.
- TX DMA needs payload bytes to remain valid after lwIP returns.
- lwIP needs internal pbufs for stack-managed packet storage.

The potential waste is sizing all three for worst case without proving the
traffic pattern requires it.

## Memory Pressure Signals Already Present

`main_app.c` already exports important runtime state through `make_state_json()`:

- FreeRTOS heap total/free/used/min-free/max-used.
- Ethernet `rxAllocErrors`.
- Ethernet `txBusyDrops`.
- Ethernet DMA error counters.
- Websocket send queue limits are enforced with `WS_MAX_SEND_QUEUE`.
- HTTPS pre-request low-heap guard uses `heap_free_bytes()`.

Use these first during stress testing. Avoid enabling global `LWIP_STATS` in a
release build until its RAM cost is understood; lwIP stats can increase static
RAM because pool elements gain statistics bookkeeping.

## Production Sizing Method

Do not pick final sizes from the map alone. Use this loop:

1. Record current static map sizes for:
   - `ucHeap`
   - `ram_heap`
   - `memp_memory_PBUF_POOL_base`
   - `memp_memory_RX_POOL_base`
   - `TxBuffers`
   - TCP/netconn/mqueue pools
2. Run worst-case traffic:
   - repeated HTTPS handshakes;
   - multiple browser tabs;
   - maximum expected websocket clients;
   - large static asset requests;
   - login/logout loops;
   - DHCP transition if DHCP is enabled;
   - cable unplug/replug;
   - slow clients that do not finish TLS/request quickly.
3. Watch runtime counters:
   - FreeRTOS min-free heap from `/api/state`;
   - `rxAllocErrors`;
   - `rxDropped`;
   - `txBusyDrops`;
   - TLS handshake failures;
   - websocket disconnects due to send queue growth.
4. Only reduce a pool if its failure counter remains zero and workload behavior
   remains stable with margin.
5. Keep a written reason for every non-default memory size.

## Production Requirements Contract

Use this table as the product-level network contract. lwIP, Mongoose, TLS, and
FreeRTOS memory sizes should be derived from these limits. If the product
requirements change, update this table before changing pool sizes.

| Requirement | Production limit | Reason / expected behavior |
| --- | ---: | --- |
| HTTPS listener sockets | 1 | Dashboard HTTPS server on port 443. |
| HTTP redirect listener sockets | 1 | Port 80 exists only to redirect to HTTPS. Do not keep HTTP clients open. |
| OSDP listener sockets | 1 | Future OSDP TCP/TLS server. |
| Active dashboard WebSocket clients | 4 | Matches `WS_MAX_CLIENTS`; each is one long-lived HTTPS/TLS/TCP connection. |
| Active OSDP client connections | 1 | Product requirement: OSDP costs only one long-lived TCP or TLS connection. |
| Concurrent HTTPS handshakes before request | 3 | Matches `HTTPS_MAX_PRE_REQUEST_CONNS`; protects TLS heap during connection storms. |
| Non-WebSocket HTTPS request connections | 4 | Allows short-lived API/static asset requests while WebSockets are active. Close aggressively after response where practical. |
| HTTP redirect connections | 2 | Short-lived only; redirect and close. More than this is overload/noise. |
| DNS/DHCP UDP users | 2 UDP PCBs | DHCP plus DNS when runtime DHCP is enabled. |
| Raw/ICMP users | 1-2 raw PCBs | ICMP/ping and lwIP internal raw users. |
| Maximum accepted TCP connections | 14 established/connecting plus listeners | 4 WS + 1 OSDP + 4 HTTPS request + 3 handshakes + 2 HTTP redirect. |
| Overload behavior | reject/close, do not grow pools | Extra dashboard/OSDP clients should receive close/503/refusal rather than consuming unbounded RAM. |
| RX burst behavior | tolerate short bursts, allow packet drops under flood | Embedded device should stay alive under broadcast/flood traffic; it does not need to buffer LAN abuse. |
| TX behavior | bounded queue, close slow consumers | `WS_MAX_SEND_QUEUE` already bounds websocket output. Slow clients must not force larger static TCP pools. |
| TLS memory policy | bounded concurrent TLS sessions | TLS sessions are expensive and use FreeRTOS heap through mbedTLS. New TLS use cases must declare their connection count. |

The derived TCP connection budget is intentionally higher than
`WS_MAX_CLIENTS`. Browser traffic uses more than WebSockets: TLS handshakes,
asset/API requests, redirects, and the future OSDP service all consume TCP/lwIP
state.

### Suggested lwIP Pool Budget From This Contract

The "first-pass value" column is the conservative production pass currently
applied in `lwipopts.h`. Validate it with stress tests before making the next
reduction.

| lwIP / driver item | Previous value | Target range | First-pass value | Why |
| --- | ---: | ---: | ---: | --- |
| `MEMP_NUM_TCP_PCB_LISTEN` | 4 | 4 | 4 | HTTPS, HTTP redirect, OSDP, spare/admin or future listener. |
| `MEMP_NUM_TCP_PCB` | 36 | 18-22 | 22 | Covers the 14-connection budget plus margin without reserving for unlimited browser sockets. |
| `MEMP_NUM_NETCONN` | 40 | 22-26 | 26 | Socket API needs one netconn per socket plus margin for listeners/control paths. |
| `MEMP_NUM_NETBUF` | 40 | 16-24 | 24 | Receive-side socket buffers; should follow active request/connection budget, not static worst-case fear. |
| `MEMP_NUM_TCP_SEG` | 256 | 96-144 | 144 | Biggest suspect. Must be sized from active send queues and `TCP_SND_BUF`; reduce only after file/API/WS stress. |
| `MEMP_NUM_TCPIP_MSG_API` | 48 | 24-32 | 32 | Socket/netif API messages; current value likely allows more queueing than product behavior requires. |
| `MEMP_NUM_TCPIP_MSG_INPKT` | 48 | 24-32 | 32 | Incoming packet messages to `tcpip_thread`; keep enough for bursts, but do not mask slow processing. |
| `TCPIP_MBOX_SIZE` | 48 | 24-32 | 32 | Should track incoming/API message budget. |
| `DEFAULT_TCP_RECVMBOX_SIZE` | 32 | 12-16 | 16 | Per-connection receive mailbox. Large values multiply by active sockets. |
| `DEFAULT_ACCEPTMBOX_SIZE` | 24 | 8-12 | 12 | Accept queue should match listener backlog policy, not browser storm capacity. |
| `PBUF_POOL_SIZE` | 32 | 16-24 | 24 | Not the normal DMA RX pool here; keep enough for lwIP internal pbuf needs and TX/internal allocation. |
| `PBUF_POOL_BUFSIZE` | 1536 | 1536 | 1536 | Keep full-frame sized unless deliberately changing MSS/frame strategy. |
| `MEM_SIZE` | 48 KiB | 40-48 KiB | 48 KiB | Reduce only after pool reductions and runtime `ram_heap` behavior are understood. |
| `ETH_RX_BUFFER_CNT` | 12 | 10-12 | 12 | Must stay greater than `ETH_RX_DESC_CNT` of 8. Lower only if `rx_alloc_errors` stays zero under bursts. |
| `ETH_TX_BUFFER_CNT` | 4 | 4 | 4 | Matches TX descriptors. Keep unless `txBusyDrops` appears or zero-copy TX is redesigned. |

For a production first pass, the safest likely reductions are not `MEM_SIZE` or
`RX_POOL`. They are the large static lwIP object counts: TCP PCBs, netconns,
netbufs, TCP segments, and mailbox sizes. Those are currently sized for a much
looser browser concurrency model than the application actually permits.

### Production Acceptance Tests

Before accepting a reduced budget, run these scenarios and keep the observed
counters with the change:

| Scenario | Required result |
| --- | --- |
| 4 authenticated dashboard WebSockets + 1 OSDP TCP connection | Stable for at least 1 hour; no heap leak trend; no unexpected disconnects. |
| Same as above with OSDP over TLS | Stable if OSDP TLS is enabled; FreeRTOS min-free heap remains above the agreed safety floor. |
| 3 parallel HTTPS handshakes while 4 WebSockets are active | New clients either complete or are deliberately rejected by guard logic; system remains responsive. |
| Browser refresh storm with static assets/API requests | No crash; bounded 503/close behavior is acceptable; WebSockets recover. |
| Slow WebSocket client | Send queue limit closes or throttles the client; other clients stay healthy. |
| Cable unplug/replug during active sessions | Link recovers; no permanent RX allocation failure. |
| DHCP renew or static/DHCP switch | No persistent PCB/netconn leak. |
| Broadcast/ping flood from LAN | `rxDropped`/drops may increase, but app and OSDP remain responsive after flood stops. |

## Specific Interpretation Of Current Pools

`RX_POOL` pressure means the MAC/lwIP/application is holding RX frames too long
or RX bursts exceed available custom pbufs.

`PBUF_POOL` pressure means lwIP internally needs more pbufs. It is not the
normal Ethernet RX DMA pool in this driver.

`TxBuffers[]` pressure means the async copy-TX path is producing frames faster
than the HAL/DMA completion path returns local TX buffers.

`ram_heap` pressure means lwIP heap users are exhausting `MEM_SIZE`.

FreeRTOS heap pressure means RTOS objects, task stacks, Mongoose, or mbedTLS are
exhausting `configTOTAL_HEAP_SIZE`.

## Questions To Answer Before Further Optimization

- What is the required maximum number of simultaneous HTTPS handshakes?
- What is the required maximum number of established websocket clients?
- Can HTTP keep-alive be disabled or aggressively closed everywhere?
- What is the largest static asset served from packed FS?
- Are large uploads expected, or only small JSON control messages?
- Is DHCP required in production, or only configuration mode?
- Are slow clients on poor networks part of the supported environment?
- Is lower latency or lower RAM the priority when choosing TX copy count?

## Rules For Future Changes

- Do not merge memory reductions without a stress scenario and observed counters.
- Do not enable `LWIP_STATS` permanently without checking map impact.
- Keep Mongoose and mbedTLS on the FreeRTOS heap unless deliberately moving to a
  custom allocator.
- Keep `ETH_RX_BUFFER_CNT > ETH_RX_DESC_CNT`.
- Keep `PBUF_POOL_BUFSIZE` large enough for the intended TCP/MSS and Ethernet
  frame strategy.
- If changing CubeMX-generated values, update `st.ioc` as well as generated
  headers where applicable.
