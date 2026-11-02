/* See run/src/log/README.md. */
#ifndef MC_RUN_LOG_INDEX_H
#define MC_RUN_LOG_INDEX_H

#include <stdarg.h>
#include <stdint.h>

/* One line is one write, and a pipe keeps writes up to PIPE_BUF (4096) whole, so lines from different threads never
 * mix. A longer message is cut and ends in "...". */
enum { LOG_LINE_BYTES = 1024 };

typedef enum log_level_t : uint8_t {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3,
} log_level_t;

/* Lines below this level are dropped. Starts at DEBUG, or at INFO where NDEBUG is defined. */
void log_set_level(log_level_t level);
log_level_t log_get_level(void);

/* Where lines go. Starts at stderr. Returns the previous descriptor; the caller still owns both. */
int32_t log_set_fd(int32_t fd);

/* Formats one line into out, ending in a newline and a NUL, and returns its length without the NUL. A line too long for
 * capacity is cut and marked. Returns 0, writing nothing, with less than 16 bytes of capacity. */
[[gnu::format(printf, 6, 0)]] uint64_t log_format_line(char *out, uint64_t capacity, log_level_t level,
                                                       const char *file, int32_t line, const char *format,
                                                       va_list arguments);

/* The format attribute is what makes the compiler check every LOG_ call. So a float64_t must be cast to libc_double_t
 * here exactly as at printf, or the build fails: plans/CODING_STANDARD.md section 2. errno is preserved. */
[[gnu::format(printf, 4, 5)]] void log_write(log_level_t level, const char *file, int32_t line, const char *format,
                                             ...);

#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif
