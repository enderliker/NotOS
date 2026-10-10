#ifndef NOTOS_MOUSE_DECODE_H
#define NOTOS_MOUSE_DECODE_H
#include "base.h"
struct mouse_decoder { u8 bytes[3]; unsigned used; };
struct mouse_motion { int dx, dy; unsigned buttons; };
int mouse_decode(struct mouse_decoder *decoder, u8 byte, struct mouse_motion *motion);
#endif
