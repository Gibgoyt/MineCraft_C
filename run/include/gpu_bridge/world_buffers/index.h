/* See cuda/src/world_buffers/README.md, which implements this, and run/src/gpu_bridge/world_buffers/README.md. C API, compiled
 * by GCC and by nvcc: both sides check these static_asserts. */
#ifndef MC_RUN_GPU_BRIDGE_WORLD_BUFFERS_INDEX_H
#define MC_RUN_GPU_BRIDGE_WORLD_BUFFERS_INDEX_H

#include "gpu_bridge/device_memory/index.h"
#include "gpu_bridge/status/index.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The functions below that read an encoded section or the table run on the card in a kernel, and on the host in a test. */
#ifdef __CUDACC__
#define WORLD_BUFFERS_BOTH_SIDES __host__ __device__
#else
#define WORLD_BUFFERS_BOTH_SIDES
#endif

enum : uint32_t {
    WORLD_BUFFERS_COLUMN_SECTIONS = 96u,
    /* A column's section_mask, in words: more sections than a word has bits. */
    WORLD_BUFFERS_MASK_WORDS = 2u,
    WORLD_BUFFERS_SECTION_VOXELS = 4096u,
    WORLD_BUFFERS_BRICKS = 64u, /* 4x4x4 of them in a section, and 4x4x4 voxels in each */
    /* How far below z = 0 a column's lowest section starts. */
    WORLD_BUFFERS_Z_ORIGIN = 256u,
    /* Slabs are a whole number of these, and a section's place in the pool is counted in them. */
    WORLD_BUFFERS_GRANULE_BYTES = 128u,
    /* The pool is cut into pages of this, each holding slabs of one size. */
    WORLD_BUFFERS_PAGE_BYTES = 1u << 20,
    WORLD_BUFFERS_PAGE_GRANULES = WORLD_BUFFERS_PAGE_BYTES / WORLD_BUFFERS_GRANULE_BYTES,
    WORLD_BUFFERS_CLASS_COUNT = 12u,
    /* Ends a list of pages. */
    WORLD_BUFFERS_PAGE_NONE = UINT32_MAX,
    /* Ends a list of slabs in a page. */
    WORLD_BUFFERS_SLAB_NONE = UINT16_MAX,
};
static_assert(WORLD_BUFFERS_PAGE_GRANULES < WORLD_BUFFERS_SLAB_NONE);

/* What one section takes on the card, in this order, starting on an 8-byte boundary:
 *
 *     world_buffers_section_t            the header, whose brick_mask has one bit for each 4x4x4 brick with a voxel in it
 *     uint64_t brick_voxels[brick_count] per brick with a bit, in bit order: one bit for each voxel in it
 *     uint16_t brick_first[brick_count]  how many occupied voxels come before that brick
 *     uint16_t material[voxel_count]     the block state of each occupied voxel, in bit order
 *     uint8_t  light[voxel_count]        its light: sky in the high half, block light in the low
 *     zeros, up to a multiple of 8
 *
 * Bit b of brick_mask is the brick at (b & 3, (b >> 2) & 3, b >> 4), and bit v of a brick_voxels word the voxel at the same
 * place inside it, so walking set bits with tzcnt walks each axis in order. A section with no block has no bytes at all.
 *
 * A section whose 4096 voxels are all one block with one light is the header, with brick_count 0 and every bit of brick_mask,
 * then that one material and that one light: WORLD_BUFFERS_SECTION_BYTES_UNIFORM bytes. Buried stone is most of a column, and
 * took 12944 bytes a section before this: Stage 08.4 issue 2. */
typedef struct world_buffers_section_t {
    uint64_t brick_mask;      /* 0 */
    uint16_t voxel_count;     /* 8: at least 1, up to 4096 */
    uint16_t material_offset; /* 10: from the start of this header */
    uint16_t light_offset;    /* 12: the same */
    uint8_t brick_count;      /* 14: the bits in brick_mask; 0 for a uniform section */
    uint8_t _pad0[1];         /* 15 */
} world_buffers_section_t;
static_assert(sizeof(world_buffers_section_t) == 16);
static_assert(alignof(world_buffers_section_t) == 8);
static_assert(offsetof(world_buffers_section_t, brick_mask) == 0);
static_assert(offsetof(world_buffers_section_t, voxel_count) == 8);
static_assert(offsetof(world_buffers_section_t, material_offset) == 10);
static_assert(offsetof(world_buffers_section_t, light_offset) == 12);
static_assert(offsetof(world_buffers_section_t, brick_count) == 14);
static_assert(offsetof(world_buffers_section_t, _pad0) == 15);

