#include "mbedtls/platform_time.h"
#include "stm32h5xx_hal.h"

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)HAL_GetTick();
}
