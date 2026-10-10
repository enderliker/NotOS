/* Delegate chain/name/signature/time checks to BearSSL; add bounded leaf policy. */
#include "x509_policy.h"
#include "certificate_policy.h"
#include <string.h>
static struct x509_policy *context(const br_x509_class **ctx) {return (struct x509_policy *)ctx;}
static void start_chain(const br_x509_class **ctx,const char *name) {
    struct x509_policy *p=context(ctx);p->certificates=p->leaf_bytes=0;p->rejected=0;
    p->validator->vtable->start_chain(&p->validator->vtable,name);
}
static void start_cert(const br_x509_class **ctx,uint32_t bytes) {
    struct x509_policy *p=context(ctx);p->certificates++;p->leaf_bytes=0;
    if (p->certificates>8 || bytes>sizeof(p->leaf)) p->rejected=1;
    p->validator->vtable->start_cert(&p->validator->vtable,bytes);
}
static void append(const br_x509_class **ctx,const unsigned char *data,size_t bytes) {
    struct x509_policy *p=context(ctx);
    {
        if (bytes>sizeof(p->leaf)-p->leaf_bytes) p->rejected=1;
        else {memcpy(p->leaf+p->leaf_bytes,data,bytes);p->leaf_bytes+=(unsigned)bytes;}
    }
    p->validator->vtable->append(&p->validator->vtable,data,bytes);
}
static void end_cert(const br_x509_class **ctx) {
    struct x509_policy *p=context(ctx);
    if (!certificate_policy_role(p->leaf,p->leaf_bytes,p->certificates==1)) p->rejected=1;
    p->validator->vtable->end_cert(&p->validator->vtable);
}
static unsigned end_chain(const br_x509_class **ctx) {
    struct x509_policy *p=context(ctx);unsigned result=p->validator->vtable->end_chain(&p->validator->vtable);
    return p->rejected?BR_ERR_X509_FORBIDDEN_KEY_USAGE:result;
}
static const br_x509_pkey *get_pkey(const br_x509_class *const *ctx,unsigned *usages) {
    const struct x509_policy *p=(const struct x509_policy *)ctx;
    if (p->rejected) return 0;
    return p->validator->vtable->get_pkey(&p->validator->vtable,usages);
}
static const br_x509_class policy_class={sizeof(struct x509_policy),start_chain,start_cert,append,end_cert,end_chain,get_pkey};
void x509_policy_init(struct x509_policy *policy,br_x509_minimal_context *validator) {
    memset(policy,0,sizeof(*policy));policy->vtable=&policy_class;policy->validator=validator;
}