enum : uint32_t {
    /* Every brick full: 12944 bytes, which is a multiple of 8 as it stands. */
    WORLD_BUFFERS_SECTION_BYTES_MAX = (uint32_t)sizeof(world_buffers_section_t) + WORLD_BUFFERS_BRICKS * 8u +
                                      WORLD_BUFFERS_BRICKS * 2u + WORLD_BUFFERS_SECTION_VOXELS * 3u,
};
static_assert(WORLD_BUFFERS_SECTION_BYTES_MAX % 8u == 0u);
enum : uint32_t {
    /* The header, a material and a light, up to a multiple of 8. */
    WORLD_BUFFERS_SECTION_BYTES_UNIFORM = 24u,
};

enum : uint32_t {
    /* A square of this many columns each way, on the world's own grid, has one top: a power of 2. */
    WORLD_BUFFERS_REGION_COLUMNS = 8u,
    WORLD_BUFFERS_REGION_SHIFT = 3u,
};
static_assert(WORLD_BUFFERS_REGION_COLUMNS == 1u << WORLD_BUFFERS_REGION_SHIFT);

/* One slot of the toroidal table, on the card and in the host's copy. All zero is a slot with no column. A stride of eight
 * cache lines, so that a slot's address is a shift. */
typedef struct world_buffers_column_t {
    uint64_t section_mask[WORLD_BUFFERS_MASK_WORDS]; /* 0: one bit per section with bytes in the pool */
    int32_t chunk_x;                                 /* 16 */
    int32_t chunk_y;                                 /* 20 */
    uint32_t generation;                             /* 24: the map's, for the load this describes */
    int16_t top_z;                                   /* 28: no block above this, so a ray above it leaves the column */
    uint8_t ready;                                   /* 30: 1 if a column is here */
    /* 31: in sections of height, one above the highest block of the WORLD_BUFFERS_REGION_COLUMNS square of columns this one is in: a ray over this
     * crosses the whole square in a step. 0 for not known. It may be high, and never low. See world_buffers_region_note. */
    uint8_t region_top;
    uint32_t sections[WORLD_BUFFERS_COLUMN_SECTIONS]; /* 32: 0 for none, or 1 + the section's granule in the pool */
    uint8_t _pad1[96];                                /* 416 */
} world_buffers_column_t;
static_assert(sizeof(world_buffers_column_t) == 512);
static_assert(alignof(world_buffers_column_t) == 8);
static_assert(offsetof(world_buffers_column_t, section_mask) == 0);
static_assert(offsetof(world_buffers_column_t, chunk_x) == 16);
static_assert(offsetof(world_buffers_column_t, chunk_y) == 20);
static_assert(offsetof(world_buffers_column_t, generation) == 24);
static_assert(offsetof(world_buffers_column_t, top_z) == 28);
static_assert(offsetof(world_buffers_column_t, ready) == 30);
static_assert(offsetof(world_buffers_column_t, region_top) == 31);
static_assert(offsetof(world_buffers_column_t, sections) == 32);
static_assert(offsetof(world_buffers_column_t, _pad1) == 416);
static_assert(WORLD_BUFFERS_COLUMN_SECTIONS > 64u && WORLD_BUFFERS_COLUMN_SECTIONS <= WORLD_BUFFERS_MASK_WORDS * 64u);

/* Which chunks the table holds right now. It scrolls without moving anything, exactly as the world's map does. */
typedef struct world_buffers_window_t {
    int32_t min_x;       /* 0: the lowest corner of the window, in chunks */
    int32_t min_y;       /* 4 */
    uint32_t min_slot_x; /* 8: where that corner is in the table */
    uint32_t min_slot_y; /* 12 */
    uint32_t side;       /* 16 */
    uint32_t slot_count; /* 20: side * side */
} world_buffers_window_t;
static_assert(sizeof(world_buffers_window_t) == 24);
static_assert(alignof(world_buffers_window_t) == 4);
static_assert(offsetof(world_buffers_window_t, min_x) == 0);
static_assert(offsetof(world_buffers_window_t, min_y) == 4);
static_assert(offsetof(world_buffers_window_t, min_slot_x) == 8);
static_assert(offsetof(world_buffers_window_t, min_slot_y) == 12);
static_assert(offsetof(world_buffers_window_t, side) == 16);
static_assert(offsetof(world_buffers_window_t, slot_count) == 20);

