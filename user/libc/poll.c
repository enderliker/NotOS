/* Native public structs are translated, not exposed as kernel-owned storage. */
#include "os.h"
#include "../../kernel/poll_abi.h"
#include <poll.h>
#include <errno.h>
#include <stddef.h>
_Static_assert(sizeof(struct pollfd)==8,"native pollfd ABI");
int poll(struct pollfd *fds,nfds_t count,int timeout) {
    if (count>NOTOS_POLL_MAX) {errno=EINVAL;return -1;}
    size_t bytes=count*sizeof(*fds);
    if (notos_checked(notos_raw_call(SYS_USER_CHECK,(uint32_t)fds,bytes,1))) return -1;
    struct notos_pollfd wire[NOTOS_POLL_MAX];
    for (nfds_t i=0;i<count;i++) wire[i]=(struct notos_pollfd){fds[i].fd,(u16)fds[i].events,0};
    int result=notos_checked(notos_raw_call(SYS_POLL,(uint32_t)wire,count,(uint32_t)timeout));
    if (result>=0) for (nfds_t i=0;i<count;i++) fds[i].revents=(short)wire[i].revents;
    return result;
}
