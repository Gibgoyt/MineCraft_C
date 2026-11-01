/* See README.md. */
#include "standards_lint/support/index.h"

#include "standards_lint/catalog/index.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void *lint_arena_alloc(lint_arena_t *arena, uint64_t bytes) {
    const uint64_t aligned = (bytes + 7u) & ~(uint64_t)7u;
    if (aligned > arena->capacity - arena->used) {
        fprintf(stderr, "standards_lint: arena exhausted asking for %" PRIu64 " bytes\n", bytes);
        exit(2);
    }
    void *result = arena->borrowed_base + arena->used;
    arena->used += aligned;
    return result;
}

char *lint_arena_text(lint_arena_t *arena, const char *text, uint64_t length) {
    char *copy = lint_arena_alloc(arena, length + 1u);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

char *lint_arena_join(lint_arena_t *arena, const char *left, const char *right) {
    const uint64_t left_length = strlen(left);
    const uint64_t right_length = strlen(right);
    char *joined = lint_arena_alloc(arena, left_length + right_length + 1u);
    memcpy(joined, left, left_length);
    memcpy(joined + left_length, right, right_length + 1u);
    return joined;
}

void lint_breach(lint_report_t *report, const char *path, uint32_t line, const char *rule, const char *format, ...) {
    /* A rule the catalog does not list could never get a fixture, so it counts as a lint bug and not a breach. */
    if (!lint_catalog_has_rule(rule)) {
        lint_error(report, "internal: rule '%s' is not in the catalog", rule);
    }
    report->breach_count++;
    if (line > 0) {
        fprintf(stderr, "%s:%" PRIu32 ": [%s] ", path, line, rule);
    } else {
        fprintf(stderr, "%s: [%s] ", path, rule);
    }
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

void lint_error(lint_report_t *report, const char *format, ...) {
    report->error_count++;
    fputs("standards_lint: error: ", stderr);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

bool lint_text_equal(const char *text, uint64_t length, const char *word) {
    return strlen(word) == length && memcmp(text, word, length) == 0;
}

bool lint_starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

bool lint_ends_with(const char *text, const char *suffix) {
    const uint64_t text_length = strlen(text);
    const uint64_t suffix_length = strlen(suffix);
    return text_length >= suffix_length && memcmp(text + text_length - suffix_length, suffix, suffix_length) == 0;
}

bool lint_path_matches(const char *pattern, const char *path) {
    const char *p = pattern;
    const char *s = path;
    while (*p != '\0') {
        if (*p == '*') {
            const char *component_start = s;
            while (*s != '\0' && *s != '/') {
                s++;
            }
            if (s == component_start) {
                return false;
            }
            p++;
        } else {
            if (*p != *s) {
                return false;
            }
            if (*p == '/' && p[1] == '\0') {
                return true;
            }
            p++;
            s++;
        }
    }
    return *s == '\0';
}

bool lint_path_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0;
}

bool lint_read_file(lint_arena_t *arena, lint_report_t *report, const char *path, const char **out_text,
                    uint64_t *out_length) {
    const int32_t fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        lint_error(report, "cannot open %s", path);
        return false;
    }
    struct stat info;
    if (fstat(fd, &info) != 0 || info.st_size < 0) {
        lint_error(report, "cannot stat %s", path);
        close(fd);
        return false;
    }
    const uint64_t size = (uint64_t)info.st_size;
    char *text = lint_arena_alloc(arena, size + 1u);
    uint64_t have = 0;
    while (have < size) {
        const int64_t got = read(fd, text + have, size - have);
        if (got <= 0) {
            lint_error(report, "cannot read %s", path);
            close(fd);
            return false;
        }
        have += (uint64_t)got;
    }
    close(fd);
    text[size] = '\0';
    *out_text = text;
    *out_length = size;
    return true;
}

lint_tier_t lint_tier_of_path(const char *path) {
    const char *rest = nullptr;
    if (lint_starts_with(path, "run/src/")) {
        rest = path + strlen("run/src/");
    } else if (lint_starts_with(path, "compile/src/")) {
        rest = path + strlen("compile/src/");
    } else {
        return LINT_TIER_NONE;
    }
    uint32_t directories = 0;
    for (const char *p = rest; *p != '\0'; p++) {
        if (*p == '/') {
            directories++;
        }
    }
    if (lint_starts_with(path, "run/src/main/") && directories == 1u) {
        return LINT_TIER_ROOT;
    }
    switch (directories) {
    case 0u:
        return LINT_TIER_NONE;
    case 1u:
        return LINT_TIER_OWNER;
    case 2u:
        return LINT_TIER_ARENA;
    default:
        return LINT_TIER_BORROW;
    }
}
