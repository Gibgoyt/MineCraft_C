/* See run/src/save/names/README.md. */
#ifndef MC_RUN_SAVE_NAMES_INDEX_H
#define MC_RUN_SAVE_NAMES_INDEX_H

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* The most names one list may have. Past what the table tool allows, so that a world can outlive names that have gone. */
    SAVE_NAMES_ENTRIES_MAX = 16384u,
    /* The longest one name may be, without its NUL. */
    SAVE_NAMES_NAME_BYTES_MAX = 64u,
    /* What a map holds for a name this build does not have. No id is this: see SAVE_NAMES_ENTRIES_MAX. */
    SAVE_NAMES_LOST = 0xFFFFu,
};
enum : uint64_t {
    /* The largest one list may be on disk. */
    SAVE_NAMES_TABLE_BYTES_MAX = 512ull << 10,
    /* And what a map of file numbers to this build's takes. */
    SAVE_NAMES_MAP_BYTES = (uint64_t)SAVE_NAMES_ENTRIES_MAX * sizeof(uint16_t),
};

/* Which of this build's tables. A state file holds all of them, in this order, and a world's blocks the first. */
typedef enum save_names_kind_t : uint32_t {
    SAVE_NAMES_KIND_BLOCKS = 0,
    SAVE_NAMES_KIND_DROPS = 1,
    SAVE_NAMES_KIND_ITEMS = 2,
    SAVE_NAMES_KIND_MOBS = 3,
    SAVE_NAMES_KIND_COUNT = 4,
} save_names_kind_t;

/* At the start of a list on disk. After it, bytes of names, each ended by a NUL, and then zeros up to 8. */
typedef struct save_names_head_t {
    uint32_t count; /* 0: of names */
    uint32_t bytes; /* 4: of names, with their NULs, before the zeros */
} save_names_head_t;
static_assert(sizeof(save_names_head_t) == 8);
static_assert(alignof(save_names_head_t) == 4);
static_assert(offsetof(save_names_head_t, count) == 0);
static_assert(offsetof(save_names_head_t, bytes) == 4);

/* One list that has been looked at, in bytes it borrows. */
typedef struct save_names_view_t {
    const char *borrowed_names; /* 0: the first name */
    uint64_t total_bytes;       /* 8: the list's whole length on disk */
    uint32_t count;             /* 16 */
    uint32_t names_bytes;       /* 20 */
} save_names_view_t;
static_assert(sizeof(save_names_view_t) == 24);
static_assert(alignof(save_names_view_t) == 8);
static_assert(offsetof(save_names_view_t, borrowed_names) == 0);
static_assert(offsetof(save_names_view_t, total_bytes) == 8);
static_assert(offsetof(save_names_view_t, count) == 16);
static_assert(offsetof(save_names_view_t, names_bytes) == 20);

/* This build's table: how many it has, what the one at this id is called, and a name's hash over all of them in order, with their NULs. */
uint32_t save_names_live_count(save_names_kind_t kind);
const char *save_names_live_name(save_names_kind_t kind, uint32_t id);

/* This build's table as a list on disk. out has room for SAVE_NAMES_TABLE_BYTES_MAX. Returns its bytes. */
uint64_t save_names_write(save_names_kind_t kind, uint8_t *out);

/* All of them, one after another, as a state file holds them. out has room for that many of SAVE_NAMES_TABLE_BYTES_MAX. */
uint64_t save_names_write_all(uint8_t *out);

/* One list at the start of these bytes, which may go on past it. False if it is not exactly a list this game could write. Trusts nothing. */
bool save_names_read(const uint8_t *data, uint64_t bytes, save_names_view_t *out);

/* Whether these bytes are exactly this build's tables, one after another. */
bool save_names_are_live(const uint8_t *data, uint64_t bytes);

/* What each number of a list means in this build: out has view->count, and SAVE_NAMES_LOST for a name it does not have. Returns how many of those. */
uint32_t save_names_map(const save_names_view_t *view, save_names_kind_t kind, uint16_t *out);

/* Tells the log about each name a map could not find, and what becomes of it. */
void save_names_lost_log(const save_names_view_t *view, const uint16_t *map, const char *where, const char *what,
                         const char *becomes);

/* The name after this one in a list. */
const char *save_names_next(const char *name);

#endif
