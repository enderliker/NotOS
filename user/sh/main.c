/* User-space command shell (/system/sh.elf) with line editing, pipelines, and redirections. */
#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "../libc/os.h"
#include "../../kernel/line_core.h"
#include "../../kernel/shell_parse_core.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_BG_JOBS 8u
#define PROMPT_TEXT "sh$ "

struct bg_job {
    int active;
    int done;
    int status;
    uint32_t id;
    uint32_t pid_count;
    pid_t pids[SHELL_PARSE_MAX_STAGES];
    char cmd[64];
};

static struct bg_job bg_jobs[MAX_BG_JOBS];
static uint32_t next_job_id = 1u;
static int last_status = 0;
static struct line_editor interactive_editor;

static void write_str(int fd, const char *s) {
    if (!s) return;
    size_t len = strlen(s);
    if (len > 0) (void)write(fd, s, len);
}

static void write_u32(int fd, uint32_t value) {
    char buf[12];
    uint32_t n = 0;
    do {
        buf[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value > 0u);
    char out[12];
    for (uint32_t i = 0; i < n; i++) out[i] = buf[n - 1u - i];
    (void)write(fd, out, n);
}

static void write_err(const char *prefix, const char *detail) {
    write_str(STDERR_FILENO, "sh: ");
    write_str(STDERR_FILENO, prefix);
    if (detail) {
        write_str(STDERR_FILENO, ": ");
        write_str(STDERR_FILENO, detail);
    }
    write_str(STDERR_FILENO, "\n");
}

static int decode_wait_status(int raw_status) {
    if (WIFEXITED(raw_status)) return WEXITSTATUS(raw_status);
    if (WIFSIGNALED(raw_status)) return 128 + WTERMSIG(raw_status);
    return raw_status & 0xff;
}

static const char *cmd_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    return base;
}

/* Builtin fallback applets so pipelines/redirections work standalone before and with box.elf. */
static int applet_echo(int argc, char **argv) {
    int newline = 1;
    int i = 1;
    if (i < argc && strcmp(argv[i], "-n") == 0) {
        newline = 0;
        i++;
    }
    for (; i < argc; i++) {
        if (i > (newline ? 1 : 2)) write_str(STDOUT_FILENO, " ");
        write_str(STDOUT_FILENO, argv[i]);
    }
    if (newline) write_str(STDOUT_FILENO, "\n");
    return 0;
}

static int copy_fd_out(int fd) {
    char buf[512];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) return 1;
        if (n == 0) return 0;
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(STDOUT_FILENO, buf + off, (size_t)(n - off));
            if (w <= 0) return 1;
            off += w;
        }
    }
}

static int applet_cat(int argc, char **argv) {
    if (argc <= 1) return copy_fd_out(STDIN_FILENO);
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            if (copy_fd_out(STDIN_FILENO) != 0) rc = 1;
            continue;
        }
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            write_err("cat", argv[i]);
            rc = 1;
            continue;
        }
        if (copy_fd_out(fd) != 0) rc = 1;
        close(fd);
    }
    return rc;
}

static int applet_env(void) {
    if (!environ) return 0;
    for (char **e = environ; *e; e++) {
        write_str(STDOUT_FILENO, *e);
        write_str(STDOUT_FILENO, "\n");
    }
    return 0;
}

static int applet_sleep(int argc, char **argv) {
    if (argc < 2) return 1;
    uint32_t ticks = 0;
    if (argc >= 3 && strcmp(argv[1], "-m") == 0) {
        uint32_t ms = (uint32_t)strtoul(argv[2], NULL, 10);
        ticks = (ms + 9u) / 10u;
    } else {
        uint32_t val = (uint32_t)strtoul(argv[1], NULL, 10);
        ticks = val <= 10u ? val * 100u : val;
    }
    if (ticks > 0u) (void)notos_raw_call(SYS_SLEEP, ticks, 0, 0);
    return 0;
}