/* One page of the pool, as the host keeps it. The card never sees this. */
typedef struct world_buffers_page_t {
    uint32_t previous;   /* 0: in its class's list of pages with room */
    uint32_t next;       /* 4: the same, or in the list of free pages */
    uint16_t free_head;  /* 8: a slab given back, in granules from this page's start */
    uint16_t carved;     /* 10: granules never yet handed out start here */
    uint16_t live;       /* 12: slabs held */
    uint8_t class_index; /* 14 */
    uint8_t _pad0[1];    /* 15 */
} world_buffers_page_t;
static_assert(sizeof(world_buffers_page_t) == 16);
static_assert(alignof(world_buffers_page_t) == 4);
static_assert(offsetof(world_buffers_page_t, previous) == 0);
static_assert(offsetof(world_buffers_page_t, next) == 4);
static_assert(offsetof(world_buffers_page_t, free_head) == 8);
static_assert(offsetof(world_buffers_page_t, carved) == 10);
static_assert(offsetof(world_buffers_page_t, live) == 12);
static_assert(offsetof(world_buffers_page_t, class_index) == 14);
static_assert(offsetof(world_buffers_page_t, _pad0) == 15);

/* The section pool and the column table: blocks on the card, and what the host must know about them. One thread at a time,
 * the gpu thread, on the same CUDA stream as the frame's kernels: that stream's order is what makes it safe to reuse a slab a
 * kernel launched earlier was reading. The fields may be read directly; only these functions write them. */
typedef struct world_buffers_t {
    device_memory_t *borrowed_memory;        /* 0 */
    const device_block_t *borrowed_pool;     /* 8: from the owner */
    const device_block_t *borrowed_table;    /* 16: from the owner */
    world_buffers_column_t *borrowed_shadow; /* 24: the host's copy of the table; slot_count */
    uint32_t *borrowed_dirty_slots;          /* 32: slots the card's table is behind on */
    uint8_t *borrowed_dirty_flags;           /* 40: slot_count: whether a slot is in that list */
    world_buffers_page_t *borrowed_pages;    /* 48: page_count */
    uint16_t *borrowed_slab_next;            /* 56: per granule of the pool: the next slab given back in its page */
    gpu_error_t last_error;                  /* 64: the last failure */
    world_buffers_window_t window;           /* 96 */
    uint64_t pool_bytes;                     /* 120: a whole number of pages */
    uint64_t slab_bytes;                     /* 128: held by sections, slack included */
    uint64_t flush_bytes;                    /* 136: sent to the card's table so far */
    uint32_t pool_full_count;                /* 144: writes refused for lack of a slab */
    /* 148: traverse_world_t.top_limit: over every column_set since init, the highest. It never comes down, as finding that out would take
     * a walk of the table, and a limit too high costs only some steps. */
    uint32_t top_limit;
    uint32_t class_pages[WORLD_BUFFERS_CLASS_COUNT]; /* 152: per class, a list of pages with room */
    uint32_t free_pages;                             /* 200: a list of pages given back */
    uint32_t page_count;                             /* 204 */
    uint32_t pages_used;                             /* 208: pages ever taken: past here they have never been used */
    uint32_t pages_live;                             /* 212: pages with a slab in them */
    uint32_t section_count;                          /* 216: sections with bytes in the pool */
    uint32_t dirty_count;                            /* 220 */
    uint32_t flush_count;                            /* 224: flushes that sent anything */
    uint32_t flush_whole_count;                      /* 228: of those, the ones that sent the whole table */
} world_buffers_t;
static_assert(sizeof(world_buffers_t) == 232);
static_assert(alignof(world_buffers_t) == 8);
static_assert(offsetof(world_buffers_t, borrowed_memory) == 0);
static_assert(offsetof(world_buffers_t, borrowed_pool) == 8);
static_assert(offsetof(world_buffers_t, borrowed_table) == 16);
static_assert(offsetof(world_buffers_t, borrowed_shadow) == 24);
static_assert(offsetof(world_buffers_t, borrowed_dirty_slots) == 32);
static_assert(offsetof(world_buffers_t, borrowed_dirty_flags) == 40);
static_assert(offsetof(world_buffers_t, borrowed_pages) == 48);
static_assert(offsetof(world_buffers_t, borrowed_slab_next) == 56);
static_assert(offsetof(world_buffers_t, last_error) == 64);
static_assert(offsetof(world_buffers_t, window) == 96);
static_assert(offsetof(world_buffers_t, pool_bytes) == 120);
static_assert(offsetof(world_buffers_t, slab_bytes) == 128);
static_assert(offsetof(world_buffers_t, flush_bytes) == 136);
static_assert(offsetof(world_buffers_t, pool_full_count) == 144);
static_assert(offsetof(world_buffers_t, top_limit) == 148);
static_assert(offsetof(world_buffers_t, class_pages) == 152);
static_assert(offsetof(world_buffers_t, free_pages) == 200);
static_assert(offsetof(world_buffers_t, page_count) == 204);
static_assert(offsetof(world_buffers_t, pages_used) == 208);
static_assert(offsetof(world_buffers_t, pages_live) == 212);
static_assert(offsetof(world_buffers_t, section_count) == 216);
static_assert(offsetof(world_buffers_t, dirty_count) == 220);
static_assert(offsetof(world_buffers_t, flush_count) == 224);
static_assert(offsetof(world_buffers_t, flush_whole_count) == 228);

