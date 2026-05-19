#include "main.h"
#include <stdio.h>
#include <string.h>
#include "mbedtls.h"

int dummy_rng(void *p_rng, unsigned char *output, size_t len)
{
    (void)p_rng;
    uint32_t random_val;
    extern RNG_HandleTypeDef hrng;
    for( size_t i = 0; i < len; i += 4 )
    {
        if( HAL_RNG_GenerateRandomNumber( &hrng, &random_val ) != HAL_OK )
            return -1;
        size_t to_copy = ( len - i >= 4 ) ? 4 : ( len - i );
        memcpy( output + i, &random_val, to_copy );
    }
    return 0;
}

void main_app(void *arg)
{
    (void)arg;
    int ret = 0;
    int v = 1; /* v=1 for verbose mode */

    ret = mbedtls_sha256_self_test(v);
    if (ret != 0)
        return;
    ret = mbedtls_aes_self_test(v);
    if (ret != 0)
        return;
    ret = mbedtls_gcm_self_test(v);
    if (ret != 0)
        return;
    ret = mbedtls_rsa_self_test(v);
    if (ret != 0)
        return;
    ret = mbedtls_ecp_self_test(v);
    if (ret != 0)
        return;

    /* Test ECDH (PKA) */
    {
        mbedtls_ecp_group grp;
        mbedtls_mpi d;
        mbedtls_ecp_point Q;
        mbedtls_ecp_group_init( &grp );
        mbedtls_mpi_init( &d );
        mbedtls_ecp_point_init( &Q );

        printf( "  Testing ECDH key generation (PKA)...\n" );
        if( mbedtls_ecp_group_load( &grp, MBEDTLS_ECP_DP_SECP256R1 ) != 0 )
        {
            printf( "  Failed to load SECP256R1 group\n" );
        }
        else
        {
            if( mbedtls_ecdh_gen_public( &grp, &d, &Q, dummy_rng, NULL ) == 0 )
            {
                printf( "  Successfully generated ECDH key pair using PKA\n\n" );
            }
            else
            {
                printf( "  Failed to generate ECDH key pair\n\n" );
            }
        }
        mbedtls_ecp_group_free( &grp );
        mbedtls_mpi_free( &d );
        mbedtls_ecp_point_free( &Q );
    }
    return;
}