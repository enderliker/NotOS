#include "mouse_decode.h"
int mouse_decode(struct mouse_decoder *decoder, u8 byte, struct mouse_motion *motion) {
    if (!decoder || !motion) return 0;
    if (!decoder->used && !(byte & 8)) return 0;
    decoder->bytes[decoder->used++] = byte;
    if (decoder->used < 3) return 0;
    decoder->used = 0;
    u8 flags = decoder->bytes[0];
    if (flags & 0xc0) return 0; /* Overflow packet: do not move or click. */
    motion->dx = (int)decoder->bytes[1] - ((flags & 0x10) ? 256 : 0);
    motion->dy = -((int)decoder->bytes[2] - ((flags & 0x20) ? 256 : 0));
    motion->buttons = flags & 7;
    return 1;
}
