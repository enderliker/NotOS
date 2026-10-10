#ifndef NOTOS_BASE_H
#define NOTOS_BASE_H
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline u16 inw(u16 p) { u16 v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0,%1" :: "a"(v), "Nd"(p)); }
static inline u32 inl(u16 p) { u32 v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline unsigned length(const char *s) { unsigned n=0; while(s[n]) n++; return n; }
static inline int equal(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return *a==*b; }
static inline int starts(const char *s,const char *prefix) { while(*prefix) if(*s++!=*prefix++) return 0; return 1; }
void console_putc(char c);
void console_puts(const char *s);
void console_int(int value);
void gui_init(void);
void gui_clear(void);
void gui_putc(char c);
#endif
