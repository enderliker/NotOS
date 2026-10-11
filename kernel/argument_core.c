#include "argument_core.h"

static int argument_fits(const char *text, unsigned limit) {
    for (unsigned i = 0; i <= limit; i++) {
        if (!text[i]) return 1;
    }
    return 0;
}

int argument_split(struct argument_list *list, const char *name, const char *text) {
    if (!list || !name || !*name || !argument_fits(name, ARGUMENT_NAME_BYTES)) return 0;
    if (text && !argument_fits(text, ARGUMENT_MAX_BYTES)) return 0;
    list->count = 1;
    list->values[0] = name;
    unsigned bytes = 0;
    if (text) {
        while (text[bytes]) {
            list->buffer[bytes] = text[bytes];
            bytes++;
        }
    }
    list->buffer[bytes] = 0;
    char *cursor = list->buffer;
    while (*cursor) {
        while (*cursor == ' ') cursor++;
        if (!*cursor) break;
        if (list->count == ARGUMENT_MAX_COUNT) return 0;
        list->values[list->count++] = cursor;
        while (*cursor && *cursor != ' ') cursor++;
        if (*cursor) *cursor++ = 0;
    }
    return 1;
}
