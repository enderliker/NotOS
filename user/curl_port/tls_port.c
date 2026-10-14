/* Guest TLS platform and policy interposition; upstream sources stay unchanged. */
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "../../kernel/certificate_policy.h"

int mbedtls_hardware_poll(void *context, unsigned char *output, size_t bytes, size_t *written) {
    (void)context;
    *written = 0;
    while (bytes) {
        size_t chunk = bytes > 256 ? 256 : bytes;
        if (getentropy(output, chunk)) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        output += chunk;
        bytes -= chunk;
        *written += chunk;
    }
    return 0;
}
psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t *context,
        uint8_t *output, size_t bytes, size_t *written) {
    return mbedtls_hardware_poll(context, output, bytes, written)
        ? PSA_ERROR_INSUFFICIENT_ENTROPY : PSA_SUCCESS;
}
mbedtls_ms_time_t mbedtls_ms_time(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
/* curl enables file parsing in its translation unit; the library uses this bounded adapter. */
int mbedtls_x509_crt_parse_file(mbedtls_x509_crt *chain, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return MBEDTLS_ERR_X509_FILE_IO_ERROR;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return MBEDTLS_ERR_X509_FILE_IO_ERROR; }
    long bytes = ftell(file);
    if (bytes <= 0 || bytes > 256 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return MBEDTLS_ERR_X509_FILE_IO_ERROR;
    }
    unsigned char *buffer = calloc(1, (size_t)bytes + 1);
    if (!buffer) { fclose(file); return MBEDTLS_ERR_X509_ALLOC_FAILED; }
    int result = fread(buffer, 1, (size_t)bytes, file) == (size_t)bytes
        ? mbedtls_x509_crt_parse(chain, buffer, (size_t)bytes + 1) : MBEDTLS_ERR_X509_FILE_IO_ERROR;
    free(buffer);
    fclose(file);
    return result > 0 ? MBEDTLS_ERR_X509_INVALID_FORMAT : result;
}
int mbedtls_x509_crt_parse_path(mbedtls_x509_crt *chain, const char *path) {
    (void)chain;
    (void)path;
    return MBEDTLS_ERR_X509_FEATURE_UNAVAILABLE;
}
int __real_mbedtls_ssl_config_defaults(mbedtls_ssl_config *, int, int, int);
int __wrap_mbedtls_ssl_config_defaults(mbedtls_ssl_config *config, int endpoint, int transport, int preset) {
    time_t now;
    if (time(&now) == (time_t)-1) return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    return __real_mbedtls_ssl_config_defaults(config, endpoint, transport, preset);
}
void __real_mbedtls_ssl_conf_cert_profile(mbedtls_ssl_config *, const mbedtls_x509_crt_profile *);
void __wrap_mbedtls_ssl_conf_cert_profile(mbedtls_ssl_config *config, const mbedtls_x509_crt_profile *profile) {
    (void)profile;
    static const mbedtls_x509_crt_profile strict = {
        MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA256) | MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA384)
            | MBEDTLS_X509_ID_FLAG(MBEDTLS_MD_SHA512),
        0xffffffffu, 0xffffffffu, 2048
    };
    __real_mbedtls_ssl_conf_cert_profile(config, &strict);
}
static int verify_policy(void *context, mbedtls_x509_crt *certificate, int depth, uint32_t *flags) {
    (void)context;
    if (depth < 0 || depth > 7 || certificate->raw.len > 16384
        || !certificate_policy_role(certificate->raw.p, certificate->raw.len, depth == 0))
        *flags |= MBEDTLS_X509_BADCERT_OTHER;
    return 0;
}
void __real_mbedtls_ssl_conf_verify(mbedtls_ssl_config *,
    int (*)(void *, mbedtls_x509_crt *, int, uint32_t *), void *);
void __wrap_mbedtls_ssl_conf_verify(mbedtls_ssl_config *config,
        int (*callback)(void *, mbedtls_x509_crt *, int, uint32_t *), void *context) {
    (void)callback;
    (void)context;
    __real_mbedtls_ssl_conf_verify(config, verify_policy, 0);
}
int __real_mbedtls_ssl_read(mbedtls_ssl_context *, unsigned char *, size_t);
int __wrap_mbedtls_ssl_read(mbedtls_ssl_context *ssl, unsigned char *buffer, size_t bytes) {
    int result = __real_mbedtls_ssl_read(ssl, buffer, bytes);
    return result == 0 ? MBEDTLS_ERR_SSL_CONN_EOF : result;
}
int mbedtls_pk_parse_keyfile(mbedtls_pk_context *context, const char *path, const char *password,
        int (*random)(void *, unsigned char *, size_t), void *random_context) {
    (void)context;
    (void)path;
    (void)password;
    (void)random;
    (void)random_context;
    return MBEDTLS_ERR_PK_FEATURE_UNAVAILABLE;
}
