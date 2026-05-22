/*
 * gcm_alt.c - STM32H5 MbedTLS GCM ALT
 *
 * TLS-safe version:
 * - Handles in-place buffers: input == output
 * - Handles unaligned MbedTLS TLS record buffers
 * - Handles 13-byte TLS AAD
 * - Handles non-4-byte payload lengths
 * - Converts key/IV bytes to BE uint32_t words for HAL
 * - Uses STM32 GCM CTR start block IV || 0x00000002 for 12-byte IV
 * - Avoids HAL_CRYP_SetConfig()
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
#include "stm32h5xx_hal.h"

#if defined(MBEDTLS_PLATFORM_C)
#include "mbedtls/platform.h"
#else
#include <stdlib.h>
#define mbedtls_calloc calloc
#define mbedtls_free   free
#endif

#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define GCM_TIMEOUT        5000
#define GCM_ROUND_UP_4(x) (((x) + 3U) & ~3U)

#define GCM_VALIDATE(cond) \
    do { if (!(cond)) return; } while (0)

#define GCM_VALIDATE_RET(cond) \
    do { if (!(cond)) return MBEDTLS_ERR_GCM_BAD_INPUT; } while (0)

#ifndef GET_UINT32_BE
#define GET_UINT32_BE(n,b,i)                            \
do {                                                    \
    (n) = ( (uint32_t) (b)[(i)    ] << 24 )             \
        | ( (uint32_t) (b)[(i) + 1] << 16 )             \
        | ( (uint32_t) (b)[(i) + 2] <<  8 )             \
        | ( (uint32_t) (b)[(i) + 3]       );            \
} while( 0 )
#endif

static void gcm_bytes_to_be_words(uint32_t *dst,
                                  const unsigned char *src,
                                  size_t len)
{
    size_t words = len / 4;

    for (size_t i = 0; i < words; i++)
        GET_UINT32_BE(dst[i], src, i * 4);
}

static int gcm_ct_memcmp(const unsigned char *a,
                         const unsigned char *b,
                         size_t n)
{
    unsigned char diff = 0;

    for (size_t i = 0; i < n; i++)
        diff |= a[i] ^ b[i];

    return diff;
}

static void gcm_zero_free(unsigned char *p, size_t len)
{
    if (p != NULL)
    {
        if (len > 0)
            mbedtls_platform_zeroize(p, len);

        mbedtls_free(p);
    }
}

void mbedtls_gcm_init(mbedtls_gcm_context *ctx)
{
    GCM_VALIDATE(ctx != NULL);

    memset(ctx, 0, sizeof(mbedtls_gcm_context));

#if defined(MBEDTLS_THREADING_C)
    mbedtls_mutex_init(&ctx->mutex);
#endif
}

void mbedtls_gcm_free(mbedtls_gcm_context *ctx)
{
    if (ctx == NULL)
        return;

#if defined(MBEDTLS_THREADING_C)
    mbedtls_mutex_free(&ctx->mutex);
#endif

    mbedtls_platform_zeroize(ctx, sizeof(mbedtls_gcm_context));
}

int mbedtls_gcm_setkey(mbedtls_gcm_context *ctx,
                       mbedtls_cipher_id_t cipher,
                       const unsigned char *key,
                       unsigned int keybits)
{
    GCM_VALIDATE_RET(ctx != NULL);
    GCM_VALIDATE_RET(key != NULL);

    if (cipher != MBEDTLS_CIPHER_ID_AES)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    if (keybits != 128 && keybits != 256)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    memset(ctx->key, 0, sizeof(ctx->key));
    memcpy(ctx->key, key, keybits / 8);
    ctx->keybits = keybits;

    return 0;
}

static HAL_StatusTypeDef stm32_gcm_hal_once(mbedtls_gcm_context *ctx,
                                            int mode,
                                            size_t length,
                                            const unsigned char *iv16_ctr,
                                            const unsigned char *add,
                                            size_t add_len,
                                            const unsigned char *input,
                                            unsigned char *output,
                                            unsigned char *tag)
{
    HAL_StatusTypeDef status;

    uint32_t key_words[8];
    uint32_t iv_words[4];
    uint32_t local_tag_words[4];

    memset(key_words, 0, sizeof(key_words));
    memset(iv_words, 0, sizeof(iv_words));
    memset(local_tag_words, 0, sizeof(local_tag_words));
    memset(&ctx->hcryp, 0, sizeof(ctx->hcryp));

    gcm_bytes_to_be_words(key_words, ctx->key, ctx->keybits / 8);
    gcm_bytes_to_be_words(iv_words, iv16_ctr, 16);

    __HAL_RCC_AES_CLK_ENABLE();

    ctx->hcryp.Instance = AES;

    /*
     * Payload/header are byte arrays from MbedTLS.
     * Key/IV are already prepared as uint32_t words.
     */
    ctx->hcryp.Init.DataType = CRYP_DATATYPE_8B;
    ctx->hcryp.Init.DataWidthUnit = CRYP_DATAWIDTHUNIT_BYTE;
    ctx->hcryp.Init.HeaderWidthUnit = CRYP_HEADERWIDTHUNIT_BYTE;

    ctx->hcryp.Init.KeySize =
        (ctx->keybits == 128) ? CRYP_KEYSIZE_128B : CRYP_KEYSIZE_256B;

    ctx->hcryp.Init.Algorithm = CRYP_AES_GCM_GMAC;
    ctx->hcryp.Init.pKey = key_words;
    ctx->hcryp.Init.pInitVect = iv_words;

    ctx->hcryp.Init.Header = (uint32_t *)add;
    ctx->hcryp.Init.HeaderSize = add_len;

    status = HAL_CRYP_Init(&ctx->hcryp);

    if (status != HAL_OK)
    {
        __HAL_RCC_AES_CLK_DISABLE();
        goto cleanup;
    }

    if (mode == MBEDTLS_GCM_ENCRYPT)
    {
        status = HAL_CRYP_Encrypt(&ctx->hcryp,
                                  (uint32_t *)input,
                                  length,
                                  (uint32_t *)output,
                                  GCM_TIMEOUT);

        if (status == HAL_OK && tag != NULL)
        {
            status = HAL_CRYPEx_AESGCM_GenerateAuthTAG(&ctx->hcryp,
                                                       local_tag_words,
                                                       GCM_TIMEOUT);

            if (status == HAL_OK)
                memcpy(tag, local_tag_words, 16);
        }
    }
    else
    {
        status = HAL_CRYP_Decrypt(&ctx->hcryp,
                                  (uint32_t *)input,
                                  length,
                                  (uint32_t *)output,
                                  GCM_TIMEOUT);
    }

    HAL_CRYP_DeInit(&ctx->hcryp);
    __HAL_RCC_AES_CLK_DISABLE();

