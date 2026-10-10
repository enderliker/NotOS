#ifndef NOTOS_CERTIFICATE_POLICY_H
#define NOTOS_CERTIFICATE_POLICY_H
#include "base.h"
/* DNS SAN required on the leaf; EKU in any sent certificate must permit serverAuth. */
int certificate_policy(const u8 *der,unsigned bytes);
int certificate_policy_role(const u8 *der,unsigned bytes,int require_dns_san);
#endif
