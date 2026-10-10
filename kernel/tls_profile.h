#ifndef NOTOS_TLS_PROFILE_H
#define NOTOS_TLS_PROFILE_H
#include <bearssl.h>
void tls_profile(br_ssl_client_context *client,br_x509_minimal_context *validator,
    const br_x509_trust_anchor *anchors,size_t count);
#endif
