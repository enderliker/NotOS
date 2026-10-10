#ifndef NOTOS_PIC_H
#define NOTOS_PIC_H
void pic_init(void);
void pic_unmask(unsigned irq);
void pic_mask(unsigned irq);
int pic_spurious(unsigned irq);
void pic_eoi(unsigned irq);
#endif
