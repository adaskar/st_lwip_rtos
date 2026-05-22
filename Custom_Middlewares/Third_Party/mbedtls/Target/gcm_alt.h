#ifndef MBEDTLS_GCM_ALT_H
#define MBEDTLS_GCM_ALT_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MBEDTLS_GCM_ALT)

#include "stm32h5xx_hal.h"

#include "mbedtls/threading.h"

/**
 * \brief          The GCM context-type definition.
 */
typedef struct mbedtls_gcm_context
{
    /*
     * STM32 HAL CRYP handle
     */
    CRYP_HandleTypeDef hcryp;

    /*
     * AES key size in bits
     */
    uint32_t keybits;

    /*
     * AES key storage
     */
    unsigned char key[32];

#if defined(MBEDTLS_THREADING_C)
    /*
     * Thread protection mutex
     */
    mbedtls_threading_mutex_t mutex;
#endif

}
mbedtls_gcm_context;

#endif /* MBEDTLS_GCM_ALT */

#ifdef __cplusplus
}
#endif

#endif /* MBEDTLS_GCM_ALT_H */