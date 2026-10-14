#ifndef _SYS__WAIT_H
#define _SYS__WAIT_H

/* NotOS uses a flat 8-bit exit status: 0..127 normal exit, 128+sig signal termination. */
#define WNOHANG   1
#define WUNTRACED 2

#define WIFEXITED(w)   (((w) & 0xff) < 128)
#define WIFSIGNALED(w) (((w) & 0xff) >= 128)
#define WIFSTOPPED(w)  0
#define WEXITSTATUS(w) ((w) & 0xff)
#define WTERMSIG(w)    (((w) & 0xff) - 128)
#define WSTOPSIG(w)    0

#endif
