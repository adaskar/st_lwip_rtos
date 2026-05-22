/*
 * gcm_alt.c — STM32H5 MbedTLS 3.x GCM hardware acceleration
 *
 * Implements the mbedTLS 3.x streaming GCM API:
 *   mbedtls_gcm_starts()     — configure mode + IV
 *   mbedtls_gcm_update_ad()  — feed AAD (NEW in 3.x, replaces add in starts)
 *   mbedtls_gcm_update()     — feed payload chunks
 *   mbedtls_gcm_finish()     — generate tag (output+tag in 3.x signature)
 *   mbedtls_gcm_crypt_and_tag()  — convenience wrapper
 *   mbedtls_gcm_auth_decrypt()   — decrypt + verify
 *
 * Key design points:
 *   - HAL_CRYP_Init() once at setkey, SetConfig() per record
 *   - ctx_save_cr save/restore for multi-session hardware sharing
 *   - Single global gcm_hw_mutex serialises physical AES peripheral
 *   - CRYP_DATATYPE_8B — byte buffers passed directly, no word packing
 *   - No heap allocation per record
 *   - ~248 bytes RAM per context
 */

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#if defined(MBEDTLS_GCM_C)
#if defined(MBEDTLS_GCM_ALT)

#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/error.h"
#include "gcm_alt.h"
#include "stm32h5xx_hal.h"

#if defined(MBEDTLS_PLATFORM_C)
#include "mbedtls/platform.h"
#else
#include <stdlib.h>
#define mbedtls_calloc  calloc
#define mbedtls_free    free
#endif

#if defined(MBEDTLS_THREADING_C)
#include "mbedtls/threading.h"
#endif

#include <string.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define GCM_TIMEOUT     5000U
#define IV_LENGTH       12U       /* hardware supports 96-bit IV only      */

/* -------------------------------------------------------------------------
 * Validation macros
 *
 * MBEDTLS_INTERNAL_VALIDATE / MBEDTLS_INTERNAL_VALIDATE_RET are internal
 * mbedTLS symbols not guaranteed to be visible in all versions.
 * We define our own simple guards that match the expected behaviour.
 * ---------------------------------------------------------------------- */

#define GCM_VALIDATE_RET(cond)              \
    do {                                    \
        if (!(cond))                        \
            return MBEDTLS_ERR_GCM_BAD_INPUT; \
    } while(0)

#define GCM_VALIDATE(cond)                  \
    do {                                    \
        if (!(cond))                        \
            return;                         \
    } while(0)

/* -------------------------------------------------------------------------
 * GET_UINT32_BE — read big-endian uint32 from byte buffer
 * ---------------------------------------------------------------------- */

#define GET_UINT32_BE(n, b, i)                    \
    do {                                          \
        (n) = ( (uint32_t)(b)[(i)    ] << 24U )  \
            | ( (uint32_t)(b)[(i) + 1] << 16U )  \
            | ( (uint32_t)(b)[(i) + 2] <<  8U )  \
            | ( (uint32_t)(b)[(i) + 3]        ); \
    } while(0)

/* -------------------------------------------------------------------------
 * Global hardware mutex
 *
 * Serialises access to the single physical AES peripheral across all
 * GCM contexts. Compiles away entirely without MBEDTLS_THREADING_C.
 * ---------------------------------------------------------------------- */

#if defined(MBEDTLS_THREADING_C)

static mbedtls_threading_mutex_t gcm_hw_mutex;
static volatile int              gcm_hw_mutex_ready = 0;

void gcm_alt_init(void)
{
    if (!gcm_hw_mutex_ready)
    {
        mbedtls_mutex_init(&gcm_hw_mutex);
        gcm_hw_mutex_ready = 1;
    }
}

void gcm_alt_deinit(void)
{
    if (gcm_hw_mutex_ready)
    {
        mbedtls_mutex_free(&gcm_hw_mutex);
        gcm_hw_mutex_ready = 0;
    }
}

#define GCM_HW_LOCK()                                        \
    do {                                                     \
        if (mbedtls_mutex_lock(&gcm_hw_mutex) != 0)         \
            return MBEDTLS_ERR_THREADING_MUTEX_ERROR;        \
    } while(0)

#define GCM_HW_UNLOCK()                                      \
    do {                                                     \
        mbedtls_mutex_unlock(&gcm_hw_mutex);                 \
    } while(0)

