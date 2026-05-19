/**
  ******************************************************************************
  * @file    low_level_rng.c
  * @author  MCD Application Team
  * @brief   Low Level Interface module to use STM32 RNG Ip
  *          This file provides mbed-crypto random generataor
  *
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
#include "low_level_rng.h"
#include "stm32h5xx_hal.h"
#include <stdio.h>
extern void Error_Handler(void);

extern RNG_HandleTypeDef hrng;
static uint8_t users = 0;

#define COMPILER_BARRIER() __ASM __IO("" : : : "memory")

static uint8_t atomic_incr_u8(__IO uint8_t *valuePtr, uint8_t delta)
{
#ifdef __GNUC__
  return __atomic_add_fetch(valuePtr, delta, __ATOMIC_SEQ_CST);
#else
  uint8_t prev = *valuePtr;
  *valuePtr = prev + delta;
  return prev + delta;
#endif
}

void RNG_Init(void)
{
  uint32_t dummy;
  /*  We're only supporting a single user of RNG */
  if (atomic_incr_u8(&users, 1) > 1)
  {
    Error_Handler();
  }

  /* Select RNG clock source */
  __HAL_RCC_RNG_CONFIG(RCC_RNGCLKSOURCE_HSI48);

  /* RNG Peripheral clock enable */
  __HAL_RCC_RNG_CLK_ENABLE();

  /* Initialize RNG instance */
  hrng.Instance = RNG;
  hrng.State = HAL_RNG_STATE_RESET;
  hrng.Lock = HAL_UNLOCKED;

  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    Error_Handler();
  }

  /* first random number generated after setting the RNGEN bit should not be used */
  HAL_RNG_GenerateRandomNumber(&hrng, &dummy);
}

void RNG_GetBytes(uint8_t *output, size_t length, size_t *output_length)
{
  int32_t ret = 0;
  uint8_t try = 0;
  __IO uint8_t random[4];
  *output_length = 0;

  /* Get Random byte */
  while ((*output_length < length) && (ret == 0))
  {
    HAL_StatusTypeDef status = HAL_RNG_GenerateRandomNumber(&hrng, (uint32_t *)random);
    if (status != HAL_OK)
    {
      printf("[RNG] GenerateRandomNumber failed, status=%d, state=%d, errorCode=0x%lx\n", 
             (int)status, (int)hrng.State, (unsigned long)hrng.ErrorCode);
      /* retry when random number generated are not immediately available */
      if (try < 3)
      {
        try++;
      }
      else
      {
        ret = -1;
      }
    }
    else
    {
      for (uint8_t i = 0; (i < 4) && (*output_length < length) ; i++)
      {
        *output++ = random[i];
        *output_length += 1;
        random[i] = 0;
      }
    }
  }
  /* Just be extra sure that we didn't do it wrong */
  uint32_t flags = __HAL_RNG_GET_FLAG(&hrng, (RNG_FLAG_CECS | RNG_FLAG_SECS));
  if (flags != 0)
  {
    printf("[RNG] Error flags detected: 0x%lx\n", (unsigned long)flags);
    *output_length = 0;
  }
}

void RNG_DeInit(void)
{
  /*Disable the RNG peripheral */
  HAL_RNG_DeInit(&hrng);
  /* RNG Peripheral clock disable - assume we're the only users of RNG  */
  __HAL_RCC_RNG_CLK_DISABLE();

  users = 0;
}


/*  interface for mbed-crypto */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
  RNG_GetBytes(output, len, olen);
  if (*olen != len)
  {
    printf("[RNG] mbedtls_hardware_poll FAILED: gathered %d, expected %d\n", (int)*olen, (int)len);
    return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
  }
  return 0;
}

