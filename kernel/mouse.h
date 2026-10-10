#ifndef NOTOS_MOUSE_H
#define NOTOS_MOUSE_H
#include "base.h"
int mouse_init(void);
void mouse_receive(u8 byte);
void mouse_poll(void);
void mouse_status(void);
#endif
