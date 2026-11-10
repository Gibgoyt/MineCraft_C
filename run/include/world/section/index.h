/* See run/src/world/section/README.md. */
#ifndef MC_RUN_WORLD_SECTION_INDEX_H
#define MC_RUN_WORLD_SECTION_INDEX_H

#include "arena/index.h"
#include "assert/index.h"
#include "machine/index.h"
#include "types/block_state/index.h"

#include <stdint.h>

enum : uint32_t {
    SECTION_SIZE = MC_WORLD_SECTION_SIZE,
    SECTION_VOXELS = SECTION_SIZE * SECTION_SIZE * SECTION_SIZE,
    SECTION_LIGHT_MAX = 15u,
    /* Index bits per voxel. 0 is uniform, 16 is the state itself with no palette, and the rest index a palette. */
    SECTION_BITS_UNIFORM = 0u,
    SECTION_BITS_DIRECT = 16u,
    /* Size classes of the pool: one per bit width with an array, and one for light. */
    SECTION_CLASS_COUNT = 6u,
};
/* The bit layout of section_voxel_index, and of every array here, relies on this. */
static_assert(SECTION_SIZE == 16u);
/* A state here is a whole voxel: the block id in the low 12 bits and the block's own state in the 4 above, as
 * run/include/types/block_state/ lays them out. A palette tells two facings of one block apart as it does two blocks, so a
 * section costs more only when it holds both. */
static_assert((uint32_t)SECTION_BITS_DIRECT == (uint32_t)BLOCK_STATE_ID_BITS + 4u);

/* Fully sunlit and no block light: what air under an open sky holds. */
#define SECTION_LIGHT_OPEN_SKY ((uint8_t)0xF0u)

/* Sixteen cubed voxels. Zero bytes are a valid section: all state 0, which is air, and all light 0.
 *
 * Blocks: borrowed_storage is nullptr and block_uniform is the state of every voxel, or it is one block from the pool. The
 * block holds packed indices, then palette_capacity states, then as many counts of voxels per palette slot. With
 * SECTION_BITS_DIRECT it holds only the 4096 states.
 * Light: borrowed_light is nullptr and light_uniform is every voxel's, or it is 4096 bytes from the pool.
 *
 * One writer and no readers while it writes: a write may move the block. Any number of readers without a writer. */
typedef struct section_t {
    uint8_t *borrowed_storage; /* 0: from the pool, returned by section_deinit */
    uint8_t *borrowed_light;   /* 8: the same */
    uint16_t block_uniform;    /* 16: only when bits is SECTION_BITS_UNIFORM */
    uint16_t palette_used;     /* 18: slots with a count above 0; only when there is a palette */
    uint16_t scan_writes;      /* 20: writes with no count behind them, since section_compact last looked */
    uint8_t bits;              /* 22 */
    uint8_t light_uniform;     /* 23: only when borrowed_light is nullptr; section_light_pack */
} section_t;
static_assert(sizeof(section_t) == 24);

/* Where sections' blocks come from and return to. An arena cannot take one allocation back, and sections change size and
 * come and go for as long as a world is open, so released blocks wait here for the next of their size. One thread at a time. */
typedef struct section_pool_t {
    arena_t *borrowed_arena;                     /* 0 */
    uint8_t *borrowed_free[SECTION_CLASS_COUNT]; /* 8: a list through each block's first 8 bytes */
    uint64_t carved_bytes;                       /* 56: taken from the arena; never falls until a reset */
    uint64_t live_bytes;                         /* 64: held by sections right now */
    uint64_t failed_count;                       /* 72: refused for lack of room */
    uint32_t live_blocks[SECTION_CLASS_COUNT];   /* 80: held by sections right now, per class */
} section_pool_t;
static_assert(sizeof(section_pool_t) == 104);

/* Takes no memory yet. The arena must outlive the pool. */
void section_pool_init(section_pool_t *pool, arena_t *arena);
void section_pool_deinit(section_pool_t *pool);

/* For when the arena has been reset: forgets every block. Every section that held one is dead with it. */
void section_pool_reset(section_pool_t *pool);

