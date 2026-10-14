#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/line_core.h"
int main(void) {
    struct line_editor editor = {0};
    line_insert(&editor,'a');line_insert(&editor,'c');line_edit(&editor,LINE_LEFT);line_insert(&editor,'b');
    assert(!strcmp(editor.text,"abc") && editor.cursor==2);
    line_edit(&editor,LINE_HOME);line_edit(&editor,LINE_DELETE);assert(!strcmp(editor.text,"bc"));
    line_edit(&editor,LINE_END);line_edit(&editor,LINE_BACKSPACE);assert(!strcmp(editor.text,"b"));
    line_submit(&editor);line_insert(&editor,'x');line_edit(&editor,LINE_UP);assert(!strcmp(editor.text,"b"));
    line_edit(&editor,LINE_DOWN);assert(!strcmp(editor.text,"x"));line_submit(&editor);
    for(unsigned i=0;i<12;i++){line_insert(&editor,(char)('a'+i));line_submit(&editor);}
    assert(editor.count==8);for(unsigned i=0;i<10;i++)line_edit(&editor,LINE_UP);
    assert(editor.browsing==8 && !strcmp(editor.text,"e"));line_reset(&editor);
    for(unsigned i=0;i<LINE_CORE_BYTES;i++)line_insert(&editor,'x');
    assert(editor.overflow && editor.length==LINE_CORE_BYTES-1 && !editor.text[editor.length]);
    line_submit(&editor);assert(editor.count==8 && !editor.overflow);
    puts("PASS: line editing, cursor boundaries, history/draft/wrap and overflow rejection");
}
