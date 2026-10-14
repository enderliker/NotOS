#ifndef NOTOS_SHELL_PARSE_CORE_H
#define NOTOS_SHELL_PARSE_CORE_H
#include "base.h"

#define SHELL_PARSE_MAX_JOBS 8u
#define SHELL_PARSE_MAX_STAGES 4u
#define SHELL_PARSE_MAX_ARGS 16u
#define SHELL_PARSE_STORAGE_BYTES 1024u

#define SHELL_PARSE_OK 0
#define SHELL_PARSE_SYNTAX (-1)
#define SHELL_PARSE_OVERFLOW (-2)

typedef const char *(*shell_lookup_fn)(const char *name, void *context);

struct shell_command {
    u32 argc;
    const char *argv[SHELL_PARSE_MAX_ARGS + 1];
    const char *input_path;
    const char *output_path;
    int output_append;
};

struct shell_job {
    u32 stage_count;
    int background;
    struct shell_command stages[SHELL_PARSE_MAX_STAGES];
};

struct shell_program {
    u32 job_count;
    struct shell_job jobs[SHELL_PARSE_MAX_JOBS];
    char storage[SHELL_PARSE_STORAGE_BYTES];
    u32 storage_used;
};

/*
 * Parse a shell command line into sequential/background jobs and pipelines.
 * Supports single quotes ('...'), double quotes ("..."), backslash escapes (\),
 * variable expansion ($VAR, ${VAR}, $?), pipes (|), redirections (<, >, >>),
 * background execution (&), and command separators (;).
 * Returns SHELL_PARSE_OK (0) on success, SHELL_PARSE_SYNTAX (-1) on malformed
 * syntax, or SHELL_PARSE_OVERFLOW (-2) if any bounded table/storage overflows.
 */
int shell_parse_line(const char *line, shell_lookup_fn lookup, void *context,
    int last_status, struct shell_program *out);

#endif
