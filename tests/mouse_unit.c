#include <assert.h>
#include <stdio.h>
#include "../kernel/mouse_decode.h"
int main(void) {
    struct mouse_decoder decoder = {0}; struct mouse_motion motion;
    assert(!mouse_decode(&decoder, 0, &motion));
    assert(!mouse_decode(&decoder, 0x29, &motion));
    assert(!mouse_decode(&decoder, 10, &motion));
    assert(mouse_decode(&decoder, 251, &motion));
    assert(motion.dx==10 && motion.dy==5 && motion.buttons==1);
    assert(!mouse_decode(&decoder, 0xc8, &motion));
    assert(!mouse_decode(&decoder, 0, &motion));
    assert(!mouse_decode(&decoder, 0, &motion));
    assert(decoder.used==0);
    puts("PASS: PS/2 packet sync, sign, buttons and overflow rejection");
}
