/* See README.md. Uses write and not stdio: a failing assert may be holding stdio's lock, and has no reason to trust
 * anything buffered. Nothing here takes a lock or allocates. */
#include "assert/index.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

enum { ASSERT_LINE_BYTES = 1024 };

void assert_fail(const char *file, int32_t line, const char *function, const char *expression) {
    char text[ASSERT_LINE_BYTES];
    const int32_t wanted =
        snprintf(text, sizeof(text), "assertion failed: %s:%d: in %s: %s\n", file, line, function, expression);
    uint64_t length = 0;
    if (wanted > 0) {
        length = (uint64_t)wanted < sizeof(text) ? (uint64_t)wanted : sizeof(text) - 1u;
    }
    /* A long expression loses its end, but the line still ends the way a reader expects. */
    if (length == sizeof(text) - 1u) {
        text[length - 1u] = '\n';
    }
    uint64_t written = 0;
    while (written < length) {
        const int64_t count = write(STDERR_FILENO, text + written, length - written);
        if (count > 0) {
            written += (uint64_t)count;
        } else if (count == 0 || errno != EINTR) {
            break; /* stderr is gone, and the abort below still has to happen */
        }
    }
    abort();
}
