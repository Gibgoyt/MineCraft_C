/* See README.md. */
#include "texture_pack/support/index.h"

#include <errno.h> /* program_invocation_short_name too */
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TP_NAME_MAX_BYTES 200u
#define TP_ALIGN 16u

void *tp_arena_alloc(tp_arena_t *arena, uint64_t bytes) {
    const uint64_t start = (arena->used + (TP_ALIGN - 1u)) & ~(uint64_t)(TP_ALIGN - 1u);
    if (start > arena->capacity || bytes > arena->capacity - start) {
        fprintf(stderr, "texture_pack: out of arena memory asking for %" PRIu64 " bytes\n", bytes);
        exit(2);
    }
    arena->used = start + bytes;
    uint8_t *block = arena->borrowed_base + start;
    memset(block, 0, bytes);
    return block;
}

char *tp_arena_text(tp_arena_t *arena, const void *bytes, uint64_t length) {
    char *text = tp_arena_alloc(arena, length + 1u);
    if (length > 0) {
        memcpy(text, bytes, length);
    }
    return text;
}

char *tp_arena_format(tp_arena_t *arena, const char *format, ...) {
    va_list measure;
    va_start(measure, format);
    const int32_t needed = vsnprintf(nullptr, 0, format, measure);
    va_end(measure);
    if (needed < 0) {
        fprintf(stderr, "texture_pack: a message could not be formatted\n");
        exit(2);
    }
    const uint64_t size = (uint64_t)needed + 1u;
    char *text = tp_arena_alloc(arena, size);
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, size, format, arguments);
    va_end(arguments);
    return text;
}

bool tp_fail(const char *format, ...) {
    fprintf(stderr, "%s: ", program_invocation_short_name); /* texture_vector shares this */
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    return false;
}

/* Reads an open descriptor and closes it. */
static tp_read_status_t tp_read_descriptor(tp_arena_t *arena, int32_t descriptor, const char *shown, uint64_t limit,
                                           uint8_t **out_bytes, uint64_t *out_length) {
    tp_read_status_t status = TP_READ_REFUSED;
    struct stat info;
    if (fstat(descriptor, &info) != 0) {
        tp_fail("%s: %s", shown, strerror(errno));
    } else if (!S_ISREG(info.st_mode)) {
        tp_fail("%s: not a regular file", shown);
    } else if ((uint64_t)info.st_size > limit) {
        tp_fail("%s: %" PRIu64 " bytes is over this file's limit of %" PRIu64, shown, (uint64_t)info.st_size, limit);
    } else {
        const uint64_t expected = (uint64_t)info.st_size;
        /* One byte more than expected, so a file that grew is noticed rather than cut short. */
        uint8_t *bytes = tp_arena_alloc(arena, expected + 2u);
        uint64_t have = 0;
        bool failed = false;
        while (have <= expected) {
            const int64_t got = read(descriptor, bytes + have, expected + 1u - have);
            if (got < 0) {
                if (errno != EINTR) {
                    tp_fail("%s: %s", shown, strerror(errno));
                    failed = true;
                    break;
                }
            } else if (got == 0) {
                break;
            } else {
                have += (uint64_t)got;
            }
        }
        if (!failed && have != expected) {
            tp_fail("%s: changed size while being read", shown);
        } else if (!failed) {
            *out_bytes = bytes;
            *out_length = have;
            status = TP_READ_OK;
        }
    }
    close(descriptor);
    return status;
}

tp_read_status_t tp_read_path(tp_arena_t *arena, const char *path, uint64_t limit, uint8_t **out_bytes,
                              uint64_t *out_length) {
    const int32_t descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0) {
        if (errno == ENOENT) {
            return TP_READ_MISSING;
        }
        tp_fail("%s: %s", path, strerror(errno));
        return TP_READ_REFUSED;
    }
    return tp_read_descriptor(arena, descriptor, path, limit, out_bytes, out_length);
}