#define GCM_HW_UNLOCK_VOID()                                 \
    mbedtls_mutex_unlock(&gcm_hw_mutex)

#else /* !MBEDTLS_THREADING_C */

#define GCM_HW_LOCK()        do {} while(0)
#define GCM_HW_UNLOCK()      do {} while(0)
#define GCM_HW_UNLOCK_VOID() do {} while(0)

#endif /* MBEDTLS_THREADING_C */

/* =========================================================================
 * Public MbedTLS 3.x API implementation
 * ====================================================================== */

/* -------------------------------------------------------------------------
 * mbedtls_gcm_init()
 * ---------------------------------------------------------------------- */

void mbedtls_gcm_init(mbedtls_gcm_context *ctx)
{
    GCM_VALIDATE(ctx != NULL);
    memset(ctx, 0, sizeof(mbedtls_gcm_context));

#if defined(MBEDTLS_THREADING_C)
    mbedtls_mutex_init(&ctx->mutex);
#endif
}

/* -------------------------------------------------------------------------
 * mbedtls_gcm_free()
 * ---------------------------------------------------------------------- */

void mbedtls_gcm_free(mbedtls_gcm_context *ctx)
{
    if (ctx == NULL)
        return;

#if defined(MBEDTLS_THREADING_C)
    mbedtls_mutex_lock(&gcm_hw_mutex);
#endif

    __HAL_RCC_AES_CLK_ENABLE();
    HAL_CRYP_DeInit(&ctx->hcryp_gcm);
    __HAL_RCC_AES_CLK_DISABLE();

#if defined(MBEDTLS_THREADING_C)
    mbedtls_mutex_unlock(&gcm_hw_mutex);
    mbedtls_mutex_free(&ctx->mutex);
#endif

    mbedtls_platform_zeroize(ctx, sizeof(mbedtls_gcm_context));
}

/* -------------------------------------------------------------------------
 * mbedtls_gcm_setkey()
 *
 * Converts key to big-endian words and calls HAL_CRYP_Init() ONCE.
 * All subsequent operations reuse this handle via SetConfig().
 * ---------------------------------------------------------------------- */

int mbedtls_gcm_setkey(mbedtls_gcm_context *ctx,
                       mbedtls_cipher_id_t  cipher,
                       const unsigned char *key,
                       unsigned int         keybits)
{
    int ret = 0;

    GCM_VALIDATE_RET(ctx != NULL);
    GCM_VALIDATE_RET(key != NULL);

    if (cipher != MBEDTLS_CIPHER_ID_AES)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    switch (keybits)
    {
        case 128: ctx->hcryp_gcm.Init.KeySize = CRYP_KEYSIZE_128B; break;
        case 256: ctx->hcryp_gcm.Init.KeySize = CRYP_KEYSIZE_256B; break;
        default:  return MBEDTLS_ERR_GCM_BAD_INPUT;
    }

    /* Convert key bytes to HAL-ready big-endian words */
    for (unsigned int i = 0; i < (keybits / 32U); i++)
        GET_UINT32_BE(ctx->gcm_key[i], key, 4U * i);

    GCM_HW_LOCK();

    __HAL_RCC_AES_CLK_ENABLE();

    ctx->hcryp_gcm.Instance                 = AES;
    ctx->hcryp_gcm.Init.DataType            = CRYP_DATATYPE_8B;
    ctx->hcryp_gcm.Init.DataWidthUnit       = CRYP_DATAWIDTHUNIT_BYTE;
    ctx->hcryp_gcm.Init.HeaderWidthUnit     = CRYP_HEADERWIDTHUNIT_BYTE;
    ctx->hcryp_gcm.Init.Algorithm           = CRYP_AES_GCM_GMAC;
    ctx->hcryp_gcm.Init.KeyMode             = CRYP_KEYMODE_NORMAL;
    ctx->hcryp_gcm.Init.pKey                = ctx->gcm_key;
    ctx->hcryp_gcm.Init.pInitVect           = NULL;
    ctx->hcryp_gcm.Init.Header              = NULL;
    ctx->hcryp_gcm.Init.HeaderSize          = 0U;
    ctx->hcryp_gcm.Init.KeyIVConfigSkip     = CRYP_KEYIVCONFIG_ALWAYS;

    if (HAL_CRYP_Init(&ctx->hcryp_gcm) != HAL_OK)
    {
        ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
        goto exit;
    }

    /* Save initial CR state */
    ctx->ctx_save_cr = ctx->hcryp_gcm.Instance->CR;

exit:
    __HAL_RCC_AES_CLK_DISABLE();
    GCM_HW_UNLOCK();
    return ret;
}

