/*
 * ecp_alt.c — STM32H5 PKA-accelerated ECP internal functions for mbedTLS 3.x
 *
 * Implements the MBEDTLS_ECP_INTERNAL_ALT interface using the STM32H5 PKA.
 *
 * Per-step ALT flags implemented in hardware
 * ──────────────────────────────────────────
 *  MBEDTLS_ECP_NORMALIZE_JAC_ALT      → HAL_PKA_ECCProjective2Affine
 *  MBEDTLS_ECP_NORMALIZE_JAC_MANY_ALT → HAL_PKA_ECCProjective2Affine (loop)
 *  MBEDTLS_ECP_ADD_MIXED_ALT          → HAL_PKA_ECCCompleteAddition
 *
 * Per-step ALT flags NOT implemented (no PKA primitive available)
 * ───────────────────────────────────────────────────────────────
 *  MBEDTLS_ECP_DOUBLE_JAC_ALT         — PKA has no standalone point-doubling op;
 *                                        mbedTLS software fallback is used.
 *  MBEDTLS_ECP_RANDOMIZE_JAC_ALT      — Requires random-scalar field multiply
 *                                        not exposed as a PKA primitive;
 *                                        mbedTLS software fallback is used.
 *
 * Supported curves (Short Weierstrass, PKA hardware):
 *   SECP192R1, SECP224R1, SECP256R1, SECP384R1, SECP521R1
 *   BP256R1, BP384R1, BP512R1
 *
 * Montgomery curves (Curve25519, Curve448) are reported as not capable
 * and fall back to the mbedTLS software implementation automatically.
 *
 * Note on DOUBLE_JAC_ALT absence
 * ───────────────────────────────
 * The ecp_mul_comb() inner loop calls both double_jac and add_mixed per bit.
 * Since double_jac stays in software, the scalar multiplication is a hybrid:
 * each doubling is done by mbedTLS software and each addition by the PKA.
 * The PKA CompleteAddition operates on Jacobian coordinates (Z may be != 1)
 * so this is fully correct.  Pure-software doubling is fast relative to the
 * PKA overhead for a single step, so the overall cycle count is still reduced
 * compared to a full-software implementation.
 */

/* Allow direct field access on mbedtls_ecp_point / mbedtls_ecp_group. */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "common.h"

#if defined(MBEDTLS_ECP_INTERNAL_ALT)

#include "mbedtls/ecp.h"
#include "mbedtls/platform.h"
#include "mbedtls/error.h"

#include "stm32h5xx_hal.h"
#include <string.h>

#define ST_PKA_TIMEOUT  10000U

/* =========================================================================
 * Mandatory gating triplet
 * ========================================================================= */

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

        /* Montgomery curves and Koblitz curves are not supported by PKA */
        default:
            return( 0 );
    }
}

/*
 * mbedtls_internal_ecp_init()
 *
 * Called by mbedTLS before any hardware-accelerated point operation per
 * call to ecp_mul_restartable_internal().  PKA is initialised per-operation
 * inside each function below, so this is a no-op.
 */
int mbedtls_internal_ecp_init( const mbedtls_ecp_group *grp )
{
    (void) grp;
    return( 0 );
}

/*
 * mbedtls_internal_ecp_free()
 *
 * Called by mbedTLS after each hardware-accelerated operation context.
 * No persistent PKA state to clean up.
 */
void mbedtls_internal_ecp_free( const mbedtls_ecp_group *grp )
{
    (void) grp;
}

/* =========================================================================
 * Internal shared helpers
 * ========================================================================= */

/*
 * Helper: compute Montgomery parameter R² mod P for the PKA projective→affine
 * conversion.  The PKA ECCProjective2Affine operation requires R² as a
 * precomputed value (same as HAL_PKA_MontgomeryParam result).
 *
 * Caller must mbedtls_free(*pp_mont) after use.
 * Returns 0 on success, MBEDTLS_ERR_ECP_ALLOC_FAILED on OOM,
 * MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED on PKA error.
 */
