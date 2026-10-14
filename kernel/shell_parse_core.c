#include "shell_parse_core.h"

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int is_operator(char c) {
    return c == '|' || c == '<' || c == '>' || c == '&' || c == ';';
}

static int is_var_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_var_char(char c) {
    return is_var_start(c) || (c >= '0' && c <= '9');
}

static int stage_empty(const struct shell_command *stage) {
    return stage->argc == 0 && !stage->input_path && !stage->output_path;
}

static int push_char(struct shell_program *out, char c) {
    if (out->storage_used >= SHELL_PARSE_STORAGE_BYTES) return SHELL_PARSE_OVERFLOW;
    out->storage[out->storage_used++] = c;
    return SHELL_PARSE_OK;
}

static int push_status(struct shell_program *out, int status) {
    char digits[12];
    u32 count = 0;
    u32 magnitude = status < 0 ? (0u - (u32)status) : (u32)status;
    if (status < 0) {
        if (push_char(out, '-') != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
    }
    do {
        digits[count++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude);
    while (count) {
        if (push_char(out, digits[--count]) != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
    }
    return SHELL_PARSE_OK;
}

static int expand_dollar(const char *line, u32 *cursor, shell_lookup_fn lookup,
    void *context, int last_status, struct shell_program *out, int *saw_literal) {
    u32 pos = *cursor;
    char next = line[pos];
    if (next == '?') {
        *cursor = pos + 1u;
        return push_status(out, last_status);
    }
    if (next == '{') {
        pos++;
        if (line[pos] == '?' && line[pos + 1u] == '}') {
            *cursor = pos + 2u;
            return push_status(out, last_status);
        }
        if (!is_var_start(line[pos])) return SHELL_PARSE_SYNTAX;
        char name[64];
        u32 len = 0;
        while (is_var_char(line[pos])) {
            if (len + 1u >= sizeof(name)) return SHELL_PARSE_OVERFLOW;
            name[len++] = line[pos++];
        }
        if (line[pos] != '}') return SHELL_PARSE_SYNTAX;
        name[len] = 0;
        *cursor = pos + 1u;
        const char *val = lookup ? lookup(name, context) : 0;
        if (!val) return SHELL_PARSE_OK;
        for (u32 i = 0; val[i]; i++) {
            if (push_char(out, val[i]) != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
        }
        return SHELL_PARSE_OK;
    }
    if (is_var_start(next)) {
        char name[64];
        u32 len = 0;
        while (is_var_char(line[pos])) {
            if (len + 1u >= sizeof(name)) return SHELL_PARSE_OVERFLOW;
            name[len++] = line[pos++];
        }
        name[len] = 0;
        *cursor = pos;
        const char *val = lookup ? lookup(name, context) : 0;
        if (!val) return SHELL_PARSE_OK;
        for (u32 i = 0; val[i]; i++) {
            if (push_char(out, val[i]) != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
        }
        return SHELL_PARSE_OK;
    }
    *saw_literal = 1;
    return push_char(out, '$');
}

/*
 * Parse a single word starting at line[*cursor]. Sets *word_out to the
 * null-terminated string in out->storage, or NULL if an unquoted variable
 * expanded to empty with no literal or quoted characters.
 */
static int parse_word(const char *line, u32 *cursor, shell_lookup_fn lookup,
    void *context, int last_status, struct shell_program *out, const char **word_out) {
    u32 start_storage = out->storage_used;
    int saw_literal = 0;
    u32 pos = *cursor;

    while (line[pos] && !is_space(line[pos]) && !is_operator(line[pos])) {
        char c = line[pos++];
        if (c == '\'') {
            saw_literal = 1;
            while (line[pos] && line[pos] != '\'') {
                if (push_char(out, line[pos++]) != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
            }
            if (line[pos] != '\'') return SHELL_PARSE_SYNTAX;
            pos++;
        } else if (c == '"') {
            saw_literal = 1;
            while (line[pos] && line[pos] != '"') {
                char qc = line[pos++];
                if (qc == '\\') {
                    char esc = line[pos];
                    if (!esc) return SHELL_PARSE_SYNTAX;
                    if (esc == '"' || esc == '\\' || esc == '$') {
                        pos++;
                        if (push_char(out, esc) != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
                    } else {
                        if (push_char(out, '\\') != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
                        if (push_char(out, line[pos++]) != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
                    }
                } else if (qc == '$') {
                    int rc = expand_dollar(line, &pos, lookup, context, last_status, out, &saw_literal);
                    if (rc != SHELL_PARSE_OK) return rc;
                } else {
                    if (push_char(out, qc) != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
                }
            }
            if (line[pos] != '"') return SHELL_PARSE_SYNTAX;
            pos++;
        } else if (c == '\\') {
            if (!line[pos]) return SHELL_PARSE_SYNTAX;
            saw_literal = 1;
            if (push_char(out, line[pos++]) != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
        } else if (c == '$') {
            int rc = expand_dollar(line, &pos, lookup, context, last_status, out, &saw_literal);
            if (rc != SHELL_PARSE_OK) return rc;
        } else {
            saw_literal = 1;
            if (push_char(out, c) != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
        }
    }

    *cursor = pos;
    if (out->storage_used == start_storage && !saw_literal) {
        *word_out = 0;
        return SHELL_PARSE_OK;
    }
    if (push_char(out, 0) != SHELL_PARSE_OK) return SHELL_PARSE_OVERFLOW;
    *word_out = &out->storage[start_storage];
    return SHELL_PARSE_OK;
}

int shell_parse_line(const char *line, shell_lookup_fn lookup, void *context,
    int last_status, struct shell_program *out) {
    if (!line || !out) return SHELL_PARSE_SYNTAX;
    *out = (struct shell_program){0};

    u32 pos = 0;
    int active_job = 0;
    struct shell_job *job = 0;
    struct shell_command *stage = 0;

    for (;;) {
        while (is_space(line[pos])) pos++;
        char c = line[pos];
        if (!c) break;

        if (c == ';' || c == '&') {
            if (!active_job || stage->argc == 0) return SHELL_PARSE_SYNTAX;
            job->background = (c == '&');
            active_job = 0;
            pos++;
            continue;
        }
        if (c == '|') {
            if (!active_job || stage->argc == 0) return SHELL_PARSE_SYNTAX;
            pos++;
            if (line[pos] == '|' || line[pos] == '&') return SHELL_PARSE_SYNTAX;
            if (job->stage_count >= SHELL_PARSE_MAX_STAGES) return SHELL_PARSE_OVERFLOW;
            stage = &job->stages[job->stage_count++];
            continue;
        }

        if (!active_job) {
            if (out->job_count >= SHELL_PARSE_MAX_JOBS) return SHELL_PARSE_OVERFLOW;
            job = &out->jobs[out->job_count++];
            job->stage_count = 1u;
            stage = &job->stages[0];
            active_job = 1;
        }

        if (c == '<' || c == '>') {
            int is_input = (c == '<');
            int append = 0;
            pos++;
            if (!is_input && line[pos] == '>') {
                append = 1;
                pos++;
            }
            while (is_space(line[pos])) pos++;
            if (!line[pos] || is_operator(line[pos])) return SHELL_PARSE_SYNTAX;
            const char *target = 0;
            int rc = parse_word(line, &pos, lookup, context, last_status, out, &target);
            if (rc != SHELL_PARSE_OK) return rc;
            if (!target || !*target) return SHELL_PARSE_SYNTAX;
            if (is_input) {
                stage->input_path = target;
            } else {
                stage->output_path = target;
                stage->output_append = append;
            }
            continue;
        }

        const char *word = 0;
        int rc = parse_word(line, &pos, lookup, context, last_status, out, &word);
        if (rc != SHELL_PARSE_OK) return rc;
        if (word) {
            if (stage->argc >= SHELL_PARSE_MAX_ARGS) return SHELL_PARSE_OVERFLOW;
            stage->argv[stage->argc++] = word;
            stage->argv[stage->argc] = 0;
        } else if (stage_empty(stage) && job->stage_count == 1u) {
            /* Word was an unquoted empty variable at the start of a fresh job; keep job open only if more tokens follow. */
            u32 lookahead = pos;
            while (is_space(line[lookahead])) lookahead++;
            if (!line[lookahead]) {
                out->job_count--;
                active_job = 0;
            }
        }
    }

    if (active_job && stage->argc == 0) return SHELL_PARSE_SYNTAX;
    return SHELL_PARSE_OK;
}
