#ifndef NOTOS_TLS_H
#define NOTOS_TLS_H
#include "base.h"
int tls_start(const char *hostname);
void tls_stop(void);
/* Transport interfaces: bytes copied on input, caller ACKs output after queuing TCP. */
int tls_feed(const u8 *bytes,unsigned size);
const u8 *tls_output(unsigned *size);
void tls_output_ack(unsigned size);
void tls_eof(void);
void tls_poll(void);
/* 0 in progress, 1 authenticated clean closure, -1 failure. */
int tls_result(void);
int tls_verified(void);
void tls_status(void);
int tls_selftest(void);
#endif