cleanup:
    mbedtls_platform_zeroize(key_words, sizeof(key_words));
    mbedtls_platform_zeroize(iv_words, sizeof(iv_words));
    mbedtls_platform_zeroize(local_tag_words, sizeof(local_tag_words));

    return status;
}

static int stm32_gcm_crypt_and_tag(mbedtls_gcm_context *ctx,
                                   int mode,
                                   size_t length,
                                   const unsigned char *iv,
                                   size_t iv_len,
                                   const unsigned char *add,
                                   size_t add_len,
                                   const unsigned char *input,
                                   unsigned char *output,
                                   size_t tag_len,
                                   unsigned char *tag)
{
    HAL_StatusTypeDef status;

    unsigned char generated_tag[16];

    unsigned char *iv_buf = NULL;
    unsigned char *aad_buf = NULL;

    unsigned char *plain_buf = NULL;
    unsigned char *cipher_buf = NULL;
    unsigned char *verify_cipher_buf = NULL;

    size_t iv_alloc_len = 16;
    size_t aad_alloc_len;
    size_t data_alloc_len;

    GCM_VALIDATE_RET(ctx != NULL);
    GCM_VALIDATE_RET(iv != NULL);
    GCM_VALIDATE_RET(tag != NULL);
    GCM_VALIDATE_RET(tag_len <= 16);
    GCM_VALIDATE_RET(input != NULL || length == 0);
    GCM_VALIDATE_RET(output != NULL || length == 0);

    if (iv_len != 12 && iv_len != 16)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    if (ctx->keybits != 128 && ctx->keybits != 256)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    memset(generated_tag, 0, sizeof(generated_tag));

#if defined(MBEDTLS_THREADING_C)
    if (mbedtls_mutex_lock(&ctx->mutex) != 0)
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
#endif

#if 1
    printf("GCM ALT: mode=%d len=%lu iv_len=%lu aad_len=%lu tag_len=%lu in=%p out=%p same=%d\r\n",
           mode,
           (unsigned long)length,
           (unsigned long)iv_len,
           (unsigned long)add_len,
           (unsigned long)tag_len,
           input,
           output,
           input == output);
#endif

    aad_alloc_len  = GCM_ROUND_UP_4((add_len > 0) ? add_len : 1);
    data_alloc_len = GCM_ROUND_UP_4((length > 0) ? length : 1);

    iv_buf     = mbedtls_calloc(1, iv_alloc_len);
    aad_buf    = mbedtls_calloc(1, aad_alloc_len);
    plain_buf  = mbedtls_calloc(1, data_alloc_len);
    cipher_buf = mbedtls_calloc(1, data_alloc_len);

    if (iv_buf == NULL || aad_buf == NULL ||
        plain_buf == NULL || cipher_buf == NULL)
    {
        goto hw_failed;
    }

    /*
     * STM32 CRYP GCM expects the initial counter block.
     *
     * For a 96-bit GCM IV:
     *   J0        = IV || 0x00000001
     *   CTR start = IV || 0x00000002
     *
     * The HAL pInitVect is the CTR start block.
     */
    memset(iv_buf, 0, iv_alloc_len);

    if (iv_len == 12)
    {
        memcpy(iv_buf, iv, 12);

        iv_buf[12] = 0x00;
        iv_buf[13] = 0x00;
        iv_buf[14] = 0x00;
        iv_buf[15] = 0x02;
    }
    else
    {
        memcpy(iv_buf, iv, 16);
    }

    if (add_len > 0 && add != NULL)
        memcpy(aad_buf, add, add_len);

    if (mode == MBEDTLS_GCM_ENCRYPT)
    {
        if (length > 0)
            memcpy(plain_buf, input, length);

        status = stm32_gcm_hal_once(ctx,
                                    MBEDTLS_GCM_ENCRYPT,
                                    length,
                                    iv_buf,
                                    aad_buf,
                                    add_len,
                                    plain_buf,
                                    cipher_buf,
                                    generated_tag);

        if (status != HAL_OK)
            goto hw_failed;

        if (length > 0)
            memcpy(output, cipher_buf, length);

        memcpy(tag, generated_tag, tag_len);
    }
    else
    {
        verify_cipher_buf = mbedtls_calloc(1, data_alloc_len);

        if (verify_cipher_buf == NULL)
            goto hw_failed;

        if (length > 0)
            memcpy(cipher_buf, input, length);

        status = stm32_gcm_hal_once(ctx,
                                    MBEDTLS_GCM_DECRYPT,
                                    length,
                                    iv_buf,
                                    aad_buf,
                                    add_len,
                                    cipher_buf,
                                    plain_buf,
                                    NULL);

        if (status != HAL_OK)
            goto hw_failed;

        status = stm32_gcm_hal_once(ctx,
                                    MBEDTLS_GCM_ENCRYPT,
                                    length,
                                    iv_buf,
                                    aad_buf,
                                    add_len,
                                    plain_buf,
                                    verify_cipher_buf,
                                    generated_tag);

        if (status != HAL_OK)
            goto hw_failed;

#if 0
        printf("received tag : ");
        for (size_t i = 0; i < tag_len; i++) printf("%02X", tag[i]);
        printf("\r\n");

        printf("generated tag: ");
        for (size_t i = 0; i < tag_len; i++) printf("%02X", generated_tag[i]);
        printf("\r\n");
#endif

        if (gcm_ct_memcmp(generated_tag, tag, tag_len) != 0)
            goto auth_failed;

        if (length > 0)
            memcpy(output, plain_buf, length);
    }

    gcm_zero_free(iv_buf, iv_alloc_len);
    gcm_zero_free(aad_buf, aad_alloc_len);
    gcm_zero_free(plain_buf, data_alloc_len);
    gcm_zero_free(cipher_buf, data_alloc_len);
    gcm_zero_free(verify_cipher_buf, data_alloc_len);

#if defined(MBEDTLS_THREADING_C)
    mbedtls_mutex_unlock(&ctx->mutex);
#endif

    return 0;

auth_failed:

    if (output != NULL && length > 0)
        mbedtls_platform_zeroize(output, length);

    gcm_zero_free(iv_buf, iv_alloc_len);
    gcm_zero_free(aad_buf, aad_alloc_len);
    gcm_zero_free(plain_buf, data_alloc_len);
    gcm_zero_free(cipher_buf, data_alloc_len);
    gcm_zero_free(verify_cipher_buf, data_alloc_len);

#if defined(MBEDTLS_THREADING_C)
    mbedtls_mutex_unlock(&ctx->mutex);
#endif

    return MBEDTLS_ERR_GCM_AUTH_FAILED;

hw_failed:

    gcm_zero_free(iv_buf, iv_alloc_len);
    gcm_zero_free(aad_buf, aad_alloc_len);
    gcm_zero_free(plain_buf, data_alloc_len);
    gcm_zero_free(cipher_buf, data_alloc_len);
    gcm_zero_free(verify_cipher_buf, data_alloc_len);

#if defined(MBEDTLS_THREADING_C)
    mbedtls_mutex_unlock(&ctx->mutex);
#endif

    return MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
}