/* -------------------------------------------------------------------------
 * mbedtls_gcm_starts()  — mbedTLS 3.x signature
 *
 * Configures mode and IV only. AAD is fed separately via update_ad().
 * IV counter block: iv[0..11] || 0x00000002 (CTR start = J0+1)
 * ---------------------------------------------------------------------- */

int mbedtls_gcm_starts(mbedtls_gcm_context *ctx,
                       int                  mode,
                       const unsigned char *iv,
                       size_t               iv_len)
{
    int ret = 0;

    /*
     * iv_32b: IV counter block — stack allocated, word aligned.
     * Must remain valid until HAL_CRYP_SetConfig() returns.
     */
    __ALIGN_BEGIN static uint32_t iv_32b[4] __ALIGN_END;

    GCM_VALIDATE_RET(ctx != NULL);
    GCM_VALIDATE_RET(iv  != NULL);

    /* Hardware supports 96-bit IV only */
    if (iv_len != IV_LENGTH)
        return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;

    GCM_HW_LOCK();

    __HAL_RCC_AES_CLK_ENABLE();

    /* Restore this context's hardware state */
    ctx->hcryp_gcm.Instance->CR = ctx->ctx_save_cr;

    ctx->mode    = mode;
    ctx->len     = 0U;
    ctx->add_len = 0U;

    /*
     * Build CTR start block for 96-bit IV:
     *   GCM spec J0     = IV || 0x00000001
     *   CTR start       = IV || 0x00000002   (inc32(J0))
     */
    GET_UINT32_BE(iv_32b[0], iv, 0U);
    GET_UINT32_BE(iv_32b[1], iv, 4U);
    GET_UINT32_BE(iv_32b[2], iv, 8U);
    iv_32b[3] = 0x00000002U;

    ctx->hcryp_gcm.Init.pInitVect      = iv_32b;
    ctx->hcryp_gcm.Init.Header         = NULL;
    ctx->hcryp_gcm.Init.HeaderSize     = 0U;

    /*
     * CRYP_KEYIVCONFIG_ONCE: IV loaded on this SetConfig call.
     * Key stays loaded from setkey. Not reloaded on update() calls.
     */
    ctx->hcryp_gcm.Init.KeyIVConfigSkip = CRYP_KEYIVCONFIG_ONCE;

    if (HAL_CRYP_SetConfig(&ctx->hcryp_gcm, &ctx->hcryp_gcm.Init) != HAL_OK)
    {
        ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
        goto exit;
    }

    ctx->ctx_save_cr = ctx->hcryp_gcm.Instance->CR;

exit:
    __HAL_RCC_AES_CLK_DISABLE();
    GCM_HW_UNLOCK();
    return ret;
}

/* -------------------------------------------------------------------------
 * mbedtls_gcm_update_ad()  — mbedTLS 3.x NEW function
 *
 * Feed Additional Authenticated Data. Must be called after starts()
 * and before the first update(). May be called multiple times.
 * ---------------------------------------------------------------------- */

int mbedtls_gcm_update_ad(mbedtls_gcm_context *ctx,
                          const unsigned char *add,
                          size_t               add_len)
{
    int ret = 0;

    GCM_VALIDATE_RET(ctx != NULL);
    GCM_VALIDATE_RET(add_len == 0U || add != NULL);

    if (add_len == 0U)
        return 0;

    GCM_HW_LOCK();

    __HAL_RCC_AES_CLK_ENABLE();

    /* Restore this context's hardware state */
    ctx->hcryp_gcm.Instance->CR = ctx->ctx_save_cr;

    /*
     * Feed AAD to hardware via SetConfig with Header pointer.
     * CRYP_DATATYPE_8B + CRYP_HEADERWIDTHUNIT_BYTE means HAL
     * accepts raw bytes and handles alignment internally.
     */
    ctx->hcryp_gcm.Init.Header     = (uint32_t *)(uintptr_t)add;
    ctx->hcryp_gcm.Init.HeaderSize = (uint32_t)add_len;

    if (HAL_CRYP_SetConfig(&ctx->hcryp_gcm, &ctx->hcryp_gcm.Init) != HAL_OK)
    {
        ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
        goto exit;
    }

    ctx->add_len    += add_len;
    ctx->ctx_save_cr = ctx->hcryp_gcm.Instance->CR;

exit:
    __HAL_RCC_AES_CLK_DISABLE();
    GCM_HW_UNLOCK();
    return ret;
}

