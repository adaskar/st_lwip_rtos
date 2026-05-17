/*
 * ecdsa_alt.c — STM32H5 PKA-accelerated ECDSA for mbedTLS 3.x
 *
 * Implements MBEDTLS_ECDSA_SIGN_ALT and MBEDTLS_ECDSA_VERIFY_ALT using the
 * STM32H5 Public Key Accelerator (PKA) peripheral.
 *
 * Supported curves: all Short Weierstrass prime-field curves that the PKA
 * accepts (SECP192R1 through SECP521R1, BP256R1/384R1/512R1).
 */

/* Allow direct field access on mbedtls_ecp_point / mbedtls_ecp_group.
 * This is explicitly supported for hardware port code in mbedTLS 3.x. */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "mbedtls/ecdsa.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/error.h"
#include "mbedtls/bignum.h"

#if defined(MBEDTLS_ECDSA_SIGN_ALT) || defined(MBEDTLS_ECDSA_VERIFY_ALT)

#include "stm32h5xx_hal.h"
#include <string.h>

#define ST_PKA_TIMEOUT 10000U

/* -----------------------------------------------------------------------
 * Internal helper: resolve the PKA 'coefSign' and |A| binary for a group.
 *
 * mbedTLS 3.x stores A as (P - 3) for curves where a = -3 (NIST SECP*).
 * The PKA wants the absolute value and a 1-bit sign flag.
 * coefSign: 0 = positive, 1 = negative
 * ----------------------------------------------------------------------- */
static int ecdsa_alt_get_a( const mbedtls_ecp_group *grp,
                             uint8_t  *coef_bin,
                             size_t    modulusSize,
                             uint32_t *coefSign )
{
    int ret = 0;
    mbedtls_mpi P_minus_3, three;

    mbedtls_mpi_init( &P_minus_3 );
    mbedtls_mpi_init( &three );

    MBEDTLS_MPI_CHK( mbedtls_mpi_lset( &three, 3 ) );

    /* If A == 0 in the group it implicitly means a = -3 (NIST optimisation) */
    if( mbedtls_mpi_cmp_int( &grp->A, 0 ) == 0 )
    {
        *coefSign = 1;
        MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &three, coef_bin, modulusSize ) );
        goto cleanup;
    }

    /* Check if A == P - 3 (the other representation of a = -3) */
    MBEDTLS_MPI_CHK( mbedtls_mpi_sub_mpi( &P_minus_3, &grp->P, &three ) );
    if( mbedtls_mpi_cmp_mpi( &grp->A, &P_minus_3 ) == 0 )
    {
        *coefSign = 1;
        MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &three, coef_bin, modulusSize ) );
    }
    else
    {
        *coefSign = 0;
        MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->A, coef_bin, modulusSize ) );
    }

cleanup:
    mbedtls_mpi_free( &P_minus_3 );
    mbedtls_mpi_free( &three );
    return( ret );
}

/* -----------------------------------------------------------------------
 * Pad or truncate hash to primeOrderSize bytes (left-aligned, zero-padded).
 * ----------------------------------------------------------------------- */
static void ecdsa_alt_prepare_hash( uint8_t        *hash_bin,
                                    size_t          primeOrderSize,
                                    const uint8_t  *buf,
                                    size_t          blen )
{
    if( blen >= primeOrderSize )
    {
        memcpy( hash_bin, buf, primeOrderSize );
    }
    else
    {
        memset( hash_bin, 0, primeOrderSize - blen );
        memcpy( hash_bin + primeOrderSize - blen, buf, blen );
    }
}

/* ======================================================================
 * ECDSA Sign — PKA hardware
 * ====================================================================== */