static int applet_wc(int argc, char **argv) {
    int mode_c = 0, mode_l = 0, mode_w = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        for (const char *f = argv[i] + 1; *f; f++) {
            if (*f == 'c') mode_c = 1;
            else if (*f == 'l') mode_l = 1;
            else if (*f == 'w') mode_w = 1;
        }
        i++;
    }
    if (!mode_c && !mode_l && !mode_w) {
        mode_l = 1;
        mode_w = 1;
        mode_c = 1;
    }
    int fd = STDIN_FILENO;
    if (i < argc) {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            write_err("wc", argv[i]);
            return 1;
        }
    }
    uint32_t lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    char buf[512];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        bytes += (uint32_t)n;
        for (ssize_t k = 0; k < n; k++) {
            char c = buf[k];
            if (c == '\n') lines++;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    if (fd != STDIN_FILENO) close(fd);
    int first = 1;
    if (mode_l) {
        write_u32(STDOUT_FILENO, lines);
        first = 0;
    }
    if (mode_w) {
        if (!first) write_str(STDOUT_FILENO, " ");
        write_u32(STDOUT_FILENO, words);
        first = 0;
    }
    if (mode_c) {
        if (!first) write_str(STDOUT_FILENO, " ");
        write_u32(STDOUT_FILENO, bytes);
    }
    write_str(STDOUT_FILENO, "\n");
    return 0;
}

static char to_lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int contains_pattern(const char *line, const char *pat, int ignore_case) {
    size_t plen = strlen(pat);
    if (plen == 0) return 1;
    size_t llen = strlen(line);
    if (llen < plen) return 0;
    for (size_t i = 0; i + plen <= llen; i++) {
        size_t j = 0;
        for (; j < plen; j++) {
            char a = line[i + j];
            char b = pat[j];
            if (ignore_case) {
                a = to_lower_ascii(a);
                b = to_lower_ascii(b);
            }
            if (a != b) break;
        }
        if (j == plen) return 1;
    }
    return 0;
}

static int applet_grep(int argc, char **argv) {
    int invert = 0, line_num = 0, ignore_case = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        for (const char *f = argv[i] + 1; *f; f++) {
            if (*f == 'v') invert = 1;
            else if (*f == 'n') line_num = 1;
            else if (*f == 'i') ignore_case = 1;
        }
        i++;
    }
    if (i >= argc) return 2;
    const char *pattern = argv[i++];
    int fd = STDIN_FILENO;
    if (i < argc) {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            write_err("grep", argv[i]);
            return 2;
        }
    }
    char line[512];
    size_t len = 0;
    uint32_t lineno = 0;
    int matched_any = 0;
    char ch;
    for (;;) {
        ssize_t n = read(fd, &ch, 1);
        if (n <= 0 && len == 0) break;
        if (n > 0 && ch != '\n' && len + 1 < sizeof(line)) {
            if (ch != '\r') line[len++] = ch;
            continue;
        }
        line[len] = '\0';
        lineno++;
        int match = contains_pattern(line, pattern, ignore_case);
        if (invert) match = !match;
        if (match) {
            matched_any = 1;
            if (line_num) {
                write_u32(STDOUT_FILENO, lineno);
                write_str(STDOUT_FILENO, ":");
            }
            write_str(STDOUT_FILENO, line);
            write_str(STDOUT_FILENO, "\n");
        }
        len = 0;
        if (n <= 0) break;
    }
    if (fd != STDIN_FILENO) close(fd);
    return matched_any ? 0 : 1;
}

static int run_embedded_applet(const char *name, int argc, char **argv) {
    if (strcmp(name, "echo") == 0) return applet_echo(argc, argv);
    if (strcmp(name, "cat") == 0) return applet_cat(argc, argv);
    if (strcmp(name, "true") == 0) return 0;
    if (strcmp(name, "false") == 0) return 1;
    if (strcmp(name, "env") == 0) return applet_env();
    if (strcmp(name, "sleep") == 0) return applet_sleep(argc, argv);
    if (strcmp(name, "wc") == 0) return applet_wc(argc, argv);
    if (strcmp(name, "grep") == 0) return applet_grep(argc, argv);
    return -1;
}

static int is_valid_var_name(const char *s, size_t len) {
    if (len == 0) return 0;
    char c0 = s[0];
    if (!((c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z') || c0 == '_')) return 0;
    for (size_t i = 1; i < len; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            return 0;
        }
    }
    return 1;
}

static void record_bg_status(pid_t pid, int raw_status) {
    for (uint32_t i = 0; i < MAX_BG_JOBS; i++) {
        if (!bg_jobs[i].active) continue;
        for (uint32_t k = 0; k < bg_jobs[i].pid_count; k++) {
            if (bg_jobs[i].pids[k] == pid) {
                bg_jobs[i].pids[k] = -1;
                if (k + 1u == bg_jobs[i].pid_count) {
                    bg_jobs[i].status = decode_wait_status(raw_status);
                }
                int all_done = 1;
                for (uint32_t m = 0; m < bg_jobs[i].pid_count; m++) {
                    if (bg_jobs[i].pids[m] > 0) all_done = 0;
                }
                if (all_done) bg_jobs[i].done = 1;
                return;
            }
        }
    }
}

static void reap_bg_jobs(int remove_done) {
    for (;;) {
        int st = 0;
        pid_t pid = waitpid(-1, &st, WNOHANG);
        if (pid <= 0) break;
        record_bg_status(pid, st);
    }
    if (remove_done) {
        for (uint32_t i = 0; i < MAX_BG_JOBS; i++) {
            if (bg_jobs[i].active && bg_jobs[i].done) {
                bg_jobs[i].active = 0;
            }
        }
    }
}

static int is_builtin_name(const char *cmd) {
    return strcmp(cmd, "cd") == 0 || strcmp(cmd, "exit") == 0
        || strcmp(cmd, "export") == 0 || strcmp(cmd, "unset") == 0
        || strcmp(cmd, "jobs") == 0 || strcmp(cmd, "wait") == 0;
}

static int run_builtin(int argc, const char *const *argv, int out_fd) {
    const char *cmd = argv[0];
    if (strcmp(cmd, "cd") == 0) {
        const char *target = (argc > 1) ? argv[1] : getenv("HOME");
        if (!target || !target[0]) target = "/";
        if (chdir(target) != 0) {
            write_err("cd", target);
            return 1;
        }
        char cwd[NOTOS_PATH_BYTES];
        if (getcwd(cwd, sizeof(cwd))) (void)setenv("PWD", cwd, 1);
        return 0;
    }
    if (strcmp(cmd, "exit") == 0) {
        int code = last_status;
        if (argc > 1) code = atoi(argv[1]) & 0xff;
        (void)fcntl(STDIN_FILENO, F_SETTTY, 0);
        _exit(code);
    }
    if (strcmp(cmd, "export") == 0) {
        if (argc == 1) {
            if (environ) {
                for (char **e = environ; *e; e++) {
                    write_str(out_fd, "export ");
                    write_str(out_fd, *e);
                    write_str(out_fd, "\n");
                }
            }
            return 0;
        }
        int rc = 0;
        for (int i = 1; i < argc; i++) {
            const char *eq = strchr(argv[i], '=');
            if (eq) {
                size_t nlen = (size_t)(eq - argv[i]);
                char name[64];
                if (nlen >= sizeof(name) || !is_valid_var_name(argv[i], nlen)) {
                    write_err("export", argv[i]);
                    rc = 1;
                    continue;
                }
                memcpy(name, argv[i], nlen);
                name[nlen] = '\0';
                if (setenv(name, eq + 1, 1) != 0) rc = 1;
            } else {
                size_t nlen = strlen(argv[i]);
                if (!is_valid_var_name(argv[i], nlen)) {
                    write_err("export", argv[i]);
                    rc = 1;
                    continue;
                }
                if (!getenv(argv[i])) {
                    if (setenv(argv[i], "", 1) != 0) rc = 1;
                }
            }
        }
        return rc;
    }
    if (strcmp(cmd, "unset") == 0) {
        int rc = 0;
        for (int i = 1; i < argc; i++) {
            if (!is_valid_var_name(argv[i], strlen(argv[i]))) {
                write_err("unset", argv[i]);
                rc = 1;
                continue;
            }
            (void)unsetenv(argv[i]);
        }
        return rc;
    }
    if (strcmp(cmd, "jobs") == 0) {
        reap_bg_jobs(0);
        for (uint32_t i = 0; i < MAX_BG_JOBS; i++) {
            if (!bg_jobs[i].active) continue;
            write_str(out_fd, "[");
            write_u32(out_fd, bg_jobs[i].id);
            write_str(out_fd, "] ");
            if (bg_jobs[i].done) {
                write_str(out_fd, "Done(");
                write_u32(out_fd, (uint32_t)bg_jobs[i].status);
                write_str(out_fd, ") ");
            } else {
                write_str(out_fd, "Running ");
            }
            write_str(out_fd, bg_jobs[i].cmd);
            write_str(out_fd, "\n");
            if (bg_jobs[i].done) bg_jobs[i].active = 0;
        }
        return 0;
    }
    if (strcmp(cmd, "wait") == 0) {
        int rc = 0;
        if (argc > 1) {
            for (int i = 1; i < argc; i++) {
                pid_t target = (pid_t)atoi(argv[i]);
                if (target <= 0) {
                    rc = 127;
                    continue;
                }
                int st = 0;
                pid_t w = waitpid(target, &st, 0);
                if (w > 0) {
                    record_bg_status(w, st);
                    rc = decode_wait_status(st);
                } else {
                    /* Check if already reaped into bg_jobs. */
                    int found = 0;
                    for (uint32_t j = 0; j < MAX_BG_JOBS; j++) {
                        if (bg_jobs[j].active && bg_jobs[j].done) {
                            rc = bg_jobs[j].status;
                            bg_jobs[j].active = 0;
                            found = 1;
                            break;
                        }
                    }
                    if (!found) rc = 127;
                }
            }
            reap_bg_jobs(1);
            return rc;
        }
        for (;;) {
            int st = 0;
            pid_t w = waitpid(-1, &st, 0);
            if (w <= 0) break;
            record_bg_status(w, st);
            rc = decode_wait_status(st);
        }
        reap_bg_jobs(1);
        return rc;
    }
    return 1;
}

static const char *const box_applets[] = {
    "echo", "cat", "ls", "cp", "mv", "rm", "mkdir", "rmdir", "touch",
    "head", "tail", "wc", "grep", "env", "kill", "ps", "sleep",
    "date", "uptime", "true", "false", "reboot", "poweroff", "ed"
};

static int is_box_applet(const char *cmd) {
    for (size_t i = 0; i < sizeof(box_applets) / sizeof(box_applets[0]); i++) {
        if (strcmp(cmd, box_applets[i]) == 0) return 1;
    }
    return 0;
}

static int is_embedded_applet(const char *cmd) {
    return strcmp(cmd, "echo") == 0 || strcmp(cmd, "cat") == 0
        || strcmp(cmd, "true") == 0 || strcmp(cmd, "false") == 0
        || strcmp(cmd, "env") == 0 || strcmp(cmd, "sleep") == 0
        || strcmp(cmd, "wc") == 0 || strcmp(cmd, "grep") == 0;
}

static int resolve_command(const char *cmd, char *out_path, size_t out_size) {
    struct stat st;
    if (strchr(cmd, '/')) {
        if (strlen(cmd) >= out_size) return -1;
        if (stat(cmd, &st) != 0) return -1;
        strcpy(out_path, cmd);
        return 0;
    }
    char candidate[NOTOS_PATH_BYTES];
    if (strlen(cmd) + 13u < sizeof(candidate)) {
        strcpy(candidate, "/system/");
        strcat(candidate, cmd);
        if (stat(candidate, &st) == 0 && !S_ISDIR(st.st_mode)) {
            strcpy(out_path, candidate);
            return 0;
        }
        strcat(candidate, ".elf");
        if (stat(candidate, &st) == 0 && !S_ISDIR(st.st_mode)) {
            strcpy(out_path, candidate);
            return 0;
        }
    }
    if (is_box_applet(cmd) && stat("/system/box.elf", &st) == 0) {
        strcpy(out_path, "/system/box.elf");
        return 0;
    }
    if (is_embedded_applet(cmd) || is_builtin_name(cmd)) {
        strcpy(out_path, "/system/sh.elf");
        return 0;
    }
    return -1;
}

static int open_stage_redirections(const struct shell_command *stage, int *in_fd, int *out_fd) {
    *in_fd = -1;
    *out_fd = -1;
    if (stage->input_path && stage->input_path[0] != '\0') {
        *in_fd = open(stage->input_path, O_RDONLY | O_CLOEXEC);
        if (*in_fd < 0) {
            write_err("open", stage->input_path);
            return -1;
        }
    }
    if (stage->output_path && stage->output_path[0] != '\0') {
        int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (stage->output_append ? O_APPEND : O_TRUNC);
        *out_fd = open(stage->output_path, flags, 0644);
        if (*out_fd < 0) {
            if (*in_fd >= 0) close(*in_fd);
            *in_fd = -1;
            write_err("open", stage->output_path);
            return -1;
        }
    }
    return 0;
}

static void record_new_bg_job(const struct shell_job *job, const pid_t *pids, uint32_t count) {
    reap_bg_jobs(1);
    for (uint32_t i = 0; i < MAX_BG_JOBS; i++) {
        if (!bg_jobs[i].active) {
            bg_jobs[i].active = 1;
            bg_jobs[i].done = 0;
            bg_jobs[i].status = 0;
            bg_jobs[i].id = next_job_id++;
            bg_jobs[i].pid_count = count;
            for (uint32_t k = 0; k < count; k++) bg_jobs[i].pids[k] = pids[k];
            const char *name = job->stages[0].argv[0] ? job->stages[0].argv[0] : "job";
            strncpy(bg_jobs[i].cmd, name, sizeof(bg_jobs[i].cmd) - 1u);
            bg_jobs[i].cmd[sizeof(bg_jobs[i].cmd) - 1u] = '\0';
            return;
        }
    }
}

static int execute_job(const struct shell_job *job) {
    if (job->stage_count == 0) return 0;

    if (job->stage_count == 1 && !job->background
        && is_builtin_name(job->stages[0].argv[0])) {
        int in_fd = -1, out_fd = -1;
        if (open_stage_redirections(&job->stages[0], &in_fd, &out_fd) < 0) {
            return 1;
        }
        int target_out = (out_fd >= 0) ? out_fd : STDOUT_FILENO;
        int rc = run_builtin((int)job->stages[0].argc, job->stages[0].argv, target_out);
        if (in_fd >= 0) close(in_fd);
        if (out_fd >= 0) close(out_fd);
        return rc;
    }

    pid_t pids[SHELL_PARSE_MAX_STAGES];
    uint32_t spawned = 0;
    int prev_pipe_read = -1;
    int failed_status = 0;

    for (uint32_t s = 0; s < job->stage_count; s++) {
        const struct shell_command *stage = &job->stages[s];
        int next_pipe[2] = {-1, -1};
        if (s + 1u < job->stage_count) {
            if (pipe2(next_pipe, O_CLOEXEC) < 0) {
                write_err("pipe", "failed");
                failed_status = 1;
                break;
            }
        }

        int in_fd = -1, out_fd = -1;
        if (open_stage_redirections(stage, &in_fd, &out_fd) < 0) {
            if (next_pipe[0] >= 0) close(next_pipe[0]);
            if (next_pipe[1] >= 0) close(next_pipe[1]);
            failed_status = 1;
            break;
        }

        char exec_path[NOTOS_PATH_BYTES];
        if (resolve_command(stage->argv[0], exec_path, sizeof(exec_path)) < 0) {
            write_err(stage->argv[0], "command not found");
            if (in_fd >= 0) close(in_fd);
            if (out_fd >= 0) close(out_fd);
            if (next_pipe[0] >= 0) close(next_pipe[0]);
            if (next_pipe[1] >= 0) close(next_pipe[1]);
            failed_status = 127;
            break;
        }

        struct notos_fd_map fd_map[4];
        uint32_t map_count = 0;
        if (in_fd >= 0) {
            fd_map[map_count].child_fd = STDIN_FILENO;
            fd_map[map_count].parent_fd = in_fd;
            map_count++;
        } else if (prev_pipe_read >= 0) {
            fd_map[map_count].child_fd = STDIN_FILENO;
            fd_map[map_count].parent_fd = prev_pipe_read;
            map_count++;
        } else if (job->background) {
            fd_map[map_count].child_fd = STDIN_FILENO;
            fd_map[map_count].parent_fd = NOTOS_FD_MAP_CLOSE;
            map_count++;
        }

        if (out_fd >= 0) {
            fd_map[map_count].child_fd = STDOUT_FILENO;
            fd_map[map_count].parent_fd = out_fd;
            map_count++;
        } else if (next_pipe[1] >= 0) {
            fd_map[map_count].child_fd = STDOUT_FILENO;
            fd_map[map_count].parent_fd = next_pipe[1];
            map_count++;
        }

        char *spawn_argv[SHELL_PARSE_MAX_ARGS + 1];
        for (uint32_t a = 0; a <= stage->argc && a <= SHELL_PARSE_MAX_ARGS; a++) {
            spawn_argv[a] = (char *)stage->argv[a];
        }
        int pid = notos_spawn(exec_path, spawn_argv, environ, fd_map, map_count, 0);

        if (in_fd >= 0) close(in_fd);
        if (out_fd >= 0) close(out_fd);
        if (prev_pipe_read >= 0) close(prev_pipe_read);
        if (next_pipe[1] >= 0) close(next_pipe[1]);
        prev_pipe_read = next_pipe[0];

        if (pid < 0) {
            if (prev_pipe_read >= 0) {
                close(prev_pipe_read);
                prev_pipe_read = -1;
            }
            write_err(stage->argv[0], errno == EAGAIN ? "no free process slots" : "spawn failed");
            failed_status = 1;
            break;
        }
        pids[spawned++] = (pid_t)pid;
    }

    if (prev_pipe_read >= 0) close(prev_pipe_read);

    if (failed_status != 0) {
        for (uint32_t k = 0; k < spawned; k++) {
            (void)kill(pids[k], SIGTERM);
        }
        for (uint32_t k = 0; k < spawned; k++) {
            int st = 0;
            (void)waitpid(pids[k], &st, 0);
        }
        return failed_status;
    }

    if (job->background) {
        record_new_bg_job(job, pids, spawned);
        return 0;
    }

    int final_status = 0;
    for (uint32_t k = 0; k < spawned; k++) {
        int st = 0;
        if (waitpid(pids[k], &st, 0) > 0 && k + 1u == spawned) {
            final_status = decode_wait_status(st);
        }
    }
    return final_status;
}

static const char *lookup_env_var(const char *name, void *ctx) {
    (void)ctx;
    return getenv(name);
}

static u32 next_job_slice(const char *line, u32 start) {
    u32 pos = start;
    while (line[pos]) {
        char c = line[pos++];
        if (c == '\'') {
            while (line[pos] && line[pos] != '\'') pos++;
            if (line[pos] == '\'') pos++;
        } else if (c == '"') {
            while (line[pos] && line[pos] != '"') {
                if (line[pos] == '\\' && line[pos + 1u]) pos += 2u;
                else pos++;
            }
            if (line[pos] == '"') pos++;
        } else if (c == '\\' && line[pos]) {
            pos++;
        } else if (c == ';' || c == '&') {
            break;
        }
    }
    return pos;
}

static int run_command_line(const char *line) {
    static struct shell_program prog;
    int res = shell_parse_line(line, lookup_env_var, NULL, last_status, &prog);
    if (res != SHELL_PARSE_OK) {
        write_err("parse", "syntax error");
        last_status = 2;
        return last_status;
    }
    if (prog.job_count <= 1u) {
        if (prog.job_count == 1u) {
            last_status = execute_job(&prog.jobs[0]);
        }
        return last_status;
    }

    u32 pos = 0;
    while (line[pos]) {
        u32 end = next_job_slice(line, pos);
        u32 len = end - pos;
        char slice[SHELL_PARSE_STORAGE_BYTES];
        if (len >= sizeof(slice)) {
            write_err("parse", "syntax error");
            last_status = 2;
            return last_status;
        }
        memcpy(slice, line + pos, len);
        slice[len] = '\0';
        pos = end;
        if (shell_parse_line(slice, lookup_env_var, NULL, last_status, &prog) != SHELL_PARSE_OK) {
            write_err("parse", "syntax error");
            last_status = 2;
            return last_status;
        }
        if (prog.job_count == 1u) {
            last_status = execute_job(&prog.jobs[0]);
        }
    }
    return last_status;
}

static void move_cursor(int distance) {
    if (!distance) return;
    char seq[16];
    uint32_t mag = distance < 0 ? (uint32_t)(-distance) : (uint32_t)distance;
    char digits[10];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + (mag % 10u));
        mag /= 10u;
    } while (mag > 0u);
    uint32_t pos = 0;
    seq[pos++] = 27;
    seq[pos++] = '[';
    while (count > 0u) seq[pos++] = digits[--count];
    seq[pos++] = distance < 0 ? 'D' : 'C';
    (void)write(STDOUT_FILENO, seq, pos);
}

static void redraw_editor(u32 previous_length, u32 previous_cursor) {
    move_cursor(-(int)previous_cursor);
    write_str(STDOUT_FILENO, interactive_editor.text);
    for (u32 i = interactive_editor.length; i < previous_length; i++) {
        (void)write(STDOUT_FILENO, " ", 1);
    }
    u32 rendered = interactive_editor.length > previous_length ? interactive_editor.length : previous_length;
    move_cursor((int)interactive_editor.cursor - (int)rendered);
}

static int starts_with(const char *text, const char *prefix) {
    while (*prefix) {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

static void complete_interactive(void) {
    static const char *const commands[] = {
        "cd", "exit", "export", "unset", "jobs", "wait",
        "sh", "echo", "cat", "ls", "cp", "mv", "rm", "mkdir", "rmdir",
        "touch", "head", "tail", "wc", "grep", "env", "kill", "ps",
        "sleep", "date", "uptime", "true", "false", "reboot", "poweroff",
        "ed", "curl", "libc"
    };
    u32 start = interactive_editor.cursor;
    while (start > 0u && interactive_editor.text[start - 1u] != ' ') start--;
    char prefix[NOTOS_PATH_BYTES];
    u32 bytes = interactive_editor.cursor - start;
    if (bytes >= sizeof(prefix)) return;
    for (u32 i = 0; i < bytes; i++) prefix[i] = interactive_editor.text[start + i];
    prefix[bytes] = '\0';

    int is_command = (start == 0u);
    for (u32 i = 0; i < bytes; i++) {
        if (prefix[i] == '/') is_command = 0;
    }

    char common[NOTOS_PATH_BYTES] = {0};
    char matches[32][24];
    u32 count = 0;
    u32 common_bytes = 0;
    u32 basename_off = 0;
    for (u32 i = 0; i < bytes; i++) {
        if (prefix[i] == '/') basename_off = i + 1u;
    }

    if (is_command) {
        for (size_t idx = 0; idx < sizeof(commands) / sizeof(commands[0]); idx++) {
            const char *cand = commands[idx];
            if (!starts_with(cand, prefix)) continue;
            if (!count) {
                common_bytes = (u32)strlen(cand);
                strcpy(common, cand);
            } else {
                u32 i = 0;
                while (i < common_bytes && cand[i] && cand[i] == common[i]) i++;
                common_bytes = i;
                common[i] = '\0';
            }
            if (count < 32u) {
                strncpy(matches[count], cand, sizeof(matches[count]) - 1u);
                matches[count][sizeof(matches[count]) - 1u] = '\0';
            }
            count++;
        }
    } else {
        char dir_path[NOTOS_PATH_BYTES];
        if (basename_off == 0u) {
            strcpy(dir_path, ".");
        } else if (basename_off == 1u && prefix[0] == '/') {
            strcpy(dir_path, "/");
        } else {
            for (u32 i = 0; i + 1u < basename_off; i++) dir_path[i] = prefix[i];
            dir_path[basename_off - 1u] = '\0';
        }
        DIR *dir = opendir(dir_path);
        if (!dir) return;
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            const char *cand = de->d_name;
            if (!starts_with(cand, prefix + basename_off)) continue;
            if (!count) {
                common_bytes = (u32)strlen(cand);
                strcpy(common, cand);
            } else {
                u32 i = 0;
                while (i < common_bytes && cand[i] && cand[i] == common[i]) i++;
                common_bytes = i;
                common[i] = '\0';
            }
            if (count < 32u) {
                strncpy(matches[count], cand, sizeof(matches[count]) - 1u);
                matches[count][sizeof(matches[count]) - 1u] = '\0';
            }
            count++;
        }
        closedir(dir);
    }

    if (!count) return;
    u32 prev_len = interactive_editor.length;
    u32 prev_cur = interactive_editor.cursor;
    for (u32 i = bytes - basename_off; i < common_bytes; i++) {
        line_insert(&interactive_editor, common[i]);
    }
    if (count == 1u && interactive_editor.cursor == interactive_editor.length) {
        char full[NOTOS_PATH_BYTES];
        u32 size = interactive_editor.cursor - start;
        if (size < sizeof(full)) {
            for (u32 i = 0; i < size; i++) full[i] = interactive_editor.text[start + i];
            full[size] = '\0';
            struct stat st;
            int is_dir = !is_command && stat(full, &st) == 0 && S_ISDIR(st.st_mode);
            line_insert(&interactive_editor, is_dir ? '/' : ' ');
        }
    }
    if (count > 1u && interactive_editor.cursor == prev_cur) {
        write_str(STDOUT_FILENO, "\n");
        for (u32 i = 0; i < count && i < 32u; i++) {
            write_str(STDOUT_FILENO, matches[i]);
            write_str(STDOUT_FILENO, " ");
        }
        write_str(STDOUT_FILENO, "\n" PROMPT_TEXT);
        write_str(STDOUT_FILENO, interactive_editor.text);
        move_cursor((int)interactive_editor.cursor - (int)interactive_editor.length);
    } else {
        redraw_editor(prev_len, prev_cur);
    }
}

static void handle_editor_key(enum line_key key) {
    u32 prev_len = interactive_editor.length;
    u32 prev_cur = interactive_editor.cursor;
    line_edit(&interactive_editor, key);
    redraw_editor(prev_len, prev_cur);
}

static int read_interactive_line(char *out, size_t out_size, int *overflow_out) {
    *overflow_out = 0;
    reap_bg_jobs(0);
    (void)fcntl(STDIN_FILENO, F_SETTTY, NOTOS_TTY_RAW);
    write_str(STDOUT_FILENO, PROMPT_TEXT);

    static int last_was_cr = 0;
    u32 escape = 0;
    u32 parameter = 0;

    for (;;) {
        char ch = 0;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            (void)fcntl(STDIN_FILENO, F_SETTTY, 0);
            return 0;
        }
        if (n == 0) {
            if (interactive_editor.length == 0u) {
                write_str(STDOUT_FILENO, "\n");
                (void)fcntl(STDIN_FILENO, F_SETTTY, 0);
                return 0;
            }
            continue;
        }

        if (escape) {
            if (escape == 1u) {
                escape = (ch == '[' || ch == 'O') ? 2u : 0u;
                parameter = 0u;
                continue;
            }
            if (ch >= '0' && ch <= '9') {
                parameter = parameter < 100u ? parameter * 10u + (u32)(ch - '0') : 100u;
                continue;
            }
            if (ch == ';') continue;
            escape = 0u;
            if (ch == 'A') handle_editor_key(LINE_UP);
            else if (ch == 'B') handle_editor_key(LINE_DOWN);
            else if (ch == 'C') handle_editor_key(LINE_RIGHT);
            else if (ch == 'D') handle_editor_key(LINE_LEFT);
            else if (ch == 'H' || (ch == '~' && (parameter == 1u || parameter == 7u))) handle_editor_key(LINE_HOME);
            else if (ch == 'F' || (ch == '~' && (parameter == 4u || parameter == 8u))) handle_editor_key(LINE_END);
            else if (ch == '~' && parameter == 3u) handle_editor_key(LINE_DELETE);
            continue;
        }

        if (ch == 27) {
            escape = 1u;
            continue;
        }
        if (ch == '\n' && last_was_cr) {
            last_was_cr = 0;
            continue;
        }
        last_was_cr = (ch == '\r');

        if ((unsigned char)ch == 4u) {
            if (interactive_editor.length == 0u) {
                write_str(STDOUT_FILENO, "\n");
                (void)fcntl(STDIN_FILENO, F_SETTTY, 0);
                return 0;
            }
            handle_editor_key(LINE_DELETE);
            continue;
        }
        if ((unsigned char)ch == 3u) {
            move_cursor((int)interactive_editor.length - (int)interactive_editor.cursor);
            write_str(STDOUT_FILENO, "^C\n" PROMPT_TEXT);
            line_reset(&interactive_editor);
            last_status = 130;
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            move_cursor((int)interactive_editor.length - (int)interactive_editor.cursor);
            write_str(STDOUT_FILENO, "\n");
            (void)fcntl(STDIN_FILENO, F_SETTTY, 0);
            *overflow_out = interactive_editor.overflow;
            strncpy(out, interactive_editor.text, out_size - 1u);
            out[out_size - 1u] = '\0';
            line_submit(&interactive_editor);
            return 1;
        }
        if (ch == '\t') {
            complete_interactive();
            continue;
        }
        if (ch == '\b' || ch == 127) {
            handle_editor_key(LINE_BACKSPACE);
            continue;
        }
        if ((unsigned char)ch >= 32u && (unsigned char)ch <= 126u) {
            u32 prev_len = interactive_editor.length;
            u32 prev_cur = interactive_editor.cursor;
            line_insert(&interactive_editor, ch);
            if (prev_cur == prev_len && interactive_editor.length > prev_len) {
                (void)write(STDOUT_FILENO, &ch, 1);
            } else {
                redraw_editor(prev_len, prev_cur);
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc > 0 && argv[0]) {
        const char *base = cmd_basename(argv[0]);
        if (strcmp(base, "sh") != 0 && strcmp(base, "sh.elf") != 0) {
            int applet_rc = run_embedded_applet(base, argc, argv);
            if (applet_rc >= 0) return applet_rc;
            if (is_builtin_name(base)) return run_builtin(argc, (const char *const *)argv, STDOUT_FILENO);
        }
    }

    if (!getenv("PATH")) (void)setenv("PATH", "/system", 1);
    if (!getenv("PWD")) {
        char cwd[NOTOS_PATH_BYTES];
        if (getcwd(cwd, sizeof(cwd))) (void)setenv("PWD", cwd, 1);
    }

    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        return run_command_line(argv[2]);
    }

    if (argc >= 2 && argv[1][0] != '-') {
        int fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
            write_err("open", argv[1]);
            return 127;
        }
        char line[LINE_CORE_BYTES];
        size_t len = 0;
        char ch;
        for (;;) {
            ssize_t n = read(fd, &ch, 1);
            if (n <= 0 && len == 0) break;
            if (n > 0 && ch != '\n' && len + 1u < sizeof(line)) {
                if (ch != '\r') line[len++] = ch;
                continue;
            }
            line[len] = '\0';
            run_command_line(line);
            len = 0;
            if (n <= 0) break;
        }
        close(fd);
        return last_status;
    }

    line_reset(&interactive_editor);
    char line[LINE_CORE_BYTES];
    int overflow = 0;
    while (read_interactive_line(line, sizeof(line), &overflow)) {
        if (overflow) {
            write_err("input", "line too long");
            last_status = 1;
            continue;
        }
        run_command_line(line);
    }
    return last_status;
}
