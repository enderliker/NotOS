/* TLS 1.2 only; forward-secret AEAD suites, portable constant-time crypto. */
#include "tls_profile.h"
void tls_profile(br_ssl_client_context *client,br_x509_minimal_context *validator,
    const br_x509_trust_anchor *anchors,size_t count) {
    static const uint16_t suites[]={
        BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
    };
    br_ssl_client_zero(client);
    br_ssl_engine_set_versions(&client->eng,BR_TLS12,BR_TLS12);
    br_ssl_engine_set_suites(&client->eng,suites,sizeof(suites)/sizeof(suites[0]));
    br_ssl_engine_set_ec(&client->eng,&br_ec_all_m31);
    br_ssl_engine_set_rsavrfy(&client->eng,br_rsa_i31_pkcs1_vrfy);
    br_ssl_engine_set_ecdsa(&client->eng,br_ecdsa_i31_vrfy_asn1);
    br_x509_minimal_init(validator,&br_sha256_vtable,anchors,count);
    br_x509_minimal_set_rsa(validator,br_rsa_i31_pkcs1_vrfy);
    br_x509_minimal_set_ecdsa(validator,&br_ec_all_m31,br_ecdsa_i31_vrfy_asn1);
    br_x509_minimal_set_minrsa(validator,256); /* RSA >=2048 bits. */
    const br_hash_class *hashes[]={&br_sha256_vtable,&br_sha384_vtable,&br_sha512_vtable};
    for (unsigned i=0;i<3;i++) {
        unsigned id=i+br_sha256_ID;
        br_ssl_engine_set_hash(&client->eng,id,hashes[i]);
        br_x509_minimal_set_hash(validator,id,hashes[i]);
    }
    br_ssl_engine_set_x509(&client->eng,&validator->vtable);
    br_ssl_engine_set_prf_sha256(&client->eng,br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(&client->eng,br_tls12_sha384_prf);
    br_ssl_engine_set_default_aes_gcm(&client->eng);
    br_ssl_engine_set_default_chapol(&client->eng);
}
