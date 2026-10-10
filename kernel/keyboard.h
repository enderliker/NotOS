#ifndef NOTOS_KEYBOARD_H
#define NOTOS_KEYBOARD_H
#include "base.h"
int keyboard_init(void);
void keyboard_irq(void);
int keyboard_next(u8 *code);
unsigned keyboard_dropped(void);
#endif
