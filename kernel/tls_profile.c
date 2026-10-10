/* Certificate-authenticated ephemeral key exchange and AEAD only. No early data. */
#include "tls_profile.h"
int tls_profile(mbedtls_ssl_config *config) {
    static const int suites[]={
        MBEDTLS_TLS1_3_AES_128_GCM_SHA256,MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256,
        MBEDTLS_TLS1_3_AES_256_GCM_SHA384,
        MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,0
    };
    static const uint16_t signatures[]={
        MBEDTLS_TLS1_3_SIG_ECDSA_SECP256R1_SHA256,MBEDTLS_TLS1_3_SIG_ECDSA_SECP384R1_SHA384,
        MBEDTLS_TLS1_3_SIG_ECDSA_SECP521R1_SHA512,
        MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA256,MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA384,
        MBEDTLS_TLS1_3_SIG_RSA_PSS_RSAE_SHA512,
        MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA256,MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA384,
        MBEDTLS_TLS1_3_SIG_RSA_PKCS1_SHA512,MBEDTLS_TLS1_3_SIG_NONE
    };
    int error=mbedtls_ssl_config_defaults(config,MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
    if (error) return error;
    mbedtls_ssl_conf_min_tls_version(config,MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(config,MBEDTLS_SSL_VERSION_TLS1_3);
    mbedtls_ssl_conf_sig_algs(config,signatures);
    mbedtls_ssl_conf_ciphersuites(config,suites);return 0;
}
