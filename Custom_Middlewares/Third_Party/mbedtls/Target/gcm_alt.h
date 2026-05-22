/* gcm_alt.h */
#ifndef MBEDTLS_GCM_ALT_H
#define MBEDTLS_GCM_ALT_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MBEDTLS_GCM_ALT)

#include "stm32h5xx_hal.h"
#include <stdint.h>

/*
 * Do NOT include mbedtls/threading.h here unconditionally —
 * it is only available when MBEDTLS_THREADING_C is defined.
 */
#if defined(MBEDTLS_THREADING_C)
#include "mbedtls/threading.h"
#endif

typedef struct mbedtls_gcm_context
{
    /*
     * HAL CRYP handle.
     * Initialised once at mbedtls_gcm_setkey().
     * IV and AAD reconfigured per-operation via HAL_CRYP_SetConfig()
     * with CRYP_KEYIVCONFIG_ONCE — key is NOT reloaded every record.
     */
    CRYP_HandleTypeDef hcryp_gcm;

    /*
     * AES key in HAL-ready big-endian word format.
     * Computed once at setkey from raw key bytes.
     */
    uint32_t gcm_key[8];

    /*
     * AES peripheral CR register snapshot.
     * Saved after every HAL call, restored before the next.
     * Enables correct multi-session interleaving on single hardware.
     */
    uint32_t ctx_save_cr;

    /*
     * Streaming operation state (mbedTLS 3.x API).
     * Set by mbedtls_gcm_starts(), used through update/finish.
     */
    int    mode;      /* MBEDTLS_GCM_ENCRYPT or MBEDTLS_GCM_DECRYPT */
    size_t len;       /* cumulative payload bytes processed           */
    size_t add_len;   /* AAD bytes processed via update_ad()          */

#if defined(MBEDTLS_THREADING_C)
    mbedtls_threading_mutex_t mutex;
#endif

} mbedtls_gcm_context;

#endif /* MBEDTLS_GCM_ALT */

#ifdef __cplusplus
}
#endif

#endif /* MBEDTLS_GCM_ALT_H */