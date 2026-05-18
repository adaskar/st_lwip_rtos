/*
 * ecdh_alt.c — STM32H5 PKA-accelerated ECDH for mbedTLS 3.x
 *
 * Implements MBEDTLS_ECDH_GEN_PUBLIC_ALT and MBEDTLS_ECDH_COMPUTE_SHARED_ALT
 * using the STM32H5 Public Key Accelerator (PKA) peripheral.
 *
 * TLS 1.3 key exchange algorithms supported (Short Weierstrass curves):
 *   secp256r1  (P-256)   — TLS group 0x0017
 *   secp384r1  (P-384)   — TLS group 0x0018
 *   secp521r1  (P-521)   — TLS group 0x0019
 *   brainpoolP256r1      — TLS group 0x001A  (RFC 8734)
 *   brainpoolP384r1      — TLS group 0x001B
 *   brainpoolP512r1      — TLS group 0x001C
 *
 * Montgomery curves (x25519 / x448) are NOT supported by the PKA hardware and
 * will fall through to the mbedTLS software path automatically because mbedTLS
 * only calls these alt hooks for the classical ECDH interface; the x25519/x448
 * TLS 1.3 named groups use a different code path (mbedtls_ecdh_read/write_params
 * with MBEDTLS_ECP_DP_CURVE25519 / CURVE448 groups) that is not gated by these
 * two ALT macros, so software fallback is inherent.
 *
 * Both functions accept an optional f_rng / p_rng for blinding — the PKA
 * performs the scalar multiplication directly so we only use RNG for private
 * key generation in mbedtls_ecdh_gen_public.
 */

/* Allow direct field access on mbedtls_ecp_point / mbedtls_ecp_group.
 * Explicitly supported for hardware port code in mbedTLS 3.x. */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "mbedtls/ecdh.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/error.h"
#include "mbedtls/bignum.h"

#if defined(MBEDTLS_ECDH_GEN_PUBLIC_ALT) || defined(MBEDTLS_ECDH_COMPUTE_SHARED_ALT)

#include "stm32h5xx_hal.h"
#include <string.h>

#define ST_PKA_TIMEOUT  10000U

/* -----------------------------------------------------------------------
 * Internal helper: resolve PKA coefSign and |A| binary for a group.
 *
 * mbedTLS 3.x stores A as (P - 3) for curves where a = -3 (NIST SECPxxx).
 * The PKA wants the absolute value and a 1-bit sign flag.
 *   coefSign: 0 = positive coefficient,  1 = negative coefficient
 * ----------------------------------------------------------------------- */
static int ecdh_alt_get_a( const mbedtls_ecp_group *grp,
                            uint8_t  *coef_bin,
                            size_t    modulusSize,
                            uint32_t *coefSign )
{
    int ret = 0;
    mbedtls_mpi P_minus_3, three;

    mbedtls_mpi_init( &P_minus_3 );
    mbedtls_mpi_init( &three );

    MBEDTLS_MPI_CHK( mbedtls_mpi_lset( &three, 3 ) );

    /* A == 0 in the group context means a = -3 (NIST optimisation) */
    if( mbedtls_mpi_cmp_int( &grp->A, 0 ) == 0 )
    {
        *coefSign = 1; /* negative */
        MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &three, coef_bin, modulusSize ) );
        goto cleanup;
    }

    /* Also check A == P - 3  (explicit representation of a = -3) */
    MBEDTLS_MPI_CHK( mbedtls_mpi_sub_mpi( &P_minus_3, &grp->P, &three ) );
    if( mbedtls_mpi_cmp_mpi( &grp->A, &P_minus_3 ) == 0 )
    {
        *coefSign = 1; /* negative */
        MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &three, coef_bin, modulusSize ) );
    }
    else
    {
        *coefSign = 0; /* positive */
        MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->A, coef_bin, modulusSize ) );
    }

cleanup:
    mbedtls_mpi_free( &P_minus_3 );
    mbedtls_mpi_free( &three );
    return( ret );
}

/* -----------------------------------------------------------------------
 * Internal helper: run one PKA ECC scalar multiplication.
 *
 * Computes  R = scalar * P  using HAL_PKA_ECCMulEx.
 * The result is written back into R.
 * ----------------------------------------------------------------------- */
