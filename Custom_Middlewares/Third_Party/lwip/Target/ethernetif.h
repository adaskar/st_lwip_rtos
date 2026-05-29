/**
  ******************************************************************************
  * @file    LwIP/LwIP_HTTP_Server_Socket_RTOS/LWIP/Target/ethernetif.h
  * @author  MCD Application Team
  * @brief   Header for ethernetif.c module
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__


#include "lwip/err.h"
#include "lwip/netif.h"
#include "cmsis_os2.h"
#include <stdint.h>


/* Exported types ------------------------------------------------------------*/
typedef struct
{
  uint32_t rx_packets;
  uint32_t rx_dropped;
  uint32_t rx_alloc_errors;
  uint32_t rx_alloc_status;
  uint32_t tx_packets;
  uint32_t tx_errors;
  uint32_t tx_busy_drops;
  uint32_t tx_driver_buffers_in_use;
  uint32_t tx_hal_buffers_in_use;
  uint32_t dma_errors;
  uint32_t dma_last_error;
  uint32_t dma_rbu_errors;
  uint32_t dma_tbu_errors;
  uint32_t dma_tps_errors;
  uint32_t dma_rps_errors;
  uint32_t dma_fbe_errors;
  uint32_t link_up_count;
  uint32_t link_down_count;
} ethernetif_stats_t;

err_t ethernetif_init(struct netif *netif);
void ethernet_link_thread( void *argument );
void ethernetif_get_stats(ethernetif_stats_t *out);
void ethernetif_reset_stats(void);
#endif