/* One voxel's place in every array: x fastest, z slowest. Each is from 0 to SECTION_SIZE - 1. */
static inline uint32_t section_voxel_index(uint32_t x, uint32_t y, uint32_t z) {
    ASSERT(x < SECTION_SIZE && y < SECTION_SIZE && z < SECTION_SIZE);
    return (z << 8) | (y << 4) | x;
}

/* Sky in the high half, block light in the low. Each is from 0 to SECTION_LIGHT_MAX. */
static inline uint8_t section_light_pack(uint32_t sky, uint32_t block) {
    ASSERT(sky <= SECTION_LIGHT_MAX && block <= SECTION_LIGHT_MAX);
    return (uint8_t)((sky << 4) | block);
}
static inline uint32_t section_light_sky(uint8_t packed) {
    return (uint32_t)packed >> 4;
}
static inline uint32_t section_light_block(uint8_t packed) {
    return (uint32_t)packed & SECTION_LIGHT_MAX;
}

/* Every voxel the same. Allocates nothing. Whatever the section held before is forgotten, not returned: this is for a
 * section with nothing in it. */
void section_init(section_t *section, uint16_t state, uint8_t light);

/* Returns what it holds to the pool and leaves zero bytes. Safe on zero bytes. */
void section_deinit(section_t *section, section_pool_t *pool);

/* What a section with a palette keeps its states in, for reading. Slots whose count is 0 mean nothing. */
static inline uint32_t section_palette_capacity(const section_t *section) {
    return 1u << section->bits;
}
static inline const uint16_t *section_palette(const section_t *section) {
    return (const uint16_t *)(const void *)(section->borrowed_storage + SECTION_VOXELS / 8u * section->bits);
}

static inline uint16_t section_block_get(const section_t *section, uint32_t index) {
    ASSERT(index < SECTION_VOXELS);
    const uint32_t bits = section->bits;
    if (bits == SECTION_BITS_UNIFORM) {
        return section->block_uniform;
    }
    if (bits == SECTION_BITS_DIRECT) {
        return ((const uint16_t *)(const void *)section->borrowed_storage)[index];
    }
    const uint64_t *const words = (const uint64_t *)(const void *)section->borrowed_storage;
    const uint32_t bit = index * bits;
    const uint32_t slot = (uint32_t)(words[bit >> 6] >> (bit & 63u)) & ((1u << bits) - 1u);
    return section_palette(section)[slot];
}

static inline uint8_t section_light_get(const section_t *section, uint32_t index) {
    ASSERT(index < SECTION_VOXELS);
    return section->borrowed_light != nullptr ? section->borrowed_light[index] : section->light_uniform;
}

/* False only if the pool has no room for what this would need, and then the section is as it was. A section goes back to
 * no array at all when its last other state is overwritten. */
bool section_block_set(section_t *section, section_pool_t *pool, uint32_t index, uint16_t state);

/* The same for light, with one difference: a light array is not counted, so it goes back to uniform only when looked at,
 * which is every 4096 writes or at section_compact. */
bool section_light_set(section_t *section, section_pool_t *pool, uint32_t index, uint8_t packed);

/* Every voxel at once. These return the array, so they cannot fail. */
void section_block_fill(section_t *section, section_pool_t *pool, uint16_t state);
void section_light_fill(section_t *section, section_pool_t *pool, uint8_t packed);

/* From or to SECTION_VOXELS values in section_voxel_index order. An import stores the smallest form those values allow.
 * False if the pool has no room, and then the section is as it was. */
bool section_blocks_import(section_t *section, section_pool_t *pool, const uint16_t *states);
void section_blocks_export(const section_t *section, uint16_t *out_states);
bool section_lights_import(section_t *section, section_pool_t *pool, const uint8_t *packed);
void section_lights_export(const section_t *section, uint8_t *out_packed);

/* Looks for what writes cannot see: light that has become uniform, and a direct array that a palette could hold. Writes
 * call this themselves every 4096; call it after a job that is known to have finished writing, such as a light pass. */
void section_compact(section_t *section, section_pool_t *pool);

/* What this section holds in the pool. The struct itself is extra. */
uint64_t section_pool_bytes(const section_t *section);

#endif
