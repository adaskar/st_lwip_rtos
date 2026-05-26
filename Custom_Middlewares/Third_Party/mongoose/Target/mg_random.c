#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "stm32h5xx_hal.h"

extern RNG_HandleTypeDef hrng;

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
