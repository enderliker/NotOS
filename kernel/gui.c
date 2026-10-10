#include "base.h"
#include "font.h"
#define COLS 49
#define ROWS 16
static char cells[ROWS][COLS];
static unsigned row,col;
static volatile u8 *const pixels=(volatile u8 *)0xa0000;
static void rect(int x,int y,int w,int h,u8 color) {
    for(int j=y;j<y+h;j++) for(int i=x;i<x+w;i++) pixels[j*320+i]=color;
}
static void glyph(int x,int y,char c,u8 fg,u8 bg) {
    unsigned index=(u8)c;
    if(index<32 || index>126) index='?';
    rect(x,y,6,8,bg);
    for(int j=0;j<7;j++) for(int i=0;i<5;i++)
        if(font[index-32][j] & (16>>i)) pixels[(y+j)*320+x+i]=fg;
}
static void text(int x,int y,const char *s,u8 color,u8 bg) {
    while(*s && x<=314) { glyph(x,y,*s++,color,bg); x+=6; }
}
static void redraw(void) {
    for(unsigned j=0;j<ROWS;j++) for(unsigned i=0;i<COLS;i++)
        glyph(12+i*6,48+j*8,cells[j][i],15,0);
}
void gui_clear(void) {
    row=col=0;
    for(unsigned j=0;j<ROWS;j++) for(unsigned i=0;i<COLS;i++) cells[j][i]=' ';
    redraw();
}
void gui_init(void) {
    rect(0,0,320,200,1);
    rect(0,0,320,23,0);
    text(10,8,"NOTOS / DESKTOP",15,0);
    rect(8,30,304,152,7);
    rect(10,32,300,13,8);
    text(15,35,"TERMINAL",15,8);
    rect(10,46,300,134,0);
    rect(0,187,320,13,0);
    text(10,190,"F1 HELP   F2 CLEAR   KEYBOARD + SERIAL",7,0);
    gui_clear();
}
void gui_putc(char c) {
    if(c=='\r') return;
    if(c=='\b') {
        if(col) col--; else if(row) { row--; col=COLS-1; }
        cells[row][col]=' ';
        glyph(12+col*6,48+row*8,' ',15,0);
        return;
    }
    if(c=='\n') { col=0; row++; }
    else {
        cells[row][col]=c;
        glyph(12+col*6,48+row*8,c,15,0);
        if(++col==COLS) {col=0;row++;}
    }
    if(row==ROWS) {
        for(unsigned j=1;j<ROWS;j++) for(unsigned i=0;i<COLS;i++) cells[j-1][i]=cells[j][i];
        for(unsigned i=0;i<COLS;i++) cells[ROWS-1][i]=' ';
        row=ROWS-1;
        redraw();
    }
}
