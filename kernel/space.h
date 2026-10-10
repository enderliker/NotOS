#ifndef NOTOS_SPACE_H
#define NOTOS_SPACE_H
#include "user_memory.h"
struct user_space {u32 root,directory,table,code,data,stack;};
int space_create(struct user_space *space,const u8 *image,unsigned bytes);
int space_destroy(struct user_space *space);
int space_range(const struct user_space *space,u32 address,u32 bytes,int writable);
#endif