/* -------------------------------------------------------------------------
 * mbedtls_gcm_update()  — mbedTLS 3.x signature
 *
 * Process payload. May be called multiple times. Hardware GCM state
 * (GHASH accumulator) stays live between calls via ctx_save_cr.
 *
 * In-place (input == output) is safe in polling mode.
 * ---------------------------------------------------------------------- */

int mbedtls_gcm_update(mbedtls_gcm_context  *ctx,
                       const unsigned char  *input,
                       size_t                input_length,
                       unsigned char        *output,
                       size_t                output_size,
                       size_t               *output_length)
{
    int ret = 0;

    GCM_VALIDATE_RET(ctx != NULL);
    GCM_VALIDATE_RET(input_length == 0U || input  != NULL);
    GCM_VALIDATE_RET(input_length == 0U || output != NULL);

    if (output_length != NULL)
        *output_length = 0U;

    if (input_length == 0U)
        return 0;

    if (output_size < input_length)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    /* Partial overlap guard — full in-place is fine */
    if (output > input && (size_t)(output - input) < input_length)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    /* Total payload length overflow guard */
    if (((ctx->len + input_length) < ctx->len) ||
        ((uint64_t)(ctx->len + input_length) > 0xFFFFFFFE0ULL))
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    GCM_HW_LOCK();

    __HAL_RCC_AES_CLK_ENABLE();

    /* Restore this context's hardware state */
    ctx->hcryp_gcm.Instance->CR = ctx->ctx_save_cr;

    ctx->len += input_length;

    if (ctx->mode == MBEDTLS_GCM_ENCRYPT)
    {
        if (HAL_CRYP_Encrypt(&ctx->hcryp_gcm,
                             (uint32_t *)(uintptr_t)input,
                             (uint16_t)input_length,
                             (uint32_t *)output,
                             GCM_TIMEOUT) != HAL_OK)
        {
            ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
            goto exit;
        }
    }
    else
    {
        if (HAL_CRYP_Decrypt(&ctx->hcryp_gcm,
                             (uint32_t *)(uintptr_t)input,
                             (uint16_t)input_length,
                             (uint32_t *)output,
                             GCM_TIMEOUT) != HAL_OK)
        {
            ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
            goto exit;
        }
    }

    if (output_length != NULL)
        *output_length = input_length;

    ctx->ctx_save_cr = ctx->hcryp_gcm.Instance->CR;

exit:
    __HAL_RCC_AES_CLK_DISABLE();
    GCM_HW_UNLOCK();
    return ret;
}

/* -------------------------------------------------------------------------
 * mbedtls_gcm_finish()  — mbedTLS 3.x signature
 *
 * Generate authentication tag. The 3.x API adds output/output_size/
 * output_length parameters for any remaining buffered bytes — since
 * this hardware implementation processes data immediately in update()
 * there are never any buffered bytes to flush here.
 * ---------------------------------------------------------------------- */

int mbedtls_gcm_finish(mbedtls_gcm_context  *ctx,
                       unsigned char        *output,
                       size_t                output_size,
                       size_t               *output_length,
                       unsigned char        *tag,
                       size_t                tag_len)
{
    int ret = 0;

    __ALIGN_BEGIN uint8_t mac[16] __ALIGN_END;

    GCM_VALIDATE_RET(ctx != NULL);
    GCM_VALIDATE_RET(tag != NULL);

    /* Suppress unused parameter warnings — no buffered output in HW mode */
    (void)output;
    (void)output_size;

    if (output_length != NULL)
        *output_length = 0U;

    if (tag_len < 4U || tag_len > 16U)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    GCM_HW_LOCK();

    __HAL_RCC_AES_CLK_ENABLE();

    /* Restore this context's hardware state */
    ctx->hcryp_gcm.Instance->CR = ctx->ctx_save_cr;

    memset(mac, 0, sizeof(mac));

    if (HAL_CRYPEx_AESGCM_GenerateAuthTAG(&ctx->hcryp_gcm,
                                          (uint32_t *)(uintptr_t)mac,
                                          GCM_TIMEOUT) != HAL_OK)
    {
        ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
        goto exit;
    }

    /* Copy only requested tag_len bytes (TLS uses 16, others may use less) */
    memcpy(tag, mac, tag_len);
    mbedtls_platform_zeroize(mac, sizeof(mac));

    ctx->ctx_save_cr = ctx->hcryp_gcm.Instance->CR;

exit:
    __HAL_RCC_AES_CLK_DISABLE();
    GCM_HW_UNLOCK();
    return ret;
}