static int ecdh_pka_ecc_mul( const mbedtls_ecp_group  *grp,
                              mbedtls_ecp_point        *R,
                              const mbedtls_mpi        *scalar,
                              const mbedtls_ecp_point  *P )
{
    int ret = 0;
    PKA_HandleTypeDef    hpka  = {0};
    PKA_ECCMulExInTypeDef in   = {0};
    PKA_ECCMulOutTypeDef  out  = {0};

    size_t modulusSize    = ( grp->pbits + 7U ) / 8U;
    size_t primeOrderSize = mbedtls_mpi_size( &grp->N );
    size_t scalarSize     = mbedtls_mpi_size( scalar );

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

    /* Allocate working buffers */
    coef_bin       = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    coefB_bin      = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    modulus_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    scalar_bin     = (uint8_t *) mbedtls_calloc( 1, scalarSize     );
    pointX_bin     = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    pointY_bin     = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    primeOrder_bin = (uint8_t *) mbedtls_calloc( 1, primeOrderSize );
    resultX_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize    );
    resultY_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize    );

    if( !coef_bin || !coefB_bin || !modulus_bin || !scalar_bin ||
        !pointX_bin || !pointY_bin || !primeOrder_bin ||
        !resultX_bin || !resultY_bin )
    {
        ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
        goto cleanup;
    }

    /* Serialise curve and input parameters */
    MBEDTLS_MPI_CHK( ecdh_alt_get_a( grp, coef_bin, modulusSize, &coefSign ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->B,                       coefB_bin,      modulusSize    ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->P,                       modulus_bin,    modulusSize    ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( scalar,                        scalar_bin,     scalarSize     ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &P->MBEDTLS_PRIVATE(X),        pointX_bin,     modulusSize    ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &P->MBEDTLS_PRIVATE(Y),        pointY_bin,     modulusSize    ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->N,                       primeOrder_bin, primeOrderSize ) );

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

    /* Read back result into R */
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

/* ======================================================================
 * ECDH Generate Public Key — PKA hardware
 *
 * Computes  Q = d * G  where G is the base point of the curve.
 *
 * This is called by mbedTLS during the TLS 1.3 key-share extension
 * generation (ClientHello / ServerHello) for every ECDHE key exchange.
 *
 * Parameters match the mbedtls_ecdh_gen_public() prototype exactly.
 * ====================================================================== */
#if defined(MBEDTLS_ECDH_GEN_PUBLIC_ALT)
int mbedtls_ecdh_gen_public( mbedtls_ecp_group *grp,
                              mbedtls_mpi       *d,
                              mbedtls_ecp_point *Q,
                              int (*f_rng)(void *, unsigned char *, size_t),
                              void *p_rng )
{
    int ret = 0;

    if( f_rng == NULL )
        return( MBEDTLS_ERR_ECP_RANDOM_FAILED );

    /* Generate a random private key d in [1, N-1] */
    MBEDTLS_MPI_CHK( mbedtls_mpi_random( d, 1, &grp->N, f_rng, p_rng ) );

    /* Compute the public key Q = d * G via the PKA */
    MBEDTLS_MPI_CHK( ecdh_pka_ecc_mul( grp, Q, d, &grp->G ) );

cleanup:
    return( ret );
}
#endif /* MBEDTLS_ECDH_GEN_PUBLIC_ALT */

/* ======================================================================
 * ECDH Compute Shared Secret — PKA hardware
 *
 * Computes  z = d * Qp  where Qp is the peer's public point, then
 * extracts the x-coordinate as the shared secret.
 *
 * This is called by mbedTLS during TLS 1.3 handshake key derivation
 * (HKDF input keying material from the ECDHE shared secret).
 *
 * Parameters match the mbedtls_ecdh_compute_shared() prototype exactly.
 * ====================================================================== */
#if defined(MBEDTLS_ECDH_COMPUTE_SHARED_ALT)
int mbedtls_ecdh_compute_shared( mbedtls_ecp_group       *grp,
                                  mbedtls_mpi             *z,
                                  const mbedtls_ecp_point *Q,
                                  const mbedtls_mpi       *d,
                                  int (*f_rng)(void *, unsigned char *, size_t),
                                  void *p_rng )
{
    int ret = 0;
    mbedtls_ecp_point shared_pt;

    /* f_rng / p_rng may be NULL here — blinding is done in hardware */
    (void) f_rng;
    (void) p_rng;

    mbedtls_ecp_point_init( &shared_pt );

    /* shared_pt = d * Q  (peer's public key) */
    MBEDTLS_MPI_CHK( ecdh_pka_ecc_mul( grp, &shared_pt, d, Q ) );

    /*
     * The shared secret z is the x-coordinate of the result point.
     * Per RFC 8446 §7.4.2 (and RFC 4492 §5.10) only the x-coordinate
     * is used as the ECDHE premaster/shared secret.
     */
    MBEDTLS_MPI_CHK( mbedtls_mpi_copy( z, &shared_pt.MBEDTLS_PRIVATE(X) ) );

cleanup:
    mbedtls_ecp_point_free( &shared_pt );
    return( ret );
}
#endif /* MBEDTLS_ECDH_COMPUTE_SHARED_ALT */

#endif /* MBEDTLS_ECDH_GEN_PUBLIC_ALT || MBEDTLS_ECDH_COMPUTE_SHARED_ALT */
