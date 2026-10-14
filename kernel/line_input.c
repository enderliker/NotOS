#include "line_input.h"
#include "gui.h"
#include "file_command.h"
#include "vfs.h"
static struct line_editor editor;
static void (*execute)(const char *);
static void serial_byte(char character) {
    while (!(inb(0x3fd) & 0x20)) {}
    outb(0x3f8, (u8)character);
}
static void move_cursor(int distance) {
    if (!distance) return;
    serial_byte(27); serial_byte('[');
    u32 magnitude = distance < 0 ? (u32)-distance : (u32)distance;
    char digits[10]; u32 count = 0;
    do {digits[count++] = (char)('0' + magnitude % 10); magnitude /= 10;} while (magnitude);
    while (count) serial_byte(digits[--count]);
    serial_byte(distance < 0 ? 'D' : 'C');
    gui_move_cursor(distance);
}
static void redraw(u32 previous_length, u32 previous_cursor) {
    move_cursor(-(int)previous_cursor);
    console_puts(editor.text);
    for (u32 i = editor.length; i < previous_length; i++) console_putc(' ');
    u32 rendered = editor.length > previous_length ? editor.length : previous_length;
    move_cursor((int)editor.cursor - (int)rendered);
}
void line_input_key(enum line_key key) {
    u32 previous_length = editor.length, previous_cursor = editor.cursor;
    line_edit(&editor, key);
    redraw(previous_length, previous_cursor);
}
static void complete(void) {
    static const char *commands[] = {"help","about","clear","echo","run","run-user","exec","curl","spawn","spawn-elf","ps","kill","process-trace","cd","pwd","ls","cat","write","touch","rm","stat","df","mkfs","mkdir","rmdir","mem","heap","vfs","sockets","http","https","dns","int3","libc-run","elf-run","feed","feed-eof"};
    u32 start = editor.cursor;
    while (start && editor.text[start - 1] != ' ') start--;
    char prefix[NOTOS_PATH_BYTES];
    u32 bytes = editor.cursor - start;
    if (bytes >= sizeof(prefix)) return;
    for (u32 i = 0; i < bytes; i++) prefix[i] = editor.text[start + i];
    prefix[bytes] = 0;
    char common[NOTOS_PATH_BYTES] = {0}, matches[32][24];
    u32 count = 0, common_bytes = 0;
    int is_command = start == 0;
    char directory[NOTOS_PATH_BYTES];
    u32 basename = 0;
    for (u32 i = 0; i < bytes; i++) if (prefix[i] == '/') basename = i + 1;
    if (!is_command) {
        char parent[NOTOS_PATH_BYTES];
        for (u32 i = 0; i < basename; i++) parent[i] = prefix[i];
        parent[basename] = 0;
        if (file_resolve(parent, directory)) return;
    }
    for (u32 index = 0;; index++) {
        const char *candidate;
        struct notos_directory_entry entry;
        if (is_command) {
            if (index == sizeof(commands) / sizeof(commands[0])) break;
            candidate = commands[index];
        } else {
            if (vfs_directory_entry(directory, index, &entry) <= 0) break;
            candidate = entry.name;
        }
        if (!starts(candidate, prefix + basename)) continue;
        if (!count) {
            common_bytes = length(candidate);
            for (u32 i = 0; i <= common_bytes; i++) common[i] = candidate[i];
        } else {
            u32 i = 0;
            while (i < common_bytes && candidate[i] && candidate[i] == common[i]) i++;
            common_bytes = i; common[i] = 0;
        }
        if (count < 32) for (u32 i = 0; (matches[count][i] = candidate[i]); i++) {}
        count++;
    }
    if (!count) return;
    u32 previous_length = editor.length, previous_cursor = editor.cursor;
    for (u32 i = bytes - basename; i < common_bytes; i++) line_insert(&editor, common[i]);
    if (count == 1 && editor.cursor == editor.length) {
        struct notos_file_info info;
        char resolved[NOTOS_PATH_BYTES], input[NOTOS_PATH_BYTES];
        u32 size = editor.cursor - start;
        if (size < sizeof(input)) {
            for (u32 i = 0; i < size; i++) input[i] = editor.text[start + i];
            input[size] = 0;
            int directory_match = !is_command && !file_resolve(input, resolved)
                && !vfs_stat(resolved, &info) && info.kind == NOTOS_FILE_DIRECTORY;
            line_insert(&editor, directory_match ? '/' : ' ');
        }
    }
    if (count > 1 && editor.cursor == previous_cursor) {
        console_putc('\n');
        for (u32 i = 0; i < count && i < 32; i++) {console_puts(matches[i]); console_putc(' ');}
        console_puts("\nnotos> "); console_puts(editor.text);
        move_cursor((int)editor.cursor - (int)editor.length);
    } else redraw(previous_length, previous_cursor);
}
void line_input_char(char character) {
    static int cr;
    static u32 escape, parameter;
    if (escape) {
        if (escape == 1) {escape = character == '[' || character == 'O' ? 2 : 0; parameter = 0; return;}
        if (character >= '0' && character <= '9') {parameter = parameter < 100 ? parameter * 10 + (u32)(character - '0') : 100; return;}
        if (character == ';') return;
        escape = 0;
        if (character == 'A') line_input_key(LINE_UP);
        else if (character == 'B') line_input_key(LINE_DOWN);
        else if (character == 'C') line_input_key(LINE_RIGHT);
        else if (character == 'D') line_input_key(LINE_LEFT);
        else if (character == 'H' || (character == '~' && (parameter == 1 || parameter == 7))) line_input_key(LINE_HOME);
        else if (character == 'F' || (character == '~' && (parameter == 4 || parameter == 8))) line_input_key(LINE_END);
        else if (character == '~' && parameter == 3) line_input_key(LINE_DELETE);
        return;
    }
    if (character == 27) {escape = 1; return;}
    if (character == '\n' && cr) {cr = 0; return;}
    cr = character == '\r';
    if (character == '\r' || character == '\n') {
        move_cursor((int)editor.length - (int)editor.cursor);
        console_putc('\n');
        int extended = starts(editor.text, "curl ") || starts(editor.text, "spawn-elf ");
        if (!extended && editor.length >= 160) editor.overflow = 1;
        if (editor.overflow) console_puts("ERROR: input too long; discarded\n");
        else if (execute) execute(editor.text);
        line_submit(&editor);
        console_puts("notos> ");
    } else if (character == '\t') complete();
    else if (character == '\b' || character == 127) line_input_key(LINE_BACKSPACE);
    else if (character >= 32 && character <= 126) {
        u32 previous_length = editor.length, previous_cursor = editor.cursor;
        line_insert(&editor, character);
        if (previous_cursor == previous_length && editor.length > previous_length) console_putc(character);
        else redraw(previous_length, previous_cursor);
    }
}
void line_input_clear(void) {line_reset(&editor);}
void line_input_set_command(void (*command)(const char *)) {execute = command;}