/* -------------------------------------------------------------------------
 * mbedtls_gcm_crypt_and_tag()
 *
 * Convenience wrapper for one-shot encrypt/decrypt + tag generation.
 * Chains starts → update_ad → update → finish.
 * ---------------------------------------------------------------------- */

int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context  *ctx,
                               int                   mode,
                               size_t                length,
                               const unsigned char  *iv,
                               size_t                iv_len,
                               const unsigned char  *add,
                               size_t                add_len,
                               const unsigned char  *input,
                               unsigned char        *output,
                               size_t                tag_len,
                               unsigned char        *tag)
{
    int    ret;
    size_t olen = 0U;

    GCM_VALIDATE_RET(ctx  != NULL);
    GCM_VALIDATE_RET(iv   != NULL);
    GCM_VALIDATE_RET(add_len == 0U || add    != NULL);
    GCM_VALIDATE_RET(length  == 0U || input  != NULL);
    GCM_VALIDATE_RET(length  == 0U || output != NULL);
    GCM_VALIDATE_RET(tag  != NULL);

    ret = mbedtls_gcm_starts(ctx, mode, iv, iv_len);
    if (ret != 0)
        return ret;

    if (add_len > 0U)
    {
        ret = mbedtls_gcm_update_ad(ctx, add, add_len);
        if (ret != 0)
            return ret;
    }

    ret = mbedtls_gcm_update(ctx, input, length, output, length, &olen);
    if (ret != 0)
        return ret;

    return mbedtls_gcm_finish(ctx, NULL, 0U, NULL, tag, tag_len);
}

/* -------------------------------------------------------------------------
 * mbedtls_gcm_auth_decrypt()
 *
 * Decrypt and verify authentication tag.
 * Constant-time tag comparison. Output zeroized on failure.
 * ---------------------------------------------------------------------- */

int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context  *ctx,
                              size_t                length,
                              const unsigned char  *iv,
                              size_t                iv_len,
                              const unsigned char  *add,
                              size_t                add_len,
                              const unsigned char  *tag,
                              size_t                tag_len,
                              const unsigned char  *input,
                              unsigned char        *output)
{
    int           ret;
    unsigned char check_tag[16];
    unsigned char diff;
    size_t        i;

    GCM_VALIDATE_RET(ctx  != NULL);
    GCM_VALIDATE_RET(iv   != NULL);
    GCM_VALIDATE_RET(add_len == 0U || add   != NULL);
    GCM_VALIDATE_RET(tag  != NULL);
    GCM_VALIDATE_RET(length  == 0U || input  != NULL);
    GCM_VALIDATE_RET(length  == 0U || output != NULL);

    ret = mbedtls_gcm_crypt_and_tag(ctx,
                                    MBEDTLS_GCM_DECRYPT,
                                    length,
                                    iv, iv_len,
                                    add, add_len,
                                    input, output,
                                    tag_len, check_tag);
    if (ret != 0)
        return ret;

    /* Constant-time comparison — all bytes always evaluated */
    diff = 0U;
    for (i = 0U; i < tag_len; i++)
        diff |= tag[i] ^ check_tag[i];

    mbedtls_platform_zeroize(check_tag, sizeof(check_tag));

    if (diff != 0U)
    {
        if (output != NULL && length > 0U)
            mbedtls_platform_zeroize(output, length);
        return MBEDTLS_ERR_GCM_AUTH_FAILED;
    }

    return 0;
}

#endif /* MBEDTLS_GCM_ALT */
#endif /* MBEDTLS_GCM_C */