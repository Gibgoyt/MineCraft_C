/*
 * Arena, text and file helpers for compile/src/texture_pack. See compile/src/texture_pack/support/README.md. The pack is
 * untrusted, so tp_read_contained is the only way any of this tool's code reads a file from it.
 */
#ifndef TEXTURE_PACK_SUPPORT_INDEX_H
#define TEXTURE_PACK_SUPPORT_INDEX_H

#include <stdint.h>

/* A bump allocator over memory the tool's Owner mapped. Running out ends the process with a message and status 2. */
typedef struct tp_arena_t {
    uint8_t *borrowed_base; /* 0 */
    uint64_t capacity;      /* 8 */
    uint64_t used;          /* 16 */
} tp_arena_t;
static_assert(sizeof(tp_arena_t) == 24);

/* Zeroed, and aligned to 16 bytes. */
void *tp_arena_alloc(tp_arena_t *arena, uint64_t bytes);
/* A NUL-terminated copy. */
char *tp_arena_text(tp_arena_t *arena, const void *bytes, uint64_t length);
[[gnu::format(printf, 2, 3)]]
char *tp_arena_format(tp_arena_t *arena, const char *format, ...);

/* Prints "texture_pack: <message>" to stderr. Returns false, so a caller can write `return tp_fail(...)`. */
[[gnu::format(printf, 1, 2)]]
bool tp_fail(const char *format, ...);

typedef enum tp_read_status_t : uint8_t {
    TP_READ_OK = 0,
    TP_READ_MISSING = 1, /* no such file; nothing was printed, since a caller may be fine with that */
    TP_READ_REFUSED = 2, /* anything else; the reason was printed */
} tp_read_status_t;

/*
 * Reads a whole file we trust the path of. The result has a NUL after its last byte that length does not count. A file
 * over limit is refused before any memory is taken.
 */
tp_read_status_t tp_read_path(tp_arena_t *arena, const char *path, uint64_t limit, uint8_t **out_bytes,
                              uint64_t *out_length);

/*
 * The same, for a file inside an untrusted tree. No symbolic link is followed at any step of relative, so nothing in the
 * tree can point this outside itself, and anything but a regular file (a FIFO, say) is refused without blocking.
 */
tp_read_status_t tp_read_contained(tp_arena_t *arena, const char *root, const char *relative, uint64_t limit,
                                   uint8_t **out_bytes, uint64_t *out_length);

/* An open directory inside an untrusted tree, under the same rule, or -1 with the reason printed. relative may be "". */
int32_t tp_open_contained_directory(tp_arena_t *arena, const char *root, const char *relative);

/* One path component of [A-Za-z0-9._-], at most 200 bytes, not starting with a dot. Decides what we repeat in messages. */
bool tp_name_is_safe(const char *name, uint64_t length);
/* Safe names joined with single slashes. Refuses absolute paths, "..", and empty components. */
bool tp_path_is_safe(const char *path);
/* Printable ASCII only, so that it can go into a message, a README or quoted YAML. */
bool tp_text_is_plain(const char *text);
bool tp_bytes_contain(const uint8_t *bytes, uint64_t length, const char *text);

/* mkdir -p, without recursion. */
bool tp_make_directories(tp_arena_t *arena, const char *path);
/*
 * Replaces a file through a temporary and a rename, so a reader never sees half of it. A file already holding these
 * bytes is left alone, so its modification time stays and a rerun rebuilds nothing. out_written may be nullptr.
 */
bool tp_write_if_changed(tp_arena_t *arena, const char *path, const void *bytes, uint64_t length, bool *out_written);

/* A growing text with a fixed capacity. Running out ends the process, like the arena. */
typedef struct tp_builder_t {
    char *borrowed_text; /* 0: always NUL-terminated */
    uint64_t used;       /* 8 */
    uint64_t capacity;   /* 16 */
} tp_builder_t;
static_assert(sizeof(tp_builder_t) == 24);

tp_builder_t tp_builder_begin(tp_arena_t *arena, uint64_t capacity);
[[gnu::format(printf, 2, 3)]]
void tp_builder_add(tp_builder_t *builder, const char *format, ...);

#endif