static int ecp_pka_montgomery_param( const mbedtls_ecp_group *grp,
                                     size_t                   modulusSize,
                                     uint32_t               **pp_mont,
                                     size_t                  *p_mont_words )
{
    int ret = 0;
    PKA_HandleTypeDef            hpka       = {0};
    PKA_MontgomeryParamInTypeDef mont_in    = {0};
    uint8_t *modulus_bin = NULL;

    /* Montgomery result is returned as array of uint32_t words */
    size_t mont_words  = ( modulusSize + 3U ) / 4U;
    uint32_t *mont_buf = (uint32_t *) mbedtls_calloc( mont_words, sizeof( uint32_t ) );
    modulus_bin        = (uint8_t *)  mbedtls_calloc( 1, modulusSize );

    if( !mont_buf || !modulus_bin )
    {
        ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
        goto cleanup;
    }

    if( mbedtls_mpi_write_binary( &grp->P, modulus_bin, modulusSize ) != 0 )
    {
        ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
        goto cleanup;
    }

    mont_in.size = modulusSize;
    mont_in.pOp1 = modulus_bin;

    hpka.Instance = PKA;
    if( HAL_PKA_Init( &hpka ) != HAL_OK )
    {
        ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
        goto cleanup;
    }

    HAL_PKA_RAMReset( &hpka );

    if( HAL_PKA_MontgomeryParam( &hpka, &mont_in, ST_PKA_TIMEOUT ) != HAL_OK )
    {
        ret = MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED;
        HAL_PKA_DeInit( &hpka );
        goto cleanup;
    }

    HAL_PKA_MontgomeryParam_GetResult( &hpka, mont_buf );
    HAL_PKA_DeInit( &hpka );

    *pp_mont      = mont_buf;
    *p_mont_words = mont_words;
    mont_buf = NULL; /* ownership transferred */

cleanup:
    mbedtls_free( modulus_bin );
    mbedtls_free( mont_buf );
    return( ret );
}

/*
 * Helper: resolve PKA coefSign and |A| binary for a group.
 *
 * mbedTLS 3.x stores A as (P - 3) for curves where a = -3 (NIST SECPxxx).
 * The PKA wants the absolute value and a 1-bit sign flag.
 *   coefSign: 0 = positive,  1 = negative
 */