/*
 * Opens the directories of relative in turn, following no link, and stopping before its last component if asked. Returns
 * the descriptor of the last one opened, with *out_rest at what is left of relative, which is changed. -1 on failure,
 * with the reason printed or, for a component that is simply not there, with *out_missing set instead.
 */
static int32_t tp_walk_directories(const char *root, char *relative, bool stop_before_last, char **out_rest,
                                   bool *out_missing) {
    *out_missing = false;
    int32_t current = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (current < 0) {
        if (errno == ENOENT) {
            *out_missing = true;
        } else {
            tp_fail("%s: %s", root, strerror(errno));
        }
        return -1;
    }
    char *component = relative;
    while (component[0] != '\0') {
        char *slash = strchr(component, '/');
        if (slash == nullptr && stop_before_last) {
            break;
        }
        if (slash != nullptr) {
            *slash = '\0';
        }
        if (component[0] == '\0' || strcmp(component, ".") == 0 || strcmp(component, "..") == 0) {
            tp_fail("%s: a path inside it has an empty, '.' or '..' component", root);
            close(current);
            return -1;
        }
        const int32_t next = openat(current, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        const int32_t saved_errno = errno;
        close(current);
        if (next < 0) {
            if (saved_errno == ENOENT) {
                *out_missing = true;
            } else {
                /* ENOTDIR is also what O_DIRECTORY | O_NOFOLLOW says about a symbolic link. */
                tp_fail("%s: directory '%s' on the way refused, it may be a symbolic link: %s", root, component,
                        strerror(saved_errno));
            }
            return -1;
        }
        current = next;
        component = slash != nullptr ? slash + 1 : component + strlen(component);
    }
    *out_rest = component;
    return current;
}

tp_read_status_t tp_read_contained(tp_arena_t *arena, const char *root, const char *relative, uint64_t limit,
                                   uint8_t **out_bytes, uint64_t *out_length) {
    char *copy = tp_arena_text(arena, relative, strlen(relative));
    char *rest = nullptr;
    bool missing = false;
    const int32_t directory = tp_walk_directories(root, copy, true, &rest, &missing);
    if (directory < 0) {
        return missing ? TP_READ_MISSING : TP_READ_REFUSED;
    }
    if (rest[0] == '\0' || strcmp(rest, ".") == 0 || strcmp(rest, "..") == 0) {
        tp_fail("%s: '%s' does not name a file", root, relative);
        close(directory);
        return TP_READ_REFUSED;
    }
    /* O_NONBLOCK: opening a FIFO would otherwise wait forever for a writer. The read below refuses it. */
    const int32_t descriptor = openat(directory, rest, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    const int32_t saved_errno = errno;
    close(directory);
    if (descriptor < 0) {
        if (saved_errno == ENOENT) {
            return TP_READ_MISSING;
        }
        if (saved_errno == ELOOP) {
            tp_fail("%s/%s: a symbolic link, and links in a pack are not followed", root, relative);
        } else {
            tp_fail("%s/%s: %s", root, relative, strerror(saved_errno));
        }
        return TP_READ_REFUSED;
    }
    return tp_read_descriptor(arena, descriptor, relative, limit, out_bytes, out_length);
}

int32_t tp_open_contained_directory(tp_arena_t *arena, const char *root, const char *relative) {
    char *copy = tp_arena_text(arena, relative, strlen(relative));
    char *rest = nullptr;
    bool missing = false;
    const int32_t directory = tp_walk_directories(root, copy, false, &rest, &missing);
    if (directory < 0 && missing) {
        tp_fail("%s: directory '%s' does not exist", root, relative);
    }
    return directory;
}

bool tp_name_is_safe(const char *name, uint64_t length) {
    if (length == 0 || length > TP_NAME_MAX_BYTES || name[0] == '.') {
        return false;
    }
    for (uint64_t i = 0; i < length; i++) {
        const char c = name[i];
        const bool good = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                          c == '-' || c == '.';
        if (!good) {
            return false;
        }
    }
    return true;
}

bool tp_path_is_safe(const char *path) {
    const char *component = path;
    while (true) {
        const char *slash = strchr(component, '/');
        const uint64_t length = slash != nullptr ? (uint64_t)(slash - component) : strlen(component);
        if (!tp_name_is_safe(component, length)) {
            return false;
        }
        if (slash == nullptr) {
            return true;
        }
        component = slash + 1;
    }
}

bool tp_text_is_plain(const char *text) {
    for (const char *c = text; *c != '\0'; c++) {
        if (*c < 0x20 || *c > 0x7E) {
            return false;
        }
    }
    return true;
}

bool tp_bytes_contain(const uint8_t *bytes, uint64_t length, const char *text) {
    return memmem(bytes, length, text, strlen(text)) != nullptr;
}

bool tp_make_directories(tp_arena_t *arena, const char *path) {
    char *copy = tp_arena_text(arena, path, strlen(path));
    char *c = copy + 1;
    while (true) {
        const char held = *c;
        if (held == '/' || held == '\0') {
            *c = '\0';
            if (mkdir(copy, 0777) != 0 && errno != EEXIST) {
                return tp_fail("%s: %s", copy, strerror(errno));
            }
            *c = held;
            if (held == '\0') {
                return true;
            }
        }
        c++;
    }
}

static bool tp_write_all(int32_t descriptor, const uint8_t *bytes, uint64_t length) {
    uint64_t done = 0;
    while (done < length) {
        const int64_t wrote = write(descriptor, bytes + done, length - done);
        if (wrote < 0) {
            if (errno != EINTR) {
                return false;
            }
        } else {
            done += (uint64_t)wrote;
        }
    }
    return true;
}

/* Whether path is a regular file holding exactly these bytes. Any trouble just means no. */
static bool tp_file_already_holds(tp_arena_t *arena, const char *path, const void *bytes, uint64_t length) {
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || (uint64_t)info.st_size != length) {
        return false;
    }
    uint8_t *existing = nullptr;
    uint64_t existing_length = 0;
    return tp_read_path(arena, path, length, &existing, &existing_length) == TP_READ_OK && existing_length == length &&
           (length == 0 || memcmp(existing, bytes, length) == 0);
}

bool tp_write_if_changed(tp_arena_t *arena, const char *path, const void *bytes, uint64_t length, bool *out_written) {
    const uint64_t mark = arena->used;
    if (out_written != nullptr) {
        *out_written = false;
    }
    bool good = true;
    if (!tp_file_already_holds(arena, path, bytes, length)) {
        const char *temporary = tp_arena_format(arena, "%s.tmp.%" PRId64, path, (int64_t)getpid());
        const int32_t descriptor = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (descriptor < 0) {
            good = tp_fail("%s: %s", temporary, strerror(errno));
        } else {
            const bool wrote = tp_write_all(descriptor, bytes, length);
            const int32_t write_errno = errno;
            const bool closed = close(descriptor) == 0;
            if (!wrote || !closed) {
                good = tp_fail("%s: %s", temporary, strerror(write_errno));
            } else if (rename(temporary, path) != 0) {
                good = tp_fail("%s: %s", path, strerror(errno));
            } else if (out_written != nullptr) {
                *out_written = true;
            }
            if (!good) {
                unlink(temporary);
            }
        }
    }
    arena->used = mark;
    return good;
}

tp_builder_t tp_builder_begin(tp_arena_t *arena, uint64_t capacity) {
    return (tp_builder_t){.borrowed_text = tp_arena_alloc(arena, capacity), .used = 0, .capacity = capacity};
}

void tp_builder_add(tp_builder_t *builder, const char *format, ...) {
    const uint64_t left = builder->capacity - builder->used;
    va_list arguments;
    va_start(arguments, format);
    const int32_t added = vsnprintf(builder->borrowed_text + builder->used, left, format, arguments);
    va_end(arguments);
    if (added < 0 || (uint64_t)added >= left) {
        fprintf(stderr, "texture_pack: a generated file is over its %" PRIu64 " bytes\n", builder->capacity);
        exit(2);
    }
    builder->used += (uint64_t)added;
}
