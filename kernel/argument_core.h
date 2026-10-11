#ifndef NOTOS_ARGUMENT_CORE_H
#define NOTOS_ARGUMENT_CORE_H

#define ARGUMENT_MAX_COUNT 16u
#define ARGUMENT_MAX_BYTES 256u
#define ARGUMENT_NAME_BYTES 64u

struct argument_list {
    char buffer[ARGUMENT_MAX_BYTES + 1];
    const char *values[ARGUMENT_MAX_COUNT];
    unsigned count;
};

/* Split kernel-owned text on spaces; return zero on invalid names or limits. */
int argument_split(struct argument_list *list, const char *name, const char *text);

#endif
