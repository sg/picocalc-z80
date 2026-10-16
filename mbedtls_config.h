/*
 * mbedtls configuration for the update client's HTTPS, RP2350 boards only.
 * A TLS 1.2 client with the cipher suites GitHub and Forgejo offer, no
 * server, no TLS 1.3, and no certificate verification yet: the download is
 * checked against the release's SHA-256 sums instead. Started from
 * picocalc_lua's, which came from the SDK's kitchen_sink example.
 */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* some mbedtls sources use INT_MAX without including limits.h */
#include <limits.h>

/* system support: the SDK's pico_mbedtls.c feeds the entropy pool */
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* platform */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MS_TIME_ALT

/* mbedtls runs inside lwIP's callbacks, in an interrupt on core 0, where the
   SDK's malloc mutex must not be taken: with this set, lwIP's altcp layer
   points mbedtls at lwIP's own heap instead, which the lwIP lock already
   serializes; MEM_SIZE in lwipopts.h is sized for a session */
#define MBEDTLS_PLATFORM_MEMORY

/* records: incoming at the protocol's 16 KB, since servers send full-size
   records whatever the client asks; outgoing has to hold one whole request,
   because lwIP's altcp layer cannot split a write across records */
#define MBEDTLS_SSL_OUT_CONTENT_LEN     4096
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_SHA256_SMALLER

/* lwIP's altcp_tls reaches into the ssl context */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

/* crypto */
#define MBEDTLS_AES_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_ERROR_C
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PKCS5_C
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C

/* cipher modes */
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_GCM_C

/* RSA/PKCS */
#define MBEDTLS_PKCS1_V15

/* elliptic curves */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED

/* key exchange */
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED

/* TLS 1.2 client only */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2

/* certificates are parsed even though they are not verified */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* GitHub's asset host needs SNI */
#define MBEDTLS_SSL_SERVER_NAME_INDICATION

#endif /* !MBEDTLS_CONFIG_H */
