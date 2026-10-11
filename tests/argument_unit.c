#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/argument_core.h"

int main(void) {
    struct argument_list list;
    assert(!argument_split(0, "curl", ""));
    assert(!argument_split(&list, 0, ""));
    assert(!argument_split(&list, "", ""));
    assert(argument_split(&list, "curl", 0) && list.count == 1);
    assert(argument_split(&list, "curl", "   ") && list.count == 1);
    assert(argument_split(&list, "curl", "  -s  -o /tmp/result  http://host/  "));
    assert(list.count == 5 && !strcmp(list.values[0], "curl"));
    assert(!strcmp(list.values[1], "-s") && !strcmp(list.values[3], "/tmp/result"));
    assert(!strcmp(list.values[4], "http://host/"));
    char text[ARGUMENT_MAX_BYTES + 2];
    memset(text, 'a', sizeof(text));
    text[ARGUMENT_MAX_BYTES] = 0;
    assert(argument_split(&list, "curl", text) && strlen(list.values[1]) == ARGUMENT_MAX_BYTES);
    text[ARGUMENT_MAX_BYTES] = 'a';
    text[ARGUMENT_MAX_BYTES + 1] = 0;
    assert(!argument_split(&list, "curl", text));
    text[ARGUMENT_NAME_BYTES] = 0;
    assert(argument_split(&list, text, ""));
    text[ARGUMENT_NAME_BYTES] = 'a';
    text[ARGUMENT_NAME_BYTES + 1] = 0;
    assert(!argument_split(&list, text, ""));
    strcpy(text, "a a a a a a a a a a a a a a a");
    assert(argument_split(&list, "curl", text) && list.count == ARGUMENT_MAX_COUNT);
    strcat(text, " a");
    assert(!argument_split(&list, "curl", text));
    assert(argument_split(&list, "curl", "\"two words\" \\literal\ttab"));
    assert(list.count == 4 && !strcmp(list.values[1], "\"two"));
    assert(!strcmp(list.values[3], "\\literal\ttab"));
    puts("PASS: argument limits, spaces, literal quotes and escapes");
    return 0;
}
