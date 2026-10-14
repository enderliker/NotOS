#include "path_core.h"
#include "syscall_numbers.h"

int path_directory(const char *path) {
    return equal(path, "/") || equal(path, "/tmp")
        || equal(path, "/disk") || equal(path, "/dev") || equal(path, "/system");
}

static int path_append(char *output, u32 *used, const char *input) {
    while (*input) {
        while (*input == '/') input++;
        const char *component = input;
        u32 bytes = 0;
        while (*input && *input != '/') {
            input++;
            bytes++;
        }
        if (!bytes || (bytes == 1 && component[0] == '.')) continue;
        if (bytes == 2 && component[0] == '.' && component[1] == '.') {
            while (*used > 1 && output[*used - 1] != '/') (*used)--;
            if (*used > 1) (*used)--;
            output[*used] = 0;
            continue;
        }
        u32 separator = *used > 1;
        if (bytes >= NOTOS_PATH_BYTES - *used - separator)
            return -ERROR_NAME_TOO_LONG;
        if (separator) output[(*used)++] = '/';
        for (u32 i = 0; i < bytes; i++) output[(*used)++] = component[i];
        output[*used] = 0;
    }
    return 0;
}

int path_resolve(const char *cwd, const char *input, char output[NOTOS_PATH_BYTES]) {
    if (!cwd || !input || !output || cwd[0] != '/')
        return -ERROR_INVALID_ARGUMENT;
    char resolved[NOTOS_PATH_BYTES] = "/";
    u32 used = 1;
    int error = *input == '/' ? 0 : path_append(resolved, &used, cwd);
    if (!error) error = path_append(resolved, &used, input);
    if (error) return error;
    for (u32 i = 0; i <= used; i++) output[i] = resolved[i];
    return 0;
}
