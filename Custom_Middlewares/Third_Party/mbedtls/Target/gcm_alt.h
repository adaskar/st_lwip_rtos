#ifndef MBEDTLS_GCM_ALT_H
#define MBEDTLS_GCM_ALT_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MBEDTLS_GCM_ALT)

#include "stm32h5xx_hal.h"
#include "mbedtls/threading.h"

typedef struct mbedtls_gcm_context
{
    CRYP_HandleTypeDef hcryp;

    uint32_t keybits;

    unsigned char key[32];

    /*
     * Cached HAL-ready AES key words.
     * Avoids converting key bytes on every TLS record.
     */
    uint32_t key_words[8];

#if defined(MBEDTLS_THREADING_C)
    mbedtls_threading_mutex_t mutex;
#endif

} mbedtls_gcm_context;

#endif /* MBEDTLS_GCM_ALT */

#ifdef __cplusplus
}
#endif

#endif /* MBEDTLS_GCM_ALT_H */