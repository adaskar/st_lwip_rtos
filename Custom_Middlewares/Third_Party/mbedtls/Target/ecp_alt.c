/*
 * ecp_alt.c — STM32H5 PKA-accelerated ECP internal functions for mbedTLS 3.x
 *
 * Implements the MBEDTLS_ECP_INTERNAL_ALT interface to accelerate elliptic
 * curve point multiplication using the STM32H5 Public Key Accelerator (PKA).
 *
 * Supported curves (Short Weierstrass, PKA hardware):
 *   - SECP192R1, SECP224R1, SECP256R1, SECP384R1, SECP521R1
 *   - BP256R1, BP384R1, BP512R1
 *
 * Montgomery curves (Curve25519, Curve448) are NOT supported in hardware and
 * fall back to the mbedTLS software implementation automatically.
 */

/* Allow direct field access on mbedtls_ecp_point / mbedtls_ecp_group.
 * This is explicitly supported for hardware port code in mbedTLS 3.x. */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "common.h"

#if defined(MBEDTLS_ECP_INTERNAL_ALT)

#include "mbedtls/ecp.h"
#include "mbedtls/platform.h"
#include "mbedtls/error.h"

#include "stm32h5xx_hal.h"
#include <string.h>

#define ST_PKA_TIMEOUT 10000

/*
 * mbedtls_internal_ecp_grp_capable()
 *
 * Returns 1 if the PKA hardware can handle this curve, 0 otherwise.
 * The PKA supports Short Weierstrass prime-field curves only.
 */
unsigned char mbedtls_internal_ecp_grp_capable( const mbedtls_ecp_group *grp )
{
    switch( grp->id )
    {
        case MBEDTLS_ECP_DP_SECP192R1:
        case MBEDTLS_ECP_DP_SECP224R1:
        case MBEDTLS_ECP_DP_SECP256R1:
        case MBEDTLS_ECP_DP_SECP384R1:
        case MBEDTLS_ECP_DP_SECP521R1:
        case MBEDTLS_ECP_DP_BP256R1:
        case MBEDTLS_ECP_DP_BP384R1:
        case MBEDTLS_ECP_DP_BP512R1:
            return( 1 );

        /* Montgomery curves and Koblitz curves not supported by PKA hardware */
        default:
            return( 0 );
    }
}

/*
 * mbedtls_internal_ecp_init()
 *
 * Called by mbedTLS before any hardware-accelerated point operation.
 * The PKA is initialised per-operation (inside ecp_mul below) so
 * this function is a no-op.
 */
int mbedtls_internal_ecp_init( const mbedtls_ecp_group *grp )
{
    (void) grp;
    return( 0 );
}

/*
 * mbedtls_internal_ecp_free()
 *
 * Called by mbedTLS after each hardware-accelerated point operation.
 * No persistent PKA state to clean up.
 */
void mbedtls_internal_ecp_free( const mbedtls_ecp_group *grp )
{
    (void) grp;
}

/*
 * Helper: determine |A| and its sign for the PKA.
 *
 * mbedTLS stores A as (P - 3) for NIST curves with a = -3.
 * The PKA wants the absolute value of 'a' and a separate sign flag.
 *
 * coefSign: 0 = positive, 1 = negative
 */
static int ecp_get_a_sign_and_abs( const mbedtls_ecp_group *grp,
                                   uint8_t *coef_bin,
                                   size_t   modulusSize,
                                   uint32_t *coefSign )
{
    int ret = 0;
    mbedtls_mpi P_minus_3, three;

    mbedtls_mpi_init( &P_minus_3 );
    mbedtls_mpi_init( &three );

    MBEDTLS_MPI_CHK( mbedtls_mpi_lset( &three, 3 ) );

    /* If A == 0 in the context it means a = -3 (NIST optimisation) */
    if( mbedtls_mpi_cmp_int( &grp->A, 0 ) == 0 )
    {
        *coefSign = 1;  /* negative */
        MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &three, coef_bin, modulusSize ) );
        goto cleanup;
    }

    /* Check if A == P - 3 */
    MBEDTLS_MPI_CHK( mbedtls_mpi_sub_mpi( &P_minus_3, &grp->P, &three ) );
    if( mbedtls_mpi_cmp_mpi( &grp->A, &P_minus_3 ) == 0 )
    {
        *coefSign = 1;  /* negative */
        MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &three, coef_bin, modulusSize ) );
    }
    else
    {
        *coefSign = 0;  /* positive */
        MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->A, coef_bin, modulusSize ) );
    }

cleanup:
    mbedtls_mpi_free( &P_minus_3 );
    mbedtls_mpi_free( &three );
    return( ret );
}

