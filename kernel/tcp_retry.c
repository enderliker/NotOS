/* Pure single-flight retransmission policy; serial arithmetic assumes <2^31 flight. */
#include "tcp_retry.h"
void tcp_retry_begin(struct tcp_retry *retry,u32 end_sequence,u32 now) {
    *retry=(struct tcp_retry){end_sequence,now+500u,0,1};
}
void tcp_retry_ack(struct tcp_retry *retry,u32 peer_ack,u32 send_limit) {
    if (retry->active && (int)(peer_ack-retry->end_sequence)>=0
        && (int)(peer_ack-send_limit)<=0) retry->active=0;
}
int tcp_retry_due(const struct tcp_retry *retry,u32 now) {
    return retry->active && (int)(now-retry->deadline)>=0;
}
int tcp_retry_advance(struct tcp_retry *retry,u32 now) {
    if (!retry->active || retry->retries==TCP_RETRY_LIMIT) {retry->active=0;return 0;}
    retry->retries++;retry->deadline=now+(500u<<retry->retries);return 1;
}
