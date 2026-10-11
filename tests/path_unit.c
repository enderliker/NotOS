#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/path_core.h"
#include "../kernel/syscall_numbers.h"

static void check(const char *cwd, const char *input, const char *expected) {
    char result[NOTOS_PATH_BYTES];
    assert(path_resolve(cwd, input, result) == 0);
    assert(strcmp(result, expected) == 0);
}

int main(void) {
    check("/tmp", "page.txt", "/tmp/page.txt");
    check("/disk", "../tmp/./page.txt", "/tmp/page.txt");
    check("/dev", "../../../../", "/");
    check("/tmp", "/disk//hello/../file", "/disk/file");
    check("/", "", "/");
    check("/tmp", ".", "/tmp");
    char input[65], result[NOTOS_PATH_BYTES] = "unchanged";
    memset(input, 'x', sizeof(input));
    input[62] = 0;
    assert(path_resolve("/", input, result) == 0 && strlen(result) == 63);
    input[62] = 'x';
    input[63] = 0;
    strcpy(result, "unchanged");
    assert(path_resolve("/", input, result) == -ERROR_NAME_TOO_LONG);
    assert(strcmp(result, "unchanged") == 0);
    assert(path_resolve("/missing", "file", result) == -ERROR_INVALID_ARGUMENT);
    assert(path_resolve(NULL, "file", result) == -ERROR_INVALID_ARGUMENT);
    assert(!path_directory("/tmp/file") && path_directory("/disk"));
    puts("PASS: shell path normalization, parent/root boundaries and overflow rollback");
}
