/* See run/src/world/column/README.md. */
#ifndef MC_RUN_WORLD_COLUMN_INDEX_H
#define MC_RUN_WORLD_COLUMN_INDEX_H

#include "arena/index.h"
#include "machine/index.h"
#include "world/section/index.h"

#include <stdint.h>

enum : int32_t {
    /* What storage covers: [COLUMN_Z_STORAGE_MIN, COLUMN_Z_STORAGE_END). */
    COLUMN_Z_STORAGE_MIN = -256,
    COLUMN_Z_STORAGE_END = 1280,
    /* What gameplay may touch: both ends included. */
    COLUMN_Z_GAMEPLAY_MIN = MC_WORLD_Z_MIN,
    COLUMN_Z_GAMEPLAY_MAX = MC_WORLD_Z_MAX,
};
enum : uint32_t {
    COLUMN_SIZE = MC_WORLD_CHUNK_SIZE,
    COLUMN_SECTIONS = 96u,
};
static_assert((uint32_t)COLUMN_SIZE == (uint32_t)SECTION_SIZE);
static_assert(COLUMN_SIZE == 16u); /* the shifts below */
static_assert((COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN) == (int32_t)(COLUMN_SECTIONS * SECTION_SIZE));
static_assert(COLUMN_Z_STORAGE_MIN % (int32_t)SECTION_SIZE == 0);
static_assert(COLUMN_Z_GAMEPLAY_MIN >= COLUMN_Z_STORAGE_MIN && COLUMN_Z_GAMEPLAY_MAX < COLUMN_Z_STORAGE_END);

/* A 16x16 stack of sections. A section with nothing in it costs its 24 bytes here and nothing else. */
typedef struct column_t {
    section_t sections[COLUMN_SECTIONS]; /* 0: the lowest first */
    int32_t chunk_x;                     /* 2304 */
    int32_t chunk_y;                     /* 2308 */
} column_t;
static_assert(sizeof(column_t) == 2312);

/* One bit per section of a column: more of them than a word has. Small enough to pass by value, and it is also how a save file holds one. */
enum : uint32_t {
    COLUMN_MASK_WORDS = 2u,
    COLUMN_MASK_WORD_BITS = 64u,
};
typedef struct column_mask_t {
    uint64_t words[COLUMN_MASK_WORDS]; /* 0: section 0 is bit 0 of the first */
} column_mask_t;
static_assert(sizeof(column_mask_t) == 16);
static_assert(alignof(column_mask_t) == 8);
static_assert((uint32_t)COLUMN_SECTIONS > (uint32_t)COLUMN_MASK_WORD_BITS &&
              (uint32_t)COLUMN_SECTIONS < (uint32_t)COLUMN_MASK_WORDS * (uint32_t)COLUMN_MASK_WORD_BITS);

#define COLUMN_MASK_NONE ((column_mask_t){{0ull, 0ull}})
#define COLUMN_MASK_ALL                                                                                                \
    ((column_mask_t){{UINT64_MAX, (1ull << ((uint32_t)COLUMN_SECTIONS - (uint32_t)COLUMN_MASK_WORD_BITS)) - 1ull}})

typedef enum column_status_t : uint8_t {
    COLUMN_STATUS_OK = 0,
    COLUMN_STATUS_Z_OUT_OF_RANGE = 1, /* outside gameplay's range: nothing was read or changed */
    COLUMN_STATUS_NO_MEMORY = 2,      /* the pool has no room: nothing was changed */
} column_status_t;

/* From a world coordinate, x or y, to its column and its place in that column. Floors, for negatives too: -1 is in chunk -1
 * at 15. Right shift of a negative is arithmetic on every compiler this project builds with. */
static inline int32_t column_chunk_of(int32_t coordinate) {
    return coordinate >> 4;
}
static inline uint32_t column_local_of(int32_t coordinate) {
    return (uint32_t)coordinate & (COLUMN_SIZE - 1u);
}

/* The limits of the world. Everything that moves or places must go through one of these or a function below. */
static inline bool column_z_in_gameplay(int32_t z) {
    return z >= COLUMN_Z_GAMEPLAY_MIN && z <= COLUMN_Z_GAMEPLAY_MAX;
}
static inline int32_t column_z_clamp(int32_t z) {
    return z < COLUMN_Z_GAMEPLAY_MIN ? COLUMN_Z_GAMEPLAY_MIN : (z > COLUMN_Z_GAMEPLAY_MAX ? COLUMN_Z_GAMEPLAY_MAX : z);
}
static inline bool column_z_in_storage(int32_t z) {
    return z >= COLUMN_Z_STORAGE_MIN && z < COLUMN_Z_STORAGE_END;
}