/*
 * mbedtls_internal_ecp_mul()  [called via MBEDTLS_ECP_DOUBLE_JAC_ALT etc.]
 *
 * NOTE: With MBEDTLS_ECP_INTERNAL_ALT and none of the per-function ALT flags
 * set, the init/free/capable gate is in place but individual inner functions
 * are not replaced. To replace the *full* scalar multiplication we define
 * a helper that wraps HAL_PKA_ECCMul and call it from the ECDSA alt
 * (ecdsa_alt.c uses its own PKA calls for sign/verify).
 *
 * This file exposes the internal helper for possible future use when
 * MBEDTLS_ECP_MUL_ALT is available, but its main role today is to
 * provide the mandatory grp_capable / init / free triplet so that the
 * internal alt gate compiles correctly.
 *
 * For a full point-multiplication replacement you would need
 * MBEDTLS_ECP_ALT (replace whole module) or the per-step ALT flags
 * (MBEDTLS_ECP_DOUBLE_JAC_ALT etc.).  Both approaches require significant
 * porting work and are left as a future optimisation.
 */
int mbedtls_internal_ecp_mul( mbedtls_ecp_group       *grp,
                               mbedtls_ecp_point       *R,
                               const mbedtls_mpi       *m,
                               const mbedtls_ecp_point *P )
{
    int ret = 0;
    PKA_HandleTypeDef   hpka = {0};
    PKA_ECCMulExInTypeDef in  = {0};
    PKA_ECCMulOutTypeDef  out = {0};

    size_t modulusSize    = (grp->pbits + 7) / 8;
    size_t primeOrderSize = mbedtls_mpi_size( &grp->N );
    size_t scalarSize     = mbedtls_mpi_size( m );

    uint8_t *coef_bin       = NULL;
    uint8_t *coefB_bin      = NULL;
    uint8_t *modulus_bin    = NULL;
    uint8_t *scalar_bin     = NULL;
    uint8_t *pointX_bin     = NULL;
    uint8_t *pointY_bin     = NULL;
    uint8_t *primeOrder_bin = NULL;
    uint8_t *resultX_bin    = NULL;
    uint8_t *resultY_bin    = NULL;
    uint32_t coefSign       = 0;

    coef_bin       = mbedtls_calloc( 1, modulusSize );
    coefB_bin      = mbedtls_calloc( 1, modulusSize );
    modulus_bin    = mbedtls_calloc( 1, modulusSize );
    scalar_bin     = mbedtls_calloc( 1, scalarSize  );
    pointX_bin     = mbedtls_calloc( 1, modulusSize );
    pointY_bin     = mbedtls_calloc( 1, modulusSize );
    primeOrder_bin = mbedtls_calloc( 1, primeOrderSize );
    resultX_bin    = mbedtls_calloc( 1, modulusSize );
    resultY_bin    = mbedtls_calloc( 1, modulusSize );

    if( !coef_bin || !coefB_bin || !modulus_bin || !scalar_bin ||
        !pointX_bin || !pointY_bin || !primeOrder_bin ||
        !resultX_bin || !resultY_bin )
    {
        ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
        goto cleanup;
    }

    MBEDTLS_MPI_CHK( ecp_get_a_sign_and_abs( grp, coef_bin, modulusSize, &coefSign ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->B,   coefB_bin,      modulusSize    ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->P,   modulus_bin,    modulusSize    ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( m,          scalar_bin,     scalarSize     ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &P->MBEDTLS_PRIVATE(X), pointX_bin,     modulusSize    ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &P->MBEDTLS_PRIVATE(Y), pointY_bin,     modulusSize    ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->N,   primeOrder_bin, primeOrderSize ) );

    in.primeOrderSize = primeOrderSize;
    in.scalarMulSize  = scalarSize;
    in.modulusSize    = modulusSize;
    in.coefSign       = coefSign;
    in.coefA          = coef_bin;
    in.coefB          = coefB_bin;
    in.modulus        = modulus_bin;
    in.scalarMul      = scalar_bin;
    in.pointX         = pointX_bin;
    in.pointY         = pointY_bin;
    in.primeOrder     = primeOrder_bin;

    out.ptX = resultX_bin;
    out.ptY = resultY_bin;

    hpka.Instance = PKA;
    MBEDTLS_MPI_CHK( ( HAL_PKA_Init( &hpka ) != HAL_OK )
                     ? MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0 );

    HAL_PKA_RAMReset( &hpka );

    MBEDTLS_MPI_CHK( ( HAL_PKA_ECCMulEx( &hpka, &in, ST_PKA_TIMEOUT ) != HAL_OK )
                     ? MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0 );

    HAL_PKA_ECCMul_GetResult( &hpka, &out );

    MBEDTLS_MPI_CHK( mbedtls_mpi_read_binary( &R->MBEDTLS_PRIVATE(X), resultX_bin, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_read_binary( &R->MBEDTLS_PRIVATE(Y), resultY_bin, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_lset( &R->MBEDTLS_PRIVATE(Z), 1 ) );

cleanup:
    HAL_PKA_DeInit( &hpka );

    mbedtls_free( coef_bin       );
    mbedtls_free( coefB_bin      );
    mbedtls_free( modulus_bin    );
    mbedtls_free( scalar_bin     );
    mbedtls_free( pointX_bin     );
    mbedtls_free( pointY_bin     );
    mbedtls_free( primeOrder_bin );
    mbedtls_free( resultX_bin    );
    mbedtls_free( resultY_bin    );

    return( ret );
}

#endif /* MBEDTLS_ECP_INTERNAL_ALT */