/* What a voxel lookup found. */
typedef enum world_buffers_voxel_state_t : uint8_t {
    WORLD_BUFFERS_VOXEL_NOT_LOADED = 0, /* the table has no column there */
    WORLD_BUFFERS_VOXEL_EMPTY = 1,      /* a column, and no block: this is also every z outside storage */
    WORLD_BUFFERS_VOXEL_OCCUPIED = 2,
} world_buffers_voxel_state_t;

/* One question for world_buffers_probe. */
typedef struct world_buffers_probe_query_t {
    int32_t x; /* 0: world coordinates */
    int32_t y; /* 4 */
    int32_t z; /* 8 */
} world_buffers_probe_query_t;
static_assert(sizeof(world_buffers_probe_query_t) == 12);
static_assert(alignof(world_buffers_probe_query_t) == 4);
static_assert(offsetof(world_buffers_probe_query_t, x) == 0);
static_assert(offsetof(world_buffers_probe_query_t, y) == 4);
static_assert(offsetof(world_buffers_probe_query_t, z) == 8);

/* Its answer. */
typedef struct world_buffers_probe_result_t {
    uint16_t material; /* 0: 0 unless occupied */
    uint8_t light;     /* 2: 0 unless occupied */
    uint8_t state;     /* 3: world_buffers_voxel_state_t */
} world_buffers_probe_result_t;
static_assert(sizeof(world_buffers_probe_result_t) == 4);
static_assert(alignof(world_buffers_probe_result_t) == 2);
static_assert(offsetof(world_buffers_probe_result_t, material) == 0);
static_assert(offsetof(world_buffers_probe_result_t, light) == 2);
static_assert(offsetof(world_buffers_probe_result_t, state) == 3);

enum : uint32_t {
    /* What world_buffers_probe needs its scratch block to hold, per question. */
    WORLD_BUFFERS_PROBE_BYTES = (uint32_t)(sizeof(world_buffers_probe_query_t) + sizeof(world_buffers_probe_result_t)),
};

/* What the table takes on the card, and the host memory world_buffers_init borrows, for a table and a pool of these sizes. */
uint64_t world_buffers_table_bytes(uint32_t slot_count);
uint64_t world_buffers_host_bytes(uint32_t slot_count, uint64_t pool_bytes);

/* The size a section of this many bytes takes in the pool, or 0 if it cannot be one. For anyone working out what fits. */
uint64_t world_buffers_slab_bytes(uint64_t section_bytes);

/* Starts with an empty table, on the card as well, which costs one copy of it. pool and table are the owner's blocks: the
 * pool a whole number of pages, at least one, and the table world_buffers_table_bytes. host is at least
 * world_buffers_host_bytes, 8-aligned; only what is used gets touched. All of them must outlive this. */
gpu_status_t world_buffers_init(world_buffers_t *buffers, device_memory_t *memory, const device_block_t *pool,
                                const device_block_t *table, world_buffers_window_t window, uint8_t *host,
                                uint64_t host_bytes);
void world_buffers_deinit(world_buffers_t *buffers);

/* Scrolls the table. The caller says which columns that changed, with world_buffers_column_set and _clear. */
void world_buffers_set_window(world_buffers_t *buffers, world_buffers_window_t window);

/* Says what column a slot holds. If its generation is not the one already there, or no column was, its sections go back to
 * the pool, since the new load starts all air. Nothing reaches the card until world_buffers_flush. */
void world_buffers_column_set(world_buffers_t *buffers, uint32_t slot, int32_t chunk_x, int32_t chunk_y,
                              uint32_t generation, int16_t top_z);

/* The column now in a slot may have changed its square's top, so this brings that top up to date in every column of the square that is here, as
 * no column can see that alone. Costs a look at each of them, and a column's worth of the table for each that changes. Without it, a square has
 * no top, which is safe, and costs a ray a step per column. */
void world_buffers_region_note(world_buffers_t *buffers, uint32_t slot);

/* No column there any more. Its sections go back to the pool. */
void world_buffers_column_clear(world_buffers_t *buffers, uint32_t slot);

/* Replaces a section of the column in a slot with these bytes, sent to the card now, though the table will not point at them
 * until world_buffers_flush. No bytes means the section is now empty. GPU_STATUS_OVER_BUDGET means no slab is free: the
 * section stays what it was, pool_full_count says so, and it is not a failure of the card. Anything else is. */
gpu_status_t world_buffers_section_write(world_buffers_t *buffers, uint32_t slot, uint32_t section_index,
                                         const void *bytes, uint64_t byte_count);

/* Brings the card's table up to date with every change so far. This is what a frame's kernels see. */
gpu_status_t world_buffers_flush(world_buffers_t *buffers);

/* Has a real kernel look up count voxels, through the table and the tree, as the card has them now, and waits for it.
 * scratch is a block of the owner's, of at least count * WORLD_BUFFERS_PROBE_BYTES. Not for a hot path: it is what a test
 * and a debug view ask with. */
gpu_status_t world_buffers_probe(world_buffers_t *buffers, const device_block_t *scratch,
                                 const world_buffers_probe_query_t *queries, world_buffers_probe_result_t *results,
                                 uint32_t count);

/* ---- Reading what is on the card -------------------------------------------------------------------------------- */

WORLD_BUFFERS_BOTH_SIDES static inline uint32_t world_buffers_popcount(uint64_t word) {
#ifdef __CUDA_ARCH__
    return (uint32_t)__popcll(word);
#else
    return (uint32_t)__builtin_popcountll(word);
#endif
}

/* Whether a column's section_mask, or a copy of its two words, has this section. */
WORLD_BUFFERS_BOTH_SIDES static inline bool world_buffers_mask_has(const uint64_t mask[WORLD_BUFFERS_MASK_WORDS],
                                                                   uint32_t section) {
    return ((mask[section >> 6] >> (section & 63u)) & 1ull) != 0ull;
}

/* Which brick, and which voxel in it, for a place in a section. Each coordinate is from 0 to 15. */
WORLD_BUFFERS_BOTH_SIDES static inline uint32_t world_buffers_brick_of(uint32_t x, uint32_t y, uint32_t z) {
    return (x >> 2) | ((y >> 2) << 2) | ((z >> 2) << 4);
}
WORLD_BUFFERS_BOTH_SIDES static inline uint32_t world_buffers_brick_voxel_of(uint32_t x, uint32_t y, uint32_t z) {
    return (x & 3u) | ((y & 3u) << 2) | ((z & 3u) << 4);
}

/* One voxel of an encoded section. True if a block is there, with its material and light. It trusts the bytes, so it is for
 * what run/src/render/encode/ wrote. */
