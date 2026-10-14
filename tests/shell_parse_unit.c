#include <assert.h>
#include <string.h>
#include "../kernel/shell_parse_core.h"

struct env_pair {
    const char *name;
    const char *value;
};

static const char *lookup_env(const char *name, void *context) {
    const struct env_pair *pairs = (const struct env_pair *)context;
    if (!pairs) return 0;
    for (unsigned i = 0; pairs[i].name; i++) {
        if (strcmp(pairs[i].name, name) == 0) return pairs[i].value;
    }
    return 0;
}

int main(void) {
    struct shell_program prog;
    struct env_pair env[] = {
        {"USER", "notos"},
        {"DIR", "/disk/sub"},
        {"SPACED", "two words"},
        {"EMPTY", ""},
        {0, 0}
    };

    /* Empty and whitespace lines */
    assert(shell_parse_line("", lookup_env, env, 0, &prog) == SHELL_PARSE_OK);
    assert(prog.job_count == 0);
    assert(shell_parse_line("   \t\r\n  ", lookup_env, env, 0, &prog) == SHELL_PARSE_OK);
    assert(prog.job_count == 0);
    assert(shell_parse_line("$UNSET", lookup_env, env, 0, &prog) == SHELL_PARSE_OK);
    assert(prog.job_count == 0);

    /* Basic words, adjacent quotes, empty quotes */
    assert(shell_parse_line("echo foo\"bar\"'baz' \"\" ''", lookup_env, env, 0, &prog) == SHELL_PARSE_OK);
    assert(prog.job_count == 1);
    assert(prog.jobs[0].stage_count == 1);
    assert(prog.jobs[0].background == 0);
    assert(prog.jobs[0].stages[0].argc == 4);
    assert(strcmp(prog.jobs[0].stages[0].argv[0], "echo") == 0);
    assert(strcmp(prog.jobs[0].stages[0].argv[1], "foobarbaz") == 0);
    assert(strcmp(prog.jobs[0].stages[0].argv[2], "") == 0);
    assert(strcmp(prog.jobs[0].stages[0].argv[3], "") == 0);
    assert(prog.jobs[0].stages[0].argv[4] == 0);

    /* Single quotes, double quotes, and backslash escapes */
    assert(shell_parse_line("echo '$USER \\\"' \"$USER \\\" \\\\ \\$ \\a\" a\\ b\\|c\\;d", lookup_env, env, 42, &prog) == SHELL_PARSE_OK);
    assert(prog.job_count == 1);
    assert(prog.jobs[0].stages[0].argc == 4);
    assert(strcmp(prog.jobs[0].stages[0].argv[1], "$USER \\\"") == 0);
    assert(strcmp(prog.jobs[0].stages[0].argv[2], "notos \" \\ $ \\a") == 0);
    assert(strcmp(prog.jobs[0].stages[0].argv[3], "a b|c;d") == 0);

    /* Variable expansion: $VAR, ${VAR}, $?, unset vs quoted unset, standalone $ */
    assert(shell_parse_line("echo $USER ${DIR}/file.txt $? $UNSET \"$UNSET\" $ $1", lookup_env, env, 127, &prog) == SHELL_PARSE_OK);
    assert(prog.job_count == 1);
    assert(prog.jobs[0].stages[0].argc == 7);
    assert(strcmp(prog.jobs[0].stages[0].argv[1], "notos") == 0);
    assert(strcmp(prog.jobs[0].stages[0].argv[2], "/disk/sub/file.txt") == 0);
    assert(strcmp(prog.jobs[0].stages[0].argv[3], "127") == 0);
    assert(strcmp(prog.jobs[0].stages[0].argv[4], "") == 0);
    assert(strcmp(prog.jobs[0].stages[0].argv[5], "$") == 0);
    assert(strcmp(prog.jobs[0].stages[0].argv[6], "$1") == 0);

    /* Pipelines and redirections */
    assert(shell_parse_line("< /tmp/in cat -n | grep foo >/tmp/out", lookup_env, env, 0, &prog) == SHELL_PARSE_OK);
    assert(prog.job_count == 1);
    assert(prog.jobs[0].stage_count == 2);
    assert(prog.jobs[0].stages[0].argc == 2);
    assert(strcmp(prog.jobs[0].stages[0].argv[0], "cat") == 0);
    assert(strcmp(prog.jobs[0].stages[0].argv[1], "-n") == 0);
    assert(strcmp(prog.jobs[0].stages[0].input_path, "/tmp/in") == 0);
    assert(prog.jobs[0].stages[0].output_path == 0);
    assert(prog.jobs[0].stages[1].argc == 2);
    assert(strcmp(prog.jobs[0].stages[1].argv[0], "grep") == 0);
    assert(strcmp(prog.jobs[0].stages[1].argv[1], "foo") == 0);
    assert(strcmp(prog.jobs[0].stages[1].output_path, "/tmp/out") == 0);
    assert(prog.jobs[0].stages[1].output_append == 0);

    /* Append redirection without spaces and variable in redirection target */
    assert(shell_parse_line("echo hi>>$DIR/log.txt", lookup_env, env, 0, &prog) == SHELL_PARSE_OK);
    assert(prog.job_count == 1);
    assert(prog.jobs[0].stages[0].argc == 2);
    assert(strcmp(prog.jobs[0].stages[0].output_path, "/disk/sub/log.txt") == 0);
    assert(prog.jobs[0].stages[0].output_append == 1);

    /* Background & and sequential ; jobs */
    assert(shell_parse_line("sleep 1 & echo a ; echo b | cat &", lookup_env, env, 0, &prog) == SHELL_PARSE_OK);
    assert(prog.job_count == 3);
    assert(prog.jobs[0].background == 1);
    assert(prog.jobs[0].stages[0].argc == 2);
    assert(prog.jobs[1].background == 0);
    assert(prog.jobs[1].stages[0].argc == 2);
    assert(prog.jobs[2].background == 1);
    assert(prog.jobs[2].stage_count == 2);

    /* Syntax errors */
    assert(shell_parse_line(0, lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo 'unclosed", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo \"unclosed", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo trailing\\", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo ${UNCLOSED", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo ${1BAD}", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("| echo a", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo a |", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo a || echo b", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo a && echo b", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("; echo a", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("& echo a", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo a ;; echo b", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo a & ; echo b", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo a >", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo a > | cat", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("echo a > $UNSET", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);
    assert(shell_parse_line("> /tmp/out", lookup_env, env, 0, &prog) == SHELL_PARSE_SYNTAX);

    /* Overflow bounds */
    assert(shell_parse_line("cmd a1 a2 a3 a4 a5 a6 a7 a8 a9 a10 a11 a12 a13 a14 a15 a16",
        lookup_env, env, 0, &prog) == SHELL_PARSE_OVERFLOW);
    assert(shell_parse_line("a | b | c | d | e", lookup_env, env, 0, &prog) == SHELL_PARSE_OVERFLOW);
    assert(shell_parse_line("j1;j2;j3;j4;j5;j6;j7;j8;j9", lookup_env, env, 0, &prog) == SHELL_PARSE_OVERFLOW);

    return 0;
}
