/* See README.md. Works before anything has been initialized, because the first thing that may need to speak is the CPU
 * check at the top of main. That is why its two settings are statics with defaults and not fields of app_t. They own
 * nothing, so the One-Owner rule is not in play. */
#include "log/index.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    LOG_MIN_CAPACITY = 16,
#ifdef NDEBUG
    LOG_DEFAULT_LEVEL = LOG_LEVEL_INFO,
#else
    LOG_DEFAULT_LEVEL = LOG_LEVEL_DEBUG,
#endif
};

/* Both relaxed: each is one independent value read by the thread about to log, and it guards no other data. A line
 * racing a change may go by the old value, which is fine. */
static _Atomic uint8_t log_threshold = LOG_DEFAULT_LEVEL;
static _Atomic int32_t log_fd = STDERR_FILENO;

void log_set_level(log_level_t level) {
    atomic_store_explicit(&log_threshold, (uint8_t)level, memory_order_relaxed);
}

log_level_t log_get_level(void) {
    return (log_level_t)atomic_load_explicit(&log_threshold, memory_order_relaxed);
}

int32_t log_set_fd(int32_t fd) {
    return atomic_exchange_explicit(&log_fd, fd, memory_order_relaxed);
}

static const char *log_level_name(log_level_t level) {
    switch (level) {
    case LOG_LEVEL_DEBUG:
        return "debug";
    case LOG_LEVEL_INFO:
        return "info";
    case LOG_LEVEL_WARN:
        return "warn";
    case LOG_LEVEL_ERROR:
        return "error";
    }
    return "?";
}

/* Every source file is called index.c, so its directory is the useful part. CMake passes absolute paths. */
static const char *log_short_path(const char *file) {
    static const char marker[] = "/run/src/";
    const char *found = strstr(file, marker);
    return found != nullptr ? found + sizeof(marker) - 1u : file;
}

uint64_t log_format_line(char *out, uint64_t capacity, log_level_t level, const char *file, int32_t line,
                         const char *format, va_list arguments) {
    static const char cut_mark[] = "...\n";
    if (capacity < LOG_MIN_CAPACITY) {
        return 0;
    }
    /* Room for the cut mark, or for the newline and NUL, is held back from both prints. */
    const uint64_t limit = capacity - sizeof(cut_mark);
    const int32_t prefix = snprintf(out, limit, "[%s] %s:%d: ", log_level_name(level), log_short_path(file), line);
    uint64_t length = 0;
    bool cut = false;
    if (prefix > 0) {
        cut = (uint64_t)prefix >= limit;
        length = cut ? limit - 1u : (uint64_t)prefix;
    }
    if (!cut) {
        const int32_t body = vsnprintf(out + length, limit - length, format, arguments);
        if (body < 0) {
            cut = true; /* an encoding error: keep the prefix, and say that something is missing */
        } else if ((uint64_t)body >= limit - length) {
            cut = true;
            length = limit - 1u;
        } else {
            length += (uint64_t)body;
        }
    }
    if (cut) {
        memcpy(out + length, cut_mark, sizeof(cut_mark));
        return length + sizeof(cut_mark) - 1u;
    }
    out[length] = '\n';
    out[length + 1u] = '\0';
    return length + 1u;
}

void log_write(log_level_t level, const char *file, int32_t line, const char *format, ...) {
    if (level < log_get_level()) {
        return;
    }
    const int32_t saved_errno = errno;
    char text[LOG_LINE_BYTES];
    va_list arguments;
    va_start(arguments, format);
    const uint64_t length = log_format_line(text, sizeof(text), level, file, line, format, arguments);
    va_end(arguments);

    const int32_t fd = atomic_load_explicit(&log_fd, memory_order_relaxed);
    uint64_t written = 0;
    while (written < length) {
        const int64_t count = write(fd, text + written, length - written);
        if (count > 0) {
            written += (uint64_t)count;
        } else if (count == 0 || errno != EINTR) {
            break; /* nowhere left to report that logging failed */
        }
    }
    errno = saved_errno;
}