#if defined(MBEDTLS_ECDSA_SIGN_ALT)
int mbedtls_ecdsa_sign( mbedtls_ecp_group *grp,
                        mbedtls_mpi       *r,
                        mbedtls_mpi       *s,
                        const mbedtls_mpi *d,
                        const unsigned char *buf,
                        size_t              blen,
                        int (*f_rng)(void *, unsigned char *, size_t),
                        void *p_rng )
{
    int ret = 0;
    PKA_HandleTypeDef      hpka = {0};
    PKA_ECDSASignInTypeDef in   = {0};
    PKA_ECDSASignOutTypeDef out = {0};
    mbedtls_mpi k;

    size_t modulusSize    = (grp->pbits + 7) / 8;
    size_t primeOrderSize = mbedtls_mpi_size( &grp->N );

    uint8_t *coef_bin       = NULL;
    uint8_t *coefB_bin      = NULL;
    uint8_t *modulus_bin    = NULL;
    uint8_t *k_bin          = NULL;
    uint8_t *basePointX_bin = NULL;
    uint8_t *basePointY_bin = NULL;
    uint8_t *hash_bin       = NULL;
    uint8_t *d_bin          = NULL;
    uint8_t *primeOrder_bin = NULL;
    uint8_t *r_bin          = NULL;
    uint8_t *s_bin          = NULL;

    mbedtls_mpi_init( &k );

    if( f_rng == NULL )
        return( MBEDTLS_ERR_ECP_RANDOM_FAILED );

    /* Generate ephemeral random k in [1, N-1] */
    MBEDTLS_MPI_CHK( mbedtls_mpi_random( &k, 1, &grp->N, f_rng, p_rng ) );

    /* Allocate working buffers */
    coef_bin       = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    coefB_bin      = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    modulus_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    k_bin          = (uint8_t *) mbedtls_calloc( 1, primeOrderSize );
    basePointX_bin = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    basePointY_bin = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    hash_bin       = (uint8_t *) mbedtls_calloc( 1, primeOrderSize );
    d_bin          = (uint8_t *) mbedtls_calloc( 1, primeOrderSize );
    primeOrder_bin = (uint8_t *) mbedtls_calloc( 1, primeOrderSize );
    r_bin          = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    s_bin          = (uint8_t *) mbedtls_calloc( 1, modulusSize    );

    if( !coef_bin || !coefB_bin || !modulus_bin || !k_bin ||
        !basePointX_bin || !basePointY_bin || !hash_bin ||
        !d_bin || !primeOrder_bin || !r_bin || !s_bin )
    {
        ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
        goto cleanup;
    }

    /* Serialise curve parameters */
    MBEDTLS_MPI_CHK( ecdsa_alt_get_a( grp, coef_bin, modulusSize, &in.coefSign ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->B,    coefB_bin,      modulusSize    ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->P,    modulus_bin,    modulusSize    ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &k,          k_bin,          primeOrderSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->G.MBEDTLS_PRIVATE(X), basePointX_bin, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->G.MBEDTLS_PRIVATE(Y), basePointY_bin, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( d,           d_bin,          primeOrderSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->N,    primeOrder_bin, primeOrderSize ) );

    ecdsa_alt_prepare_hash( hash_bin, primeOrderSize, buf, blen );

    in.primeOrderSize = primeOrderSize;
    in.modulusSize    = modulusSize;
    in.coef           = coef_bin;
    in.coefB          = coefB_bin;
    in.modulus        = modulus_bin;
    in.integer        = k_bin;
    in.basePointX     = basePointX_bin;
    in.basePointY     = basePointY_bin;
    in.hash           = hash_bin;
    in.privateKey     = d_bin;
    in.primeOrder     = primeOrder_bin;

    out.RSign = r_bin;
    out.SSign = s_bin;

    hpka.Instance = PKA;
    MBEDTLS_MPI_CHK( ( HAL_PKA_Init( &hpka ) != HAL_OK )
                     ? MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0 );

    HAL_PKA_RAMReset( &hpka );

    MBEDTLS_MPI_CHK( ( HAL_PKA_ECDSASign( &hpka, &in, ST_PKA_TIMEOUT ) != HAL_OK )
                     ? MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0 );

    HAL_PKA_ECDSASign_GetResult( &hpka, &out, NULL );

    MBEDTLS_MPI_CHK( mbedtls_mpi_read_binary( r, out.RSign, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_read_binary( s, out.SSign, modulusSize ) );

cleanup:
    HAL_PKA_DeInit( &hpka );
    mbedtls_mpi_free( &k );
    mbedtls_free( coef_bin       );
    mbedtls_free( coefB_bin      );
    mbedtls_free( modulus_bin    );
    mbedtls_free( k_bin          );
    mbedtls_free( basePointX_bin );
    mbedtls_free( basePointY_bin );
    mbedtls_free( hash_bin       );
    mbedtls_free( d_bin          );
    mbedtls_free( primeOrder_bin );
    mbedtls_free( r_bin          );
    mbedtls_free( s_bin          );

    return( ret );
}
#endif /* MBEDTLS_ECDSA_SIGN_ALT */

/* ======================================================================
 * ECDSA Verify — PKA hardware
 * ====================================================================== */
#if defined(MBEDTLS_ECDSA_VERIFY_ALT)
int mbedtls_ecdsa_verify( mbedtls_ecp_group         *grp,
                          const unsigned char        *buf,
                          size_t                      blen,
                          const mbedtls_ecp_point    *Q,
                          const mbedtls_mpi          *r,
                          const mbedtls_mpi          *s )
{
    int ret = 0;
    PKA_HandleTypeDef       hpka = {0};
    PKA_ECDSAVerifInTypeDef in   = {0};

    size_t modulusSize    = (grp->pbits + 7) / 8;
    size_t primeOrderSize = mbedtls_mpi_size( &grp->N );

    uint8_t *coef_bin          = NULL;
    uint8_t *modulus_bin       = NULL;
    uint8_t *basePointX_bin    = NULL;
    uint8_t *basePointY_bin    = NULL;
    uint8_t *pubKeyX_bin       = NULL;
    uint8_t *pubKeyY_bin       = NULL;
    uint8_t *RSign_bin         = NULL;
    uint8_t *SSign_bin         = NULL;
    uint8_t *hash_bin          = NULL;
    uint8_t *primeOrder_bin    = NULL;

    /* Allocate working buffers */
    coef_bin       = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    modulus_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    basePointX_bin = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    basePointY_bin = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    pubKeyX_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    pubKeyY_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    RSign_bin      = (uint8_t *) mbedtls_calloc( 1, primeOrderSize );
    SSign_bin      = (uint8_t *) mbedtls_calloc( 1, primeOrderSize );
    hash_bin       = (uint8_t *) mbedtls_calloc( 1, primeOrderSize );
    primeOrder_bin = (uint8_t *) mbedtls_calloc( 1, primeOrderSize );

    if( !coef_bin || !modulus_bin || !basePointX_bin || !basePointY_bin ||
        !pubKeyX_bin || !pubKeyY_bin || !RSign_bin || !SSign_bin ||
        !hash_bin || !primeOrder_bin )
    {
        ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
        goto cleanup;
    }

    /* Serialise curve and key parameters */
    MBEDTLS_MPI_CHK( ecdsa_alt_get_a( grp, coef_bin, modulusSize, &in.coefSign ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->P,    modulus_bin,    modulusSize    ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->G.MBEDTLS_PRIVATE(X), basePointX_bin, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->G.MBEDTLS_PRIVATE(Y), basePointY_bin, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &Q->MBEDTLS_PRIVATE(X),     pubKeyX_bin,    modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &Q->MBEDTLS_PRIVATE(Y),     pubKeyY_bin,    modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( r,           RSign_bin,      primeOrderSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( s,           SSign_bin,      primeOrderSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->N,    primeOrder_bin, primeOrderSize ) );

    ecdsa_alt_prepare_hash( hash_bin, primeOrderSize, buf, blen );

    in.primeOrderSize   = primeOrderSize;
    in.modulusSize      = modulusSize;
    in.coef             = coef_bin;
    in.modulus          = modulus_bin;
    in.basePointX       = basePointX_bin;
    in.basePointY       = basePointY_bin;
    in.pPubKeyCurvePtX  = pubKeyX_bin;
    in.pPubKeyCurvePtY  = pubKeyY_bin;
    in.RSign            = RSign_bin;
    in.SSign            = SSign_bin;
    in.hash             = hash_bin;
    in.primeOrder       = primeOrder_bin;

    hpka.Instance = PKA;
    MBEDTLS_MPI_CHK( ( HAL_PKA_Init( &hpka ) != HAL_OK )
                     ? MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0 );

    HAL_PKA_RAMReset( &hpka );

    if( HAL_PKA_ECDSAVerif( &hpka, &in, ST_PKA_TIMEOUT ) != HAL_OK )
    {
        ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
        goto cleanup;
    }

    if( HAL_PKA_ECDSAVerif_IsValidSignature( &hpka ) == 0 )
        ret = MBEDTLS_ERR_ECP_VERIFY_FAILED;

cleanup:
    HAL_PKA_DeInit( &hpka );
    mbedtls_free( coef_bin       );
    mbedtls_free( modulus_bin    );
    mbedtls_free( basePointX_bin );
    mbedtls_free( basePointY_bin );
    mbedtls_free( pubKeyX_bin    );
    mbedtls_free( pubKeyY_bin    );
    mbedtls_free( RSign_bin      );
    mbedtls_free( SSign_bin      );
    mbedtls_free( hash_bin       );
    mbedtls_free( primeOrder_bin );

    return( ret );
}
#endif /* MBEDTLS_ECDSA_VERIFY_ALT */

#endif /* MBEDTLS_ECDSA_SIGN_ALT || MBEDTLS_ECDSA_VERIFY_ALT */
