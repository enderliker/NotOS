#ifndef NOTOS_LINE_CORE_H
#define NOTOS_LINE_CORE_H
#include "base.h"
#define LINE_CORE_BYTES 321u
#define LINE_CORE_HISTORY 8u
enum line_key { LINE_LEFT, LINE_RIGHT, LINE_HOME, LINE_END, LINE_UP, LINE_DOWN, LINE_DELETE, LINE_BACKSPACE };
struct line_editor {
    char text[LINE_CORE_BYTES], draft[LINE_CORE_BYTES];
    char history[LINE_CORE_HISTORY][LINE_CORE_BYTES];
    u32 length, cursor, count, browsing;
    int overflow;
};
/* Pure bounded editing; rejected overflow marks the submitted line invalid. */
void line_insert(struct line_editor *editor, char character);
void line_edit(struct line_editor *editor, enum line_key key);
void line_submit(struct line_editor *editor);
void line_reset(struct line_editor *editor);
#endif
