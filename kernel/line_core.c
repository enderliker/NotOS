#include "line_core.h"
static void copy(char *destination, const char *source) {
    for (u32 i = 0; (destination[i] = source[i]); i++) {}
}
void line_reset(struct line_editor *editor) {
    editor->length = editor->cursor = editor->browsing = 0;
    editor->overflow = 0;
    editor->text[0] = 0;
}
void line_insert(struct line_editor *editor, char character) {
    if (editor->overflow) return;
    if (editor->length == LINE_CORE_BYTES - 1) {editor->overflow = 1; return;}
    for (u32 i = editor->length + 1; i > editor->cursor; i--) editor->text[i] = editor->text[i - 1];
    editor->text[editor->cursor++] = character;
    editor->length++;
}
void line_edit(struct line_editor *editor, enum line_key key) {
    if (key == LINE_LEFT && editor->cursor) editor->cursor--;
    else if (key == LINE_RIGHT && editor->cursor < editor->length) editor->cursor++;
    else if (key == LINE_HOME) editor->cursor = 0;
    else if (key == LINE_END) editor->cursor = editor->length;
    else if (key == LINE_BACKSPACE || key == LINE_DELETE) {
        if (key == LINE_BACKSPACE) {if (!editor->cursor) return; editor->cursor--;}
        if (editor->cursor == editor->length) return;
        for (u32 i = editor->cursor; i < editor->length; i++) editor->text[i] = editor->text[i + 1];
        editor->length--;
    } else if (key == LINE_UP || key == LINE_DOWN) {
        if (key == LINE_UP) {
            if (editor->browsing == editor->count) return;
            if (!editor->browsing) copy(editor->draft, editor->text);
            editor->browsing++;
        } else {if (!editor->browsing) return; editor->browsing--;}
        copy(editor->text, editor->browsing ? editor->history[editor->browsing - 1] : editor->draft);
        editor->cursor = editor->length = length(editor->text);
        editor->overflow = 0;
    }
}
void line_submit(struct line_editor *editor) {
    if (!editor->overflow && editor->length && (!editor->count || !equal(editor->history[0], editor->text))) {
        u32 count = editor->count < LINE_CORE_HISTORY ? editor->count++ : LINE_CORE_HISTORY - 1;
        for (u32 i = count; i > 0; i--) copy(editor->history[i], editor->history[i - 1]);
        copy(editor->history[0], editor->text);
    }
    line_reset(editor);
}
