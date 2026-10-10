#ifndef NOTOS_X509_POLICY_H
#define NOTOS_X509_POLICY_H
#include <bearssl.h>
struct x509_policy {
    const br_x509_class *vtable;
    br_x509_minimal_context *validator;
    unsigned certificates,leaf_bytes;
    int rejected;
    unsigned char leaf[16384];
};
void x509_policy_init(struct x509_policy *policy,br_x509_minimal_context *validator);
#endif