int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context *ctx,
                              int mode,
                              size_t length,
                              const unsigned char *iv,
                              size_t iv_len,
                              const unsigned char *add,
                              size_t add_len,
                              const unsigned char *input,
                              unsigned char *output,
                              size_t tag_len,
                              unsigned char *tag)
{
    return stm32_gcm_crypt_and_tag(ctx,
                                   mode,
                                   length,
                                   iv,
                                   iv_len,
                                   add,
                                   add_len,
                                   input,
                                   output,
                                   tag_len,
                                   tag);
}

int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context *ctx,
                             size_t length,
                             const unsigned char *iv,
                             size_t iv_len,
                             const unsigned char *add,
                             size_t add_len,
                             const unsigned char *tag,
                             size_t tag_len,
                             const unsigned char *input,
                             unsigned char *output)
{
    return stm32_gcm_crypt_and_tag(ctx,
                                   MBEDTLS_GCM_DECRYPT,
                                   length,
                                   iv,
                                   iv_len,
                                   add,
                                   add_len,
                                   input,
                                   output,
                                   tag_len,
                                   (unsigned char *)tag);
}

int mbedtls_gcm_starts(mbedtls_gcm_context *ctx,
                       int mode,
                       const unsigned char *iv,
                       size_t iv_len)
{
    (void)ctx;
    (void)mode;
    (void)iv;
    (void)iv_len;

    return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
}

int mbedtls_gcm_update(mbedtls_gcm_context *ctx,
                       const unsigned char *input,
                       size_t input_length,
                       unsigned char *output,
                       size_t output_size,
                       size_t *output_length)
{
    (void)ctx;
    (void)input;
    (void)input_length;
    (void)output;
    (void)output_size;

    if (output_length != NULL)
        *output_length = 0;

    return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
}

int mbedtls_gcm_finish(mbedtls_gcm_context *ctx,
                       unsigned char *output,
                       size_t output_size,
                       size_t *output_length,
                       unsigned char *tag,
                       size_t tag_len)
{
    (void)ctx;
    (void)output;
    (void)output_size;
    (void)tag;
    (void)tag_len;

    if (output_length != NULL)
        *output_length = 0;

    return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
}

#endif /* MBEDTLS_GCM_ALT */
#endif /* MBEDTLS_GCM_C */