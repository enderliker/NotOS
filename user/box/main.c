/* Multi-call userland utility binary (/system/box.elf) and line editor (ed). */
#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "../libc/os.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_POWER
#define SYS_POWER 50
#endif

static const char *base_name(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    return base;
}

static int write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static void write_str(int fd, const char *s) {
    if (s && *s) (void)write_all(fd, s, strlen(s));
}

static void write_u32(int fd, uint32_t v) {
    char rev[12], out[12];
    uint32_t n = 0;
    do {
        rev[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v > 0u);
    for (uint32_t i = 0; i < n; i++) out[i] = rev[n - 1u - i];
    (void)write_all(fd, out, n);
}

static void box_err(const char *applet, const char *detail) {
    write_str(STDERR_FILENO, applet);
    write_str(STDERR_FILENO, ": ");
    write_str(STDERR_FILENO, detail ? detail : "error");
    write_str(STDERR_FILENO, "\n");
}

static int cmd_echo(int argc, char **argv) {
    int newline = 1;
    int i = 1;
    if (i < argc && strcmp(argv[i], "-n") == 0) {
        newline = 0;
        i++;
    }
    for (int first = i; i < argc; i++) {
        if (i > first) write_str(STDOUT_FILENO, " ");
        write_str(STDOUT_FILENO, argv[i]);
    }
    if (newline) write_str(STDOUT_FILENO, "\n");
    return 0;
}

static int copy_fd(int src_fd, int dst_fd) {
    char buf[1024];
    for (;;) {
        ssize_t n = read(src_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return 0;
        if (write_all(dst_fd, buf, (size_t)n) < 0) return -1;
    }
}

static int cmd_cat(int argc, char **argv) {
    if (argc <= 1) return copy_fd(STDIN_FILENO, STDOUT_FILENO) < 0 ? 1 : 0;
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            if (copy_fd(STDIN_FILENO, STDOUT_FILENO) < 0) rc = 1;
            continue;
        }
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            box_err("cat", argv[i]);
            rc = 1;
            continue;
        }
        if (copy_fd(fd, STDOUT_FILENO) < 0) rc = 1;
        close(fd);
    }
    return rc;
}

struct ls_item {
    char name[32];
    uint32_t size;
    int is_dir;
};

static int cmp_ls_item(const void *a, const void *b) {
    const struct ls_item *ia = (const struct ls_item *)a;
    const struct ls_item *ib = (const struct ls_item *)b;
    return strcmp(ia->name, ib->name);
}

static void print_ls_entry(const char *name, int is_dir, uint32_t size, int long_fmt) {
    if (long_fmt) {
        write_str(STDOUT_FILENO, is_dir ? "d " : "- ");
        write_u32(STDOUT_FILENO, size);
        write_str(STDOUT_FILENO, " ");
    }
    write_str(STDOUT_FILENO, name);
    if (is_dir) write_str(STDOUT_FILENO, "/");
    write_str(STDOUT_FILENO, "\n");
}

static int list_single_path(const char *path, int long_fmt) {
    struct stat st;
    if (stat(path, &st) != 0) {
        box_err("ls", path);
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        print_ls_entry(path, 0, (uint32_t)st.st_size, long_fmt);
        return 0;
    }
    DIR *dir = opendir(path);
    if (!dir) {
        box_err("ls", path);
        return 1;
    }
    struct ls_item items[64];
    size_t count = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && count < 64u) {
        strncpy(items[count].name, de->d_name, sizeof(items[count].name) - 1u);
        items[count].name[sizeof(items[count].name) - 1u] = '\0';
        items[count].is_dir = (de->d_type == DT_DIR);
        items[count].size = 0;
        char child[NOTOS_PATH_BYTES];
        size_t plen = strlen(path);
        size_t nlen = strlen(de->d_name);
        if (plen + 1u + nlen < sizeof(child)) {
            strcpy(child, path);
            if (plen > 0 && path[plen - 1u] != '/') strcat(child, "/");
            strcat(child, de->d_name);
            struct stat cst;
            if (stat(child, &cst) == 0) {
                items[count].is_dir = S_ISDIR(cst.st_mode);
                items[count].size = (uint32_t)cst.st_size;
            }
        }
        count++;
    }
    closedir(dir);
    if (count > 1u) qsort(items, count, sizeof(items[0]), cmp_ls_item);
    for (size_t i = 0; i < count; i++) {
        print_ls_entry(items[i].name, items[i].is_dir, items[i].size, long_fmt);
    }
    return 0;
}

static int cmd_ls(int argc, char **argv) {
    int long_fmt = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        for (const char *f = argv[i] + 1; *f; f++) {
            if (*f == 'l') long_fmt = 1;
            else if (*f == 'a' || *f == '1') { /* accepted for compatibility */ }
        }
        i++;
    }
    if (i >= argc) return list_single_path(".", long_fmt);
    int rc = 0;
    for (; i < argc; i++) {
        if (list_single_path(argv[i], long_fmt) != 0) rc = 1;
    }
    return rc;
}

static int resolve_dest_path(const char *src, const char *dst, char *out, size_t out_size) {
    struct stat st;
    if (stat(dst, &st) == 0 && S_ISDIR(st.st_mode)) {
        const char *b = base_name(src);
        size_t dlen = strlen(dst);
        size_t blen = strlen(b);
        int need_slash = (dlen > 0 && dst[dlen - 1u] != '/');
        if (dlen + (size_t)need_slash + blen >= out_size) return -1;
        strcpy(out, dst);
        if (need_slash) strcat(out, "/");
        strcat(out, b);
        return 0;
    }
    if (strlen(dst) >= out_size) return -1;
    strcpy(out, dst);
    return 0;
}

static int copy_file_bytes(const char *src, const char *dst) {
    struct stat st_src, st_dst;
    if (stat(src, &st_src) != 0 || S_ISDIR(st_src.st_mode)) return -1;
    if (stat(dst, &st_dst) == 0 && st_src.st_ino == st_dst.st_ino
        && strcmp(src, dst) == 0) {
        return 0;
    }
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) return -1;
    int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }
    int rc = copy_fd(in_fd, out_fd);
    if (close(out_fd) < 0) rc = -1;
    close(in_fd);
    return rc;
}

static int cmd_cp(int argc, char **argv) {
    int i = 1;
    if (i < argc && strcmp(argv[i], "-f") == 0) i++;
    if (argc - i < 2) {
        box_err("cp", "missing operand");
        return 1;
    }
    const char *dst = argv[argc - 1];
    int rc = 0;
    for (; i < argc - 1; i++) {
        char target[NOTOS_PATH_BYTES];
        if (resolve_dest_path(argv[i], dst, target, sizeof(target)) < 0
            || copy_file_bytes(argv[i], target) < 0) {
            box_err("cp", argv[i]);
            rc = 1;
        }
    }
    return rc;
}

static int cmd_mv(int argc, char **argv) {
    int i = 1;
    if (i < argc && strcmp(argv[i], "-f") == 0) i++;
    if (argc - i < 2) {
        box_err("mv", "missing operand");
        return 1;
    }
    const char *dst = argv[argc - 1];
    int rc = 0;
    for (; i < argc - 1; i++) {
        char target[NOTOS_PATH_BYTES];
        if (resolve_dest_path(argv[i], dst, target, sizeof(target)) < 0) {
            box_err("mv", argv[i]);
            rc = 1;
            continue;
        }
        if (rename(argv[i], target) == 0) continue;
        if (errno == EXDEV) {
            if (copy_file_bytes(argv[i], target) == 0 && unlink(argv[i]) == 0) {
                continue;
            }
        }
        box_err("mv", argv[i]);
        rc = 1;
    }
    return rc;
}

static int cmd_rm(int argc, char **argv) {
    int force = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        for (const char *f = argv[i] + 1; *f; f++) {
            if (*f == 'f') force = 1;
        }
        i++;
    }
    if (i >= argc) return force ? 0 : 1;
    int rc = 0;
    for (; i < argc; i++) {
        if (unlink(argv[i]) != 0) {
            if (force && errno == ENOENT) continue;
            box_err("rm", argv[i]);
            rc = 1;
        }
    }
    return rc;
}

static int mkdir_parents(const char *path) {
    char tmp[NOTOS_PATH_BYTES];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    strcpy(tmp, path);
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            tmp[i] = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int cmd_mkdir(int argc, char **argv) {
    int parents = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        for (const char *f = argv[i] + 1; *f; f++) {
            if (*f == 'p') parents = 1;
        }
        i++;
    }
    if (i >= argc) {
        box_err("mkdir", "missing operand");
        return 1;
    }
    int rc = 0;
    for (; i < argc; i++) {
        int err = parents ? mkdir_parents(argv[i]) : mkdir(argv[i], 0755);
        if (err != 0) {
            box_err("mkdir", argv[i]);
            rc = 1;
        }
    }
    return rc;
}

static int cmd_rmdir(int argc, char **argv) {
    if (argc < 2) {
        box_err("rmdir", "missing operand");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) != 0) {
            box_err("rmdir", argv[i]);
            rc = 1;
        }
    }
    return rc;
}

static int cmd_touch(int argc, char **argv) {
    if (argc < 2) {
        box_err("touch", "missing operand");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            box_err("touch", argv[i]);
            rc = 1;
            continue;
        }
        struct stat st;
        if (fstat(fd, &st) != 0 || ftruncate(fd, st.st_size) != 0) {
            box_err("touch", argv[i]);
            rc = 1;
        }
        close(fd);
    }
    return rc;
}

static int parse_line_count_args(int argc, char **argv, uint32_t *lines_out, int *arg_idx) {
    *lines_out = 10u;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) return -1;
            *lines_out = (uint32_t)strtoul(argv[i + 1], NULL, 10);
            i += 2;
        } else if (argv[i][1] >= '0' && argv[i][1] <= '9') {
            *lines_out = (uint32_t)strtoul(argv[i] + 1, NULL, 10);
            i++;
        } else if (strncmp(argv[i], "-n", 2) == 0) {
            *lines_out = (uint32_t)strtoul(argv[i] + 2, NULL, 10);
            i++;
        } else {
            return -1;
        }
    }
    *arg_idx = i;
    return 0;
}

static int cmd_head(int argc, char **argv) {
    uint32_t limit = 10u;
    int idx = 1;
    if (parse_line_count_args(argc, argv, &limit, &idx) < 0) return 1;
    int fd = STDIN_FILENO;
    if (idx < argc && strcmp(argv[idx], "-") != 0) {
        fd = open(argv[idx], O_RDONLY);
        if (fd < 0) {
            box_err("head", argv[idx]);
            return 1;
        }
    }
    uint32_t seen = 0;
    char buf[256];
    while (seen < limit) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        ssize_t out_len = 0;
        while (out_len < n && seen < limit) {
            if (buf[out_len++] == '\n') seen++;
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)out_len) < 0) break;
    }
    if (fd != STDIN_FILENO) close(fd);
    return 0;
}

#define TAIL_BUF_MAX 16384u

static int cmd_tail(int argc, char **argv) {
    uint32_t limit = 10u;
    int idx = 1;
    if (parse_line_count_args(argc, argv, &limit, &idx) < 0) return 1;
    int fd = STDIN_FILENO;
    if (idx < argc && strcmp(argv[idx], "-") != 0) {
        fd = open(argv[idx], O_RDONLY);
        if (fd < 0) {
            box_err("tail", argv[idx]);
            return 1;
        }
    }
    if (limit == 0u) {
        if (fd != STDIN_FILENO) close(fd);
        return 0;
    }
    char *ring = (char *)malloc(TAIL_BUF_MAX);
    if (!ring) {
        if (fd != STDIN_FILENO) close(fd);
        return 1;
    }
    size_t used = 0;
    char chunk[512];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n <= 0) break;
        size_t cn = (size_t)n;
        if (cn >= TAIL_BUF_MAX) {
            memcpy(ring, chunk + (cn - TAIL_BUF_MAX), TAIL_BUF_MAX);
            used = TAIL_BUF_MAX;
        } else if (used + cn > TAIL_BUF_MAX) {
            size_t drop = (used + cn) - TAIL_BUF_MAX;
            memmove(ring, ring + drop, used - drop);
            memcpy(ring + (used - drop), chunk, cn);
            used = TAIL_BUF_MAX;
        } else {
            memcpy(ring + used, chunk, cn);
            used += cn;
        }
    }
    if (fd != STDIN_FILENO) close(fd);

    uint32_t newlines = 0;
    size_t pos = used;
    if (pos > 0 && ring[pos - 1u] == '\n') pos--;
    while (pos > 0) {
        if (ring[pos - 1u] == '\n') {
            if (++newlines == limit) break;
        }
        pos--;
    }
    (void)write_all(STDOUT_FILENO, ring + pos, used - pos);
    free(ring);
    return 0;
}

static int cmd_wc(int argc, char **argv) {
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
    if (i < argc && strcmp(argv[i], "-") != 0) {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            box_err("wc", argv[i]);
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

static char lower_ascii(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int match_substring(const char *line, const char *pat, int ignore_case) {
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
                a = lower_ascii(a);
                b = lower_ascii(b);
            }
            if (a != b) break;
        }
        if (j == plen) return 1;
    }
    return 0;
}

static int cmd_grep(int argc, char **argv) {
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
    if (i < argc && strcmp(argv[i], "-") != 0) {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            box_err("grep", argv[i]);
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
        if (n > 0 && ch != '\n' && len + 1u < sizeof(line)) {
            if (ch != '\r') line[len++] = ch;
            continue;
        }
        line[len] = '\0';
        lineno++;
        int match = match_substring(line, pattern, ignore_case);
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

static int cmd_env(int argc, char **argv) {
    int i = 1;
    while (i < argc && strchr(argv[i], '=') != NULL) {
        const char *eq = strchr(argv[i], '=');
        char key[64];
        size_t klen = (size_t)(eq - argv[i]);
        if (klen > 0 && klen < sizeof(key)) {
            memcpy(key, argv[i], klen);
            key[klen] = '\0';
            (void)setenv(key, eq + 1, 1);
        }
        i++;
    }
    if (i < argc) {
        char path[NOTOS_PATH_BYTES];
        if (strchr(argv[i], '/')) {
            strncpy(path, argv[i], sizeof(path) - 1u);
            path[sizeof(path) - 1u] = '\0';
        } else {
            strcpy(path, "/system/box.elf");
        }
        int pid = notos_spawn(path, &argv[i], environ, NULL, 0, 0);
        if (pid < 0) return 127;
        int st = 0;
        if (waitpid(pid, &st, 0) < 0) return 1;
        return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    }
    if (!environ) return 0;
    for (char **e = environ; *e; e++) {
        write_str(STDOUT_FILENO, *e);
        write_str(STDOUT_FILENO, "\n");
    }
    return 0;
}

static int parse_signal_spec(const char *s) {
    if (strncmp(s, "SIG", 3) == 0) s += 3;
    if (strcmp(s, "HUP") == 0) return SIGHUP;
    if (strcmp(s, "INT") == 0) return SIGINT;
    if (strcmp(s, "QUIT") == 0) return SIGQUIT;
    if (strcmp(s, "KILL") == 0) return SIGKILL;
    if (strcmp(s, "TERM") == 0) return SIGTERM;
    if (strcmp(s, "USR1") == 0) return SIGUSR1;
    if (strcmp(s, "USR2") == 0) return SIGUSR2;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end && *end == '\0' && v >= 0 && v <= 31) return (int)v;
    return -1;
}

static int cmd_kill(int argc, char **argv) {
    int sig = SIGTERM;
    int i = 1;
    if (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) return 1;
            sig = parse_signal_spec(argv[i + 1]);
            i += 2;
        } else {
            sig = parse_signal_spec(argv[i] + 1);
            i++;
        }
        if (sig < 0) {
            box_err("kill", "invalid signal");
            return 1;
        }
    }
    if (i >= argc) {
        box_err("kill", "missing pid");
        return 1;
    }
    int rc = 0;
    for (; i < argc; i++) {
        pid_t pid = (pid_t)strtol(argv[i], NULL, 10);
        if (pid <= 0 || kill(pid, sig) != 0) {
            box_err("kill", argv[i]);
            rc = 1;
        }
    }
    return rc;
}

static int cmd_ps(void) {
    struct notos_process_info entries[16];
    int count = notos_process_list(entries, 16u);
    if (count < 0) {
        box_err("ps", "failed");
        return 1;
    }
    write_str(STDOUT_FILENO, "PID PPID STATE TICKS NAME\n");
    for (int i = 0; i < count; i++) {
        const char *state = "R";
        if (entries[i].state == NOTOS_PROCESS_STATE_BLOCKED) state = "S";
        else if (entries[i].state == NOTOS_PROCESS_STATE_ZOMBIE) state = "Z";
        write_u32(STDOUT_FILENO, entries[i].pid);
        write_str(STDOUT_FILENO, " ");
        write_u32(STDOUT_FILENO, entries[i].ppid);
        write_str(STDOUT_FILENO, " ");
        write_str(STDOUT_FILENO, state);
        write_str(STDOUT_FILENO, " ");
        write_u32(STDOUT_FILENO, entries[i].cpu_ticks);
        write_str(STDOUT_FILENO, " ");
        write_str(STDOUT_FILENO, entries[i].name[0] ? entries[i].name : "?");
        write_str(STDOUT_FILENO, "\n");
    }
    return 0;
}

static int cmd_sleep(int argc, char **argv) {
    if (argc < 2) return 1;
    uint32_t ms = 0;
    if (argc >= 3 && strcmp(argv[1], "-m") == 0) {
        ms = (uint32_t)strtoul(argv[2], NULL, 10);
    } else {
        uint32_t val = (uint32_t)strtoul(argv[1], NULL, 10);
        ms = val <= 60u ? val * 1000u : val;
    }
    while (ms > 0u) {
        uint32_t step = ms > 60000u ? 60000u : ms;
        if (notos_raw_call(SYS_SLEEP, step, 0, 0) < 0) return 1;
        ms -= step;
    }
    return 0;
}

static void write_2digit(uint32_t v) {
    char b[2];
    b[0] = (char)('0' + ((v / 10u) % 10u));
    b[1] = (char)('0' + (v % 10u));
    (void)write_all(STDOUT_FILENO, b, 2);
}

static int cmd_date(int argc, char **argv) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        box_err("date", "clock unavailable");
        return 1;
    }
    if (argc >= 2 && strcmp(argv[1], "+%s") == 0) {
        write_u32(STDOUT_FILENO, (uint32_t)ts.tv_sec);
        write_str(STDOUT_FILENO, "\n");
        return 0;
    }
    struct tm utc;
    if (!gmtime_r(&ts.tv_sec, &utc)) return 1;
    write_u32(STDOUT_FILENO, (uint32_t)(utc.tm_year + 1900));
    write_str(STDOUT_FILENO, "-");
    write_2digit((uint32_t)(utc.tm_mon + 1));
    write_str(STDOUT_FILENO, "-");
    write_2digit((uint32_t)utc.tm_mday);
    write_str(STDOUT_FILENO, " ");
    write_2digit((uint32_t)utc.tm_hour);
    write_str(STDOUT_FILENO, ":");
    write_2digit((uint32_t)utc.tm_min);
    write_str(STDOUT_FILENO, ":");
    write_2digit((uint32_t)utc.tm_sec);
    write_str(STDOUT_FILENO, " UTC\n");
    return 0;
}

static int cmd_uptime(void) {
    uint32_t ms = (uint32_t)notos_raw_call(SYS_TICKS, 0, 0, 0);
    uint32_t secs = ms / 1000u;
    uint32_t rem = ms % 1000u;
    write_str(STDOUT_FILENO, "up ");
    write_u32(STDOUT_FILENO, secs);
    write_str(STDOUT_FILENO, ".");
    char frac[3];
    frac[0] = (char)('0' + ((rem / 100u) % 10u));
    frac[1] = (char)('0' + ((rem / 10u) % 10u));
    frac[2] = (char)('0' + (rem % 10u));
    (void)write_all(STDOUT_FILENO, frac, 3);
    write_str(STDOUT_FILENO, "s (");
    write_u32(STDOUT_FILENO, ms);
    write_str(STDOUT_FILENO, " ms)\n");
    return 0;
}

static int cmd_power(int argc, char **argv, uint32_t action, const char *name) {
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        return 0;
    }
    int r = notos_raw_call(SYS_POWER, action, 0, 0);
    if (r < 0) {
        box_err(name, "power control failed");
        return 1;
    }
    return 0;
}

/* --- Line editor (ed) --- */
#define ED_MAX_LINES 512u
#define ED_LINE_MAX  256u

struct ed_state {
    char *lines[ED_MAX_LINES];
    uint32_t count;
    uint32_t cur;
    char filename[NOTOS_PATH_BYTES];
};

