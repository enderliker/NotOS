#include "base.h"
#include "font.h"
#include "gui.h"
#define COLS 49
#define ROWS 16
static char cells[ROWS][COLS];
static unsigned row,col;
static int pointer_x, pointer_y, pointer_visible, pointer_drawn, minimized;
static unsigned pointer_buttons;
static u8 pointer_backing[64];
static enum gui_action pending_action;
static volatile u8 *const pixels=(volatile u8 *)0xa0000;
static void pointer_hide(void) {
    if (!pointer_drawn) return;
    for (int y=0;y<8;y++) for (int x=0;x<8;x++)
        if(pointer_x+x<320 && pointer_y+y<200)
            pixels[(pointer_y+y)*320+pointer_x+x]=pointer_backing[y*8+x];
    pointer_drawn=0;
}
static void pointer_show(void) {
    if (!pointer_visible || pointer_drawn) return;
    for (int y=0;y<8;y++) for (int x=0;x<8;x++) {
        if(pointer_x+x>=320 || pointer_y+y>=200) continue;
        unsigned at=(unsigned)((pointer_y+y)*320+pointer_x+x);
        pointer_backing[y*8+x]=pixels[at];
        if(x<=y/2) pixels[at]=(x==0 || x==y/2 || y==7)?0:15;
    }
    pointer_drawn=1;
}
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
    if (minimized) return;
    for(unsigned j=0;j<ROWS;j++) for(unsigned i=0;i<COLS;i++)
        glyph(12+i*6,48+j*8,cells[j][i],15,0);
}
void gui_clear(void) {
    pointer_hide();
    row=col=0;
    for(unsigned j=0;j<ROWS;j++) for(unsigned i=0;i<COLS;i++) cells[j][i]=' ';
    redraw();
    pointer_show();
}
void gui_init(void) {
    rect(0,0,320,200,1);
    rect(0,0,320,23,0);
    text(10,8,"NOTOS",15,0);
    text(188,8,"CLR FILES TASKS",15,0);
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
    pointer_hide();
    if(c=='\b') {
        if(col) col--; else if(row) { row--; col=COLS-1; }
        cells[row][col]=' ';
        if(!minimized) glyph(12+col*6,48+row*8,' ',15,0);
        pointer_show();
        return;
    }
    if(c=='\n') { col=0; row++; }
    else {
        cells[row][col]=c;
        if(!minimized) glyph(12+col*6,48+row*8,c,15,0);
        if(++col==COLS) {col=0;row++;}
    }
    if(row==ROWS) {
        for(unsigned j=1;j<ROWS;j++) for(unsigned i=0;i<COLS;i++) cells[j-1][i]=cells[j][i];
        for(unsigned i=0;i<COLS;i++) cells[ROWS-1][i]=' ';
        row=ROWS-1;
        redraw();
    }
    pointer_show();
}
void gui_pointer(int x,int y,unsigned buttons) {
    if(x<0 || x>=320 || y<0 || y>=200) return;
    pointer_hide(); pointer_x=x;pointer_y=y;pointer_visible=1;
    if((buttons&1) && !(pointer_buttons&1)) {
        if(y<23 && x>=188 && x<206) pending_action=GUI_CLEAR;
        else if(y<23 && x>=212 && x<242) pending_action=GUI_FILES;
        else if(y<23 && x>=248 && x<278) pending_action=GUI_TASKS;
        else if(y>=32 && y<45 && x>=10 && x<310) {
            minimized=!minimized;
            rect(8,46,304,136,minimized?1:7);
            if(!minimized) {rect(10,46,300,134,0);redraw();}
        }
    }
    pointer_buttons=buttons; pointer_show();
}
enum gui_action gui_next_action(void) {
    enum gui_action action=pending_action; pending_action=GUI_NONE; return action;
}
