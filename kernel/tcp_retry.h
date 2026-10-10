#ifndef NOTOS_TCP_RETRY_H
#define NOTOS_TCP_RETRY_H
#include "base.h"
#define TCP_RETRY_LIMIT 4u
struct tcp_retry { u32 end_sequence, deadline; unsigned retries; int active; };
void tcp_retry_begin(struct tcp_retry *retry,u32 end_sequence,u32 now);
void tcp_retry_ack(struct tcp_retry *retry,u32 peer_ack,u32 send_limit);
int tcp_retry_due(const struct tcp_retry *retry,u32 now);
int tcp_retry_advance(struct tcp_retry *retry,u32 now);
#endif