WORLD_BUFFERS_BOTH_SIDES static inline bool world_buffers_section_voxel(const uint8_t *encoded, uint32_t x, uint32_t y,
                                                                        uint32_t z, uint16_t *out_material,
                                                                        uint8_t *out_light) {
    const world_buffers_section_t *const header = (const world_buffers_section_t *)(const void *)encoded;
    if (header->brick_count == 0u) {
        *out_material = *(const uint16_t *)(const void *)(encoded + header->material_offset);
        *out_light = encoded[header->light_offset];
        return true;
    }
    const uint32_t brick = world_buffers_brick_of(x, y, z);
    if (((header->brick_mask >> brick) & 1u) == 0u) {
        return false;
    }
    const uint32_t rank = world_buffers_popcount(header->brick_mask & ((1ull << brick) - 1ull));
    const uint8_t *const after_header = encoded + sizeof(world_buffers_section_t);
    const uint64_t word = ((const uint64_t *)(const void *)after_header)[rank];
    const uint32_t voxel = world_buffers_brick_voxel_of(x, y, z);
    if (((word >> voxel) & 1u) == 0u) {
        return false;
    }
    const uint8_t *const after_words = after_header + (uint32_t)header->brick_count * 8u;
    const uint32_t first = ((const uint16_t *)(const void *)after_words)[rank];
    const uint32_t place = first + world_buffers_popcount(word & ((1ull << voxel) - 1ull));
    *out_material = ((const uint16_t *)(const void *)(encoded + header->material_offset))[place];
    *out_light = (encoded + header->light_offset)[place];
    return true;
}

/* Which slot a chunk has, with no division. False outside the window. */
WORLD_BUFFERS_BOTH_SIDES static inline bool world_buffers_slot_of(const world_buffers_window_t *window, int32_t chunk_x,
                                                                  int32_t chunk_y, uint32_t *out_slot) {
    const int64_t dx = (int64_t)chunk_x - window->min_x;
    const int64_t dy = (int64_t)chunk_y - window->min_y;
    if (dx < 0 || dy < 0 || dx >= (int64_t)window->side || dy >= (int64_t)window->side) {
        return false;
    }
    uint32_t slot_x = window->min_slot_x + (uint32_t)dx;
    uint32_t slot_y = window->min_slot_y + (uint32_t)dy;
    slot_x = slot_x >= window->side ? slot_x - window->side : slot_x;
    slot_y = slot_y >= window->side ? slot_y - window->side : slot_y;
    *out_slot = slot_y * window->side + slot_x;
    return true;
}

/* One voxel, at world coordinates, through a table and a pool: what a ray does at the end of its walk. */
WORLD_BUFFERS_BOTH_SIDES static inline world_buffers_probe_result_t
world_buffers_voxel_lookup(const world_buffers_column_t *table, const uint8_t *pool,
                           const world_buffers_window_t *window, int32_t x, int32_t y, int32_t z) {
    world_buffers_probe_result_t result = {0u, 0u, (uint8_t)WORLD_BUFFERS_VOXEL_NOT_LOADED};
    const int32_t chunk_x = x >> 4;
    const int32_t chunk_y = y >> 4;
    uint32_t slot = 0u;
    if (!world_buffers_slot_of(window, chunk_x, chunk_y, &slot)) {
        return result;
    }
    const world_buffers_column_t *const column = &table[slot];
    if (column->ready == 0u || column->chunk_x != chunk_x || column->chunk_y != chunk_y) {
        return result;
    }
    result.state = (uint8_t)WORLD_BUFFERS_VOXEL_EMPTY;
    const int64_t from_bottom = (int64_t)z + (int64_t)WORLD_BUFFERS_Z_ORIGIN;
    if (from_bottom < 0 || from_bottom >= (int64_t)(WORLD_BUFFERS_COLUMN_SECTIONS * 16u)) {
        return result;
    }
    const uint32_t entry = column->sections[(uint32_t)from_bottom >> 4];
    if (entry == 0u) {
        return result;
    }
    const uint8_t *const encoded = pool + (uint64_t)(entry - 1u) * WORLD_BUFFERS_GRANULE_BYTES;
    if (world_buffers_section_voxel(encoded, (uint32_t)x & 15u, (uint32_t)y & 15u, (uint32_t)from_bottom & 15u,
                                    &result.material, &result.light)) {
        result.state = (uint8_t)WORLD_BUFFERS_VOXEL_OCCUPIED;
    }
    return result;
}

#ifdef __cplusplus
}
#endif

#endif
