/**
 * @file    ecdsa_alt.h
 * @brief   STM32H5 PKA-accelerated ECDSA implementation for mbedTLS 3.x
 *
 * This header is included by mbedtls/ecdsa.h when MBEDTLS_ECDSA_SIGN_ALT
 * or MBEDTLS_ECDSA_VERIFY_ALT is defined. No extra types are required here
 * because the alt implementation reuses the standard mbedtls_ecdsa_context
 * (which is typedef'd to mbedtls_ecp_keypair).
 */

#ifndef ECDSA_ALT_H
#define ECDSA_ALT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Nothing to declare — the mbedTLS function-level ALT hooks
 * (MBEDTLS_ECDSA_SIGN_ALT / MBEDTLS_ECDSA_VERIFY_ALT) reuse the
 * standard mbedtls_ecdsa_context without modification. */

#ifdef __cplusplus
}
#endif

#endif /* ECDSA_ALT_H */