static int ecp_pka_get_a( const mbedtls_ecp_group *grp,
                           uint8_t  *coef_bin,
                           size_t    modulusSize,
                           uint32_t *coefSign )
{
    int ret = 0;
    mbedtls_mpi P_minus_3, three;

    mbedtls_mpi_init( &P_minus_3 );
    mbedtls_mpi_init( &three );

    MBEDTLS_MPI_CHK( mbedtls_mpi_lset( &three, 3 ) );

    if( mbedtls_mpi_cmp_int( &grp->A, 0 ) == 0 )
    {
        *coefSign = 1; /* a = -3 (NIST optimisation) */
        MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &three, coef_bin, modulusSize ) );
        goto cleanup;
    }

    MBEDTLS_MPI_CHK( mbedtls_mpi_sub_mpi( &P_minus_3, &grp->P, &three ) );
    if( mbedtls_mpi_cmp_mpi( &grp->A, &P_minus_3 ) == 0 )
    {
        *coefSign = 1; /* explicit a = P-3  ≡ -3 */
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

/* =========================================================================
 * MBEDTLS_ECP_NORMALIZE_JAC_ALT
 *
 * Convert a single Jacobian point (X:Y:Z) to affine (X/Z²:Y/Z³:1) using
 * the PKA projective-to-affine hardware operation.
 * ========================================================================= */
#if defined(MBEDTLS_ECP_NORMALIZE_JAC_ALT)
int mbedtls_internal_ecp_normalize_jac( const mbedtls_ecp_group *grp,
                                         mbedtls_ecp_point       *pt )
{
    int ret = 0;
    PKA_HandleTypeDef                  hpka   = {0};
    PKA_ECCProjective2AffineInTypeDef  in     = {0};
    PKA_ECCProjective2AffineOutTypeDef out    = {0};

    size_t modulusSize = ( grp->pbits + 7U ) / 8U;
    uint32_t *mont_buf = NULL;
    size_t    mont_words = 0;

    uint8_t *modulus_bin = NULL;
    uint8_t *ptX_bin     = NULL;
    uint8_t *ptY_bin     = NULL;
    uint8_t *ptZ_bin     = NULL;
    uint8_t *resX_bin    = NULL;
    uint8_t *resY_bin    = NULL;

    /* If Z is already 1 there is nothing to do */
    if( mbedtls_mpi_cmp_int( &pt->MBEDTLS_PRIVATE(Z), 1 ) == 0 )
        return( 0 );

    modulus_bin = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    ptX_bin     = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    ptY_bin     = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    ptZ_bin     = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    resX_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    resY_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize );

    if( !modulus_bin || !ptX_bin || !ptY_bin || !ptZ_bin ||
        !resX_bin    || !resY_bin )
    {
        ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
        goto cleanup;
    }

    /* Compute Montgomery parameter R² mod P */
    MBEDTLS_MPI_CHK( ecp_pka_montgomery_param( grp, modulusSize,
                                               &mont_buf, &mont_words ) );

    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->P,                  modulus_bin, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &pt->MBEDTLS_PRIVATE(X),  ptX_bin,     modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &pt->MBEDTLS_PRIVATE(Y),  ptY_bin,     modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &pt->MBEDTLS_PRIVATE(Z),  ptZ_bin,     modulusSize ) );

    in.modulusSize      = modulusSize;
    in.modulus          = modulus_bin;
    in.basePointX       = ptX_bin;
    in.basePointY       = ptY_bin;
    in.basePointZ       = ptZ_bin;
    in.pMontgomeryParam = mont_buf;

    out.ptX = resX_bin;
    out.ptY = resY_bin;

    hpka.Instance = PKA;
    MBEDTLS_MPI_CHK( ( HAL_PKA_Init( &hpka ) != HAL_OK )
                     ? MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0 );

    HAL_PKA_RAMReset( &hpka );

    MBEDTLS_MPI_CHK( ( HAL_PKA_ECCProjective2Affine( &hpka, &in, ST_PKA_TIMEOUT ) != HAL_OK )
                     ? MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0 );

    HAL_PKA_ECCProjective2Affine_GetResult( &hpka, &out );

    MBEDTLS_MPI_CHK( mbedtls_mpi_read_binary( &pt->MBEDTLS_PRIVATE(X), resX_bin, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_read_binary( &pt->MBEDTLS_PRIVATE(Y), resY_bin, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_lset( &pt->MBEDTLS_PRIVATE(Z), 1 ) );

cleanup:
    HAL_PKA_DeInit( &hpka );
    mbedtls_free( mont_buf   );
    mbedtls_free( modulus_bin );
    mbedtls_free( ptX_bin    );
    mbedtls_free( ptY_bin    );
    mbedtls_free( ptZ_bin    );
    mbedtls_free( resX_bin   );
    mbedtls_free( resY_bin   );
    return( ret );
}
#endif /* MBEDTLS_ECP_NORMALIZE_JAC_ALT */

/* =========================================================================
 * MBEDTLS_ECP_NORMALIZE_JAC_MANY_ALT
 *
 * Normalize an array of t_len Jacobian points to affine using the PKA.
 * Each point is processed independently (the PKA has no batch operation).
 *
 * Note: mbedTLS uses Montgomery's trick to do this with a single inversion
 * in software.  Our approach uses t_len PKA calls instead — acceptable
 * because t_len is small (at most 2^w where w ≤ 4 in ecp_mul_comb).
 * ========================================================================= */
#if defined(MBEDTLS_ECP_NORMALIZE_JAC_MANY_ALT)
int mbedtls_internal_ecp_normalize_jac_many( const mbedtls_ecp_group *grp,
                                              mbedtls_ecp_point       *T[],
                                              size_t                   t_len )
{
    int ret = 0;

    for( size_t i = 0; i < t_len; i++ )
    {
        MBEDTLS_MPI_CHK( mbedtls_internal_ecp_normalize_jac( grp, T[i] ) );
    }

cleanup:
    return( ret );
}
#endif /* MBEDTLS_ECP_NORMALIZE_JAC_MANY_ALT */

/* =========================================================================
 * MBEDTLS_ECP_ADD_MIXED_ALT
 *
 * Mixed Jacobian + Affine point addition:  R = P + Q
 *
 *   P  has Jacobian coordinates  (X1 : Y1 : Z1)
 *   Q  has affine   coordinates  (X2 : Y2 :  1)   (Z2 unset or 1)
 *   R  result in Jacobian coordinates (not normalised)
 *
 * Implemented via HAL_PKA_ECCCompleteAddition which accepts two Jacobian
 * points.  We pass Q with Z2 = 1 to represent its affine coordinates.
 *
 * The mbedTLS comment in ecp_internal_alt.h guarantees that none of P, Q,
 * or R is the point at infinity during ecp_mul_comb, so we do not need to
 * handle the Z == 0 special case here.
 * ========================================================================= */
#if defined(MBEDTLS_ECP_ADD_MIXED_ALT)
int mbedtls_internal_ecp_add_mixed( const mbedtls_ecp_group *grp,
                                     mbedtls_ecp_point       *R,
                                     const mbedtls_ecp_point *P,
                                     const mbedtls_ecp_point *Q )
{
    int ret = 0;
    PKA_HandleTypeDef               hpka = {0};
    PKA_ECCCompleteAdditionInTypeDef in   = {0};
    PKA_ECCCompleteAdditionOutTypeDef out = {0};

    size_t modulusSize = ( grp->pbits + 7U ) / 8U;

    uint8_t *coef_bin    = NULL;
    uint8_t *modulus_bin = NULL;
    uint8_t *p1X_bin     = NULL;
    uint8_t *p1Y_bin     = NULL;
    uint8_t *p1Z_bin     = NULL;
    uint8_t *p2X_bin     = NULL;
    uint8_t *p2Y_bin     = NULL;
    uint8_t *p2Z_bin     = NULL;   /* Q is affine → Z2 = 1 */
    uint8_t *resX_bin    = NULL;
    uint8_t *resY_bin    = NULL;
    uint8_t *resZ_bin    = NULL;
    uint32_t coefSign    = 0;

    coef_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    modulus_bin = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    p1X_bin     = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    p1Y_bin     = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    p1Z_bin     = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    p2X_bin     = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    p2Y_bin     = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    p2Z_bin     = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    resX_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    resY_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize );
    resZ_bin    = (uint8_t *) mbedtls_calloc( 1, modulusSize );

    if( !coef_bin || !modulus_bin ||
        !p1X_bin  || !p1Y_bin || !p1Z_bin ||
        !p2X_bin  || !p2Y_bin || !p2Z_bin ||
        !resX_bin || !resY_bin || !resZ_bin )
    {
        ret = MBEDTLS_ERR_ECP_ALLOC_FAILED;
        goto cleanup;
    }

    /* Resolve curve coefficient a */
    MBEDTLS_MPI_CHK( ecp_pka_get_a( grp, coef_bin, modulusSize, &coefSign ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &grp->P,                  modulus_bin, modulusSize ) );

    /* P1 (Jacobian) */
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &P->MBEDTLS_PRIVATE(X),   p1X_bin,     modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &P->MBEDTLS_PRIVATE(Y),   p1Y_bin,     modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &P->MBEDTLS_PRIVATE(Z),   p1Z_bin,     modulusSize ) );

    /* P2 (Q — affine, so Z2 = 1).  mbedTLS may leave Q->Z unset in the
     * precomputed table; treat unset (limbs == NULL) as Z = 1 per the
     * comment in ecp_internal_alt.h. */
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &Q->MBEDTLS_PRIVATE(X),   p2X_bin,     modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_write_binary( &Q->MBEDTLS_PRIVATE(Y),   p2Y_bin,     modulusSize ) );
    /* Z2 = 1 → big-endian encoding: last byte = 0x01 */
    p2Z_bin[modulusSize - 1U] = 0x01U;

    in.modulusSize  = modulusSize;
    in.coefSign     = coefSign;
    in.modulus      = modulus_bin;
    in.coefA        = coef_bin;
    in.basePointX1  = p1X_bin;
    in.basePointY1  = p1Y_bin;
    in.basePointZ1  = p1Z_bin;
    in.basePointX2  = p2X_bin;
    in.basePointY2  = p2Y_bin;
    in.basePointZ2  = p2Z_bin;

    out.ptX = resX_bin;
    out.ptY = resY_bin;
    out.ptZ = resZ_bin;

    hpka.Instance = PKA;
    MBEDTLS_MPI_CHK( ( HAL_PKA_Init( &hpka ) != HAL_OK )
                     ? MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0 );

    HAL_PKA_RAMReset( &hpka );

    MBEDTLS_MPI_CHK( ( HAL_PKA_ECCCompleteAddition( &hpka, &in, ST_PKA_TIMEOUT ) != HAL_OK )
                     ? MBEDTLS_ERR_PLATFORM_HW_ACCEL_FAILED : 0 );

    HAL_PKA_ECCCompleteAddition_GetResult( &hpka, &out );

    MBEDTLS_MPI_CHK( mbedtls_mpi_read_binary( &R->MBEDTLS_PRIVATE(X), resX_bin, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_read_binary( &R->MBEDTLS_PRIVATE(Y), resY_bin, modulusSize ) );
    MBEDTLS_MPI_CHK( mbedtls_mpi_read_binary( &R->MBEDTLS_PRIVATE(Z), resZ_bin, modulusSize ) );

cleanup:
    HAL_PKA_DeInit( &hpka );
    mbedtls_free( coef_bin    );
    mbedtls_free( modulus_bin );
    mbedtls_free( p1X_bin     );
    mbedtls_free( p1Y_bin     );
    mbedtls_free( p1Z_bin     );
    mbedtls_free( p2X_bin     );
    mbedtls_free( p2Y_bin     );
    mbedtls_free( p2Z_bin     );
    mbedtls_free( resX_bin    );
    mbedtls_free( resY_bin    );
    mbedtls_free( resZ_bin    );
    return( ret );
}
#endif /* MBEDTLS_ECP_ADD_MIXED_ALT */

#endif /* MBEDTLS_ECP_INTERNAL_ALT */