static void ed_free_all(struct ed_state *ed) {
    for (uint32_t i = 0; i < ed->count; i++) {
        free(ed->lines[i]);
        ed->lines[i] = NULL;
    }
    ed->count = 0;
    ed->cur = 0;
}

static int ed_read_line(int fd, char *out, size_t max_len) {
    size_t len = 0;
    char ch;
    for (;;) {
        ssize_t n = read(fd, &ch, 1);
        if (n <= 0) {
            if (len == 0) return 0;
            break;
        }
        if (ch == '\r') continue;
        if (ch == '\n') break;
        if (len + 1u < max_len) out[len++] = ch;
    }
    out[len] = '\0';
    return 1;
}

static int ed_insert_at(struct ed_state *ed, uint32_t index, const char *text) {
    if (ed->count >= ED_MAX_LINES || index > ed->count) return -1;
    char *dup = strdup(text);
    if (!dup) return -1;
    for (uint32_t i = ed->count; i > index; i--) {
        ed->lines[i] = ed->lines[i - 1u];
    }
    ed->lines[index] = dup;
    ed->count++;
    ed->cur = index + 1u;
    return 0;
}

static void ed_delete_range(struct ed_state *ed, uint32_t a1, uint32_t a2) {
    if (a1 < 1u || a2 < a1 || a2 > ed->count) return;
    uint32_t n = a2 - a1 + 1u;
    for (uint32_t i = a1 - 1u; i < a2; i++) {
        free(ed->lines[i]);
    }
    for (uint32_t i = a2; i < ed->count; i++) {
        ed->lines[i - n] = ed->lines[i];
    }
    ed->count -= n;
    if (ed->count == 0u) ed->cur = 0u;
    else if (a1 <= ed->count) ed->cur = a1;
    else ed->cur = ed->count;
}

static int ed_load_file(struct ed_state *ed, const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ed_free_all(ed);
    uint32_t bytes = 0;
    char line[ED_LINE_MAX];
    size_t len = 0;
    char ch;
    for (;;) {
        ssize_t n = read(fd, &ch, 1);
        if (n <= 0 && len == 0) break;
        if (n > 0) bytes++;
        if (n > 0 && ch != '\n' && len + 1u < sizeof(line)) {
            if (ch != '\r') line[len++] = ch;
            continue;
        }
        line[len] = '\0';
        if (ed_insert_at(ed, ed->count, line) < 0) {
            close(fd);
            return -1;
        }
        len = 0;
        if (n <= 0) break;
    }
    close(fd);
    write_u32(STDOUT_FILENO, bytes);
    write_str(STDOUT_FILENO, "\n");
    return 0;
}

static int ed_write_file(const struct ed_state *ed, const char *path, uint32_t a1, uint32_t a2) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    uint32_t bytes = 0;
    if (ed->count > 0u && a1 >= 1u && a2 >= a1 && a2 <= ed->count) {
        for (uint32_t i = a1 - 1u; i < a2; i++) {
            size_t len = strlen(ed->lines[i]);
            if (write_all(fd, ed->lines[i], len) < 0 || write_all(fd, "\n", 1) < 0) {
                close(fd);
                return -1;
            }
            bytes += (uint32_t)len + 1u;
        }
    }
    if (close(fd) < 0) return -1;
    write_u32(STDOUT_FILENO, bytes);
    write_str(STDOUT_FILENO, "\n");
    return 0;
}

static int parse_single_addr(const struct ed_state *ed, const char **p, uint32_t *addr_out) {
    const char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    uint32_t base = 0;
    if (*s == '.') {
        base = ed->cur;
        s++;
    } else if (*s == '$') {
        base = ed->count;
        s++;
    } else if (*s >= '0' && *s <= '9') {
        uint32_t v = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10u + (uint32_t)(*s - '0');
            s++;
        }
        base = v;
    } else if (*s == '+' || *s == '-') {
        base = ed->cur;
    } else {
        return 0;
    }
    while (*s == '+' || *s == '-') {
        int sign = (*s == '+') ? 1 : -1;
        s++;
        uint32_t delta = 1u;
        if (*s >= '0' && *s <= '9') {
            delta = 0u;
            while (*s >= '0' && *s <= '9') {
                delta = delta * 10u + (uint32_t)(*s - '0');
                s++;
            }
        }
        if (sign > 0) base += delta;
        else if (delta <= base) base -= delta;
        else return -1;
    }
    *p = s;
    *addr_out = base;
    return 1;
}