/* From a z in storage to its section slot and its place in that section. */
static inline uint32_t column_section_of(int32_t z) {
    ASSERT(column_z_in_storage(z));
    return (uint32_t)(z - COLUMN_Z_STORAGE_MIN) >> 4;
}
static inline uint32_t column_section_local_of(int32_t z) {
    ASSERT(column_z_in_storage(z));
    return (uint32_t)(z - COLUMN_Z_STORAGE_MIN) & (SECTION_SIZE - 1u);
}

static inline column_mask_t column_mask_of(uint32_t section) {
    ASSERT(section < COLUMN_SECTIONS);
    column_mask_t mask = COLUMN_MASK_NONE;
    mask.words[section / COLUMN_MASK_WORD_BITS] = 1ull << (section % COLUMN_MASK_WORD_BITS);
    return mask;
}
static inline bool column_mask_has(column_mask_t mask, uint32_t section) {
    ASSERT(section < COLUMN_SECTIONS);
    return ((mask.words[section / COLUMN_MASK_WORD_BITS] >> (section % COLUMN_MASK_WORD_BITS)) & 1ull) != 0ull;
}
static inline bool column_mask_any(column_mask_t mask) {
    return (mask.words[0] | mask.words[1]) != 0ull;
}
static inline bool column_mask_equal(column_mask_t a, column_mask_t b) {
    return a.words[0] == b.words[0] && a.words[1] == b.words[1];
}
static inline column_mask_t column_mask_or(column_mask_t a, column_mask_t b) {
    return (column_mask_t){{a.words[0] | b.words[0], a.words[1] | b.words[1]}};
}
static inline column_mask_t column_mask_and(column_mask_t a, column_mask_t b) {
    return (column_mask_t){{a.words[0] & b.words[0], a.words[1] & b.words[1]}};
}
/* What is in a and not in b. */
static inline column_mask_t column_mask_without(column_mask_t a, column_mask_t b) {
    return (column_mask_t){{a.words[0] & ~b.words[0], a.words[1] & ~b.words[1]}};
}
static inline uint32_t column_mask_count(column_mask_t mask) {
    return (uint32_t)__builtin_popcountll(mask.words[0]) + (uint32_t)__builtin_popcountll(mask.words[1]);
}
/* The lowest section in a mask with any. */
static inline uint32_t column_mask_lowest(column_mask_t mask) {
    ASSERT(column_mask_any(mask));
    return mask.words[0] != 0ull ? (uint32_t)__builtin_ctzll(mask.words[0])
                                 : COLUMN_MASK_WORD_BITS + (uint32_t)__builtin_ctzll(mask.words[1]);
}
static inline column_mask_t column_mask_drop_lowest(column_mask_t mask) {
    if (mask.words[0] != 0ull) {
        mask.words[0] &= mask.words[0] - 1ull;
    } else {
        mask.words[1] &= mask.words[1] - 1ull;
    }
    return mask;
}

/* Room for count columns, from the arena, on the map's behalf. Contents are not defined, so that memory a map never streams
 * into is never touched: column_init each before use. Aborts as for running out, since the count is a budget set at init. */
column_t *column_array_alloc(arena_t *arena, uint64_t count);

/* Every section air under open sky, and allocates nothing. Whatever it held before is forgotten: this is for empty memory. */
void column_init(column_t *column, int32_t chunk_x, int32_t chunk_y);

/* Returns everything to the pool, for when a column streams out. Safe on zero bytes. */
void column_deinit(column_t *column, section_pool_t *pool);

/* One voxel. x and y are from 0 to COLUMN_SIZE - 1: column_local_of. z is a world z, and anything outside gameplay's range
 * gets COLUMN_STATUS_Z_OUT_OF_RANGE, or false from a read with *out left alone, though storage reaches a little further.
 * column_block_get gives the block id alone, which is what nearly everything wants; column_voxel_get gives the id with its
 * state, run/include/types/block_state/, for what goes by which way a block faces. column_block_set stores what it is given,
 * state and all: an id alone is that block facing north. */
bool column_block_get(const column_t *column, uint32_t x, uint32_t y, int32_t z, uint16_t *out);
bool column_voxel_get(const column_t *column, uint32_t x, uint32_t y, int32_t z, uint16_t *out);
bool column_light_get(const column_t *column, uint32_t x, uint32_t y, int32_t z, uint8_t *out);
column_status_t column_block_set(column_t *column, section_pool_t *pool, uint32_t x, uint32_t y, int32_t z,
                                 uint16_t state);
column_status_t column_light_set(column_t *column, section_pool_t *pool, uint32_t x, uint32_t y, int32_t z,
                                 uint8_t packed);

/* What this column holds: the struct and its sections' arrays. */
uint64_t column_memory_bytes(const column_t *column);

#endif
