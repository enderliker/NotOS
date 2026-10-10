#ifndef NOTOS_NET_H
#define NOTOS_NET_H
void net_init(void);
void net_poll(unsigned now_ms);
void net_command(const char *kind,const char *host);
void net_status(void);
void net_configure(const char *argument);
void net_drop_once(const char *kind);
#endif