static int ed_input_mode(struct ed_state *ed, uint32_t insert_pos) {
    char line[ED_LINE_MAX];
    while (ed_read_line(STDIN_FILENO, line, sizeof(line))) {
        if (strcmp(line, ".") == 0) return 0;
        if (ed_insert_at(ed, insert_pos, line) < 0) return -1;
        insert_pos++;
    }
    return 0;
}

static int cmd_ed(int argc, char **argv) {
    struct ed_state ed;
    memset(&ed, 0, sizeof(ed));
    if (argc >= 2) {
        strncpy(ed.filename, argv[1], sizeof(ed.filename) - 1u);
        ed.filename[sizeof(ed.filename) - 1u] = '\0';
        struct stat st;
        if (stat(ed.filename, &st) == 0) {
            if (ed_load_file(&ed, ed.filename) < 0) {
                write_str(STDOUT_FILENO, "?\n");
            }
        }
    }

    char cmdline[ED_LINE_MAX];
    while (ed_read_line(STDIN_FILENO, cmdline, sizeof(cmdline))) {
        const char *p = cmdline;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;

        uint32_t a1 = 0, a2 = 0;
        int n_addrs = 0;
        if (*p == ',' || *p == '%') {
            a1 = ed.count > 0u ? 1u : 0u;
            a2 = ed.count;
            n_addrs = 2;
            p++;
        } else if (*p == ';') {
            a1 = ed.cur;
            a2 = ed.count;
            n_addrs = 2;
            p++;
        } else {
            int r1 = parse_single_addr(&ed, &p, &a1);
            if (r1 < 0) {
                write_str(STDOUT_FILENO, "?\n");
                continue;
            }
            if (r1 == 1) {
                a2 = a1;
                n_addrs = 1;
                while (*p == ' ' || *p == '\t') p++;
                if (*p == ',') {
                    p++;
                    int r2 = parse_single_addr(&ed, &p, &a2);
                    if (r2 <= 0) {
                        write_str(STDOUT_FILENO, "?\n");
                        continue;
                    }
                    n_addrs = 2;
                }
            }
        }

        while (*p == ' ' || *p == '\t') p++;
        char op = *p ? *p++ : '\0';

        if (op == 'q' || op == 'Q') {
            break;
        } else if (op == 'a') {
            uint32_t target = (n_addrs >= 1) ? a1 : ed.cur;
            if (target > ed.count) {
                write_str(STDOUT_FILENO, "?\n");
                continue;
            }
            if (ed_input_mode(&ed, target) < 0) write_str(STDOUT_FILENO, "?\n");
        } else if (op == 'i') {
            uint32_t target = (n_addrs >= 1) ? a1 : ed.cur;
            if (target > ed.count || (target == 0u && ed.count > 0u)) {
                if (target == 1u && ed.count == 0u) target = 0u;
                else if (target > ed.count) {
                    write_str(STDOUT_FILENO, "?\n");
                    continue;
                }
            }
            uint32_t pos = target > 0u ? target - 1u : 0u;
            if (ed_input_mode(&ed, pos) < 0) write_str(STDOUT_FILENO, "?\n");
        } else if (op == 'c') {
            if (n_addrs == 0) {
                a1 = ed.cur;
                a2 = ed.cur;
            }
            if (a1 < 1u || a2 < a1 || a2 > ed.count) {
                write_str(STDOUT_FILENO, "?\n");
                continue;
            }
            uint32_t pos = a1 - 1u;
            ed_delete_range(&ed, a1, a2);
            if (ed_input_mode(&ed, pos) < 0) write_str(STDOUT_FILENO, "?\n");
        } else if (op == 'd') {
            if (n_addrs == 0) {
                a1 = ed.cur;
                a2 = ed.cur;
            }
            if (a1 < 1u || a2 < a1 || a2 > ed.count) {
                write_str(STDOUT_FILENO, "?\n");
                continue;
            }
            ed_delete_range(&ed, a1, a2);
        } else if (op == 'p' || op == 'n' || (op == '\0' && n_addrs >= 1)) {
            if (n_addrs == 0) {
                a1 = ed.cur;
                a2 = ed.cur;
            }
            if (a1 < 1u || a2 < a1 || a2 > ed.count) {
                write_str(STDOUT_FILENO, "?\n");
                continue;
            }
            for (uint32_t i = a1; i <= a2; i++) {
                if (op == 'n') {
                    write_u32(STDOUT_FILENO, i);
                    write_str(STDOUT_FILENO, "\t");
                }
                write_str(STDOUT_FILENO, ed.lines[i - 1u]);
                write_str(STDOUT_FILENO, "\n");
            }
            ed.cur = a2;
        } else if (op == '=') {
            uint32_t target = (n_addrs >= 1) ? a1 : ed.count;
            if (target > ed.count) {
                write_str(STDOUT_FILENO, "?\n");
                continue;
            }
            write_u32(STDOUT_FILENO, target);
            write_str(STDOUT_FILENO, "\n");
        } else if (op == 'w') {
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '\0') {
                strncpy(ed.filename, p, sizeof(ed.filename) - 1u);
                ed.filename[sizeof(ed.filename) - 1u] = '\0';
            }
            if (ed.filename[0] == '\0') {
                write_str(STDOUT_FILENO, "?\n");
                continue;
            }
            if (n_addrs == 0) {
                a1 = ed.count > 0u ? 1u : 0u;
                a2 = ed.count;
            }
            if (ed_write_file(&ed, ed.filename, a1, a2) < 0) {
                write_str(STDOUT_FILENO, "?\n");
            }
        } else if (op == 'e') {
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '\0') {
                strncpy(ed.filename, p, sizeof(ed.filename) - 1u);
                ed.filename[sizeof(ed.filename) - 1u] = '\0';
            }
            if (ed.filename[0] == '\0' || ed_load_file(&ed, ed.filename) < 0) {
                write_str(STDOUT_FILENO, "?\n");
            }
        } else {
            write_str(STDOUT_FILENO, "?\n");
        }
    }

    ed_free_all(&ed);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 1 || !argv || !argv[0]) return 1;
    const char *name = base_name(argv[0]);
    if (strcmp(name, "box") == 0 || strcmp(name, "box.elf") == 0) {
        if (argc < 2) {
            box_err("box", "usage: box APPLET [ARGS...]");
            return 1;
        }
        argc--;
        argv++;
        name = base_name(argv[0]);
    }

    if (strcmp(name, "echo") == 0) return cmd_echo(argc, argv);
    if (strcmp(name, "cat") == 0) return cmd_cat(argc, argv);
    if (strcmp(name, "ls") == 0) return cmd_ls(argc, argv);
    if (strcmp(name, "cp") == 0) return cmd_cp(argc, argv);
    if (strcmp(name, "mv") == 0) return cmd_mv(argc, argv);
    if (strcmp(name, "rm") == 0) return cmd_rm(argc, argv);
    if (strcmp(name, "mkdir") == 0) return cmd_mkdir(argc, argv);
    if (strcmp(name, "rmdir") == 0) return cmd_rmdir(argc, argv);
    if (strcmp(name, "touch") == 0) return cmd_touch(argc, argv);
    if (strcmp(name, "head") == 0) return cmd_head(argc, argv);
    if (strcmp(name, "tail") == 0) return cmd_tail(argc, argv);
    if (strcmp(name, "wc") == 0) return cmd_wc(argc, argv);
    if (strcmp(name, "grep") == 0) return cmd_grep(argc, argv);
    if (strcmp(name, "env") == 0) return cmd_env(argc, argv);
    if (strcmp(name, "kill") == 0) return cmd_kill(argc, argv);
    if (strcmp(name, "ps") == 0) return cmd_ps();
    if (strcmp(name, "sleep") == 0) return cmd_sleep(argc, argv);
    if (strcmp(name, "date") == 0) return cmd_date(argc, argv);
    if (strcmp(name, "uptime") == 0) return cmd_uptime();
    if (strcmp(name, "true") == 0) return 0;
    if (strcmp(name, "false") == 0) return 1;
    if (strcmp(name, "reboot") == 0) return cmd_power(argc, argv, NOTOS_POWER_REBOOT, "reboot");
    if (strcmp(name, "poweroff") == 0) return cmd_power(argc, argv, NOTOS_POWER_POWEROFF, "poweroff");
    if (strcmp(name, "ed") == 0) return cmd_ed(argc, argv);

    box_err("box", name);
    return 127;
}
