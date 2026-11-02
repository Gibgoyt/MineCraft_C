/* See run/src/terrain/noise/README.md. */
#ifndef MC_RUN_TERRAIN_NOISE_INDEX_H
#define MC_RUN_TERRAIN_NOISE_INDEX_H

#include "arena/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    TERRAIN_NOISE_OCTAVES_MAX = 12u,
};

/* What a field looks like, before a seed makes it one world's. A frequency is lattice cells per block, for the first
 * octave, and each later octave multiplies it by lacunarity and its amplitude by persistence. A frequency of 0 on an axis
 * makes the field ignore that axis, which is how a 2D field is asked for. No octave's frequency may pass 1. */
typedef struct terrain_noise_shape_t {
    float64_t frequency_x; /* 0 */
    float64_t frequency_y; /* 8 */
    float64_t frequency_z; /* 16 */
    float32_t lacunarity;  /* 24: from 1 to 16 */
    float32_t persistence; /* 28: above 0, up to 1 */
    uint32_t octaves;      /* 32: from 1 to TERRAIN_NOISE_OCTAVES_MAX */
    uint8_t _pad0[4];      /* 36 */
} terrain_noise_shape_t;
static_assert(sizeof(terrain_noise_shape_t) == 40);

/* One octave, all worked out. */
typedef struct terrain_noise_octave_t {
    float64_t frequency_x; /* 0 */
    float64_t frequency_y; /* 8 */
    float64_t frequency_z; /* 16 */
    float64_t offset_x;    /* 24: added after the frequency, in cells */
    float64_t offset_y;    /* 32 */
    float64_t offset_z;    /* 40 */
    float32_t amplitude;   /* 48: all of a field's add up to 1, so the field is roughly from -1 to 1 */
    uint32_t seed;         /* 52: what noise3_batch8 gets */
} terrain_noise_octave_t;
static_assert(sizeof(terrain_noise_octave_t) == 56);

/* A field for one world's seed. Nothing changes it after terrain_noise_bank_add, so any number of workers may read it. */
typedef struct terrain_noise_field_t {
    terrain_noise_octave_t octaves[TERRAIN_NOISE_OCTAVES_MAX]; /* 0 */
    const char *borrowed_name;                                 /* 672: must outlive the bank; a string literal */
    uint64_t salt;                                             /* 680: from the name */
    uint32_t octave_count;                                     /* 688 */
    uint8_t _pad0[4];                                          /* 692 */
} terrain_noise_field_t;
static_assert(sizeof(terrain_noise_field_t) == 696);

/* A world's fields, in memory from an arena its Owner holds. */
typedef struct terrain_noise_bank_t {
    terrain_noise_field_t *borrowed_fields; /* 0: capacity of them */
    uint64_t seed;                          /* 8 */
    uint32_t capacity;                      /* 16 */
    uint32_t count;                         /* 20 */
} terrain_noise_bank_t;
static_assert(sizeof(terrain_noise_bank_t) == 24);

/* Points whole blocks apart. Sampled with x changing fastest, then y, then z, as a section's voxels are stored:
 * out[ix + count_x * (iy + count_y * iz)] is the point origin + i * step. */
typedef struct terrain_noise_grid_t {
    int32_t origin_x; /* 0 */
    int32_t origin_y; /* 4 */
    int32_t origin_z; /* 8 */
    int32_t step_x;   /* 12 */
    int32_t step_y;   /* 16 */
    int32_t step_z;   /* 20 */
    uint32_t count_x; /* 24 */
    uint32_t count_y; /* 28 */
    uint32_t count_z; /* 32 */
} terrain_noise_grid_t;
static_assert(sizeof(terrain_noise_grid_t) == 36);

/* Takes room for capacity fields from arena. */
void terrain_noise_bank_init(terrain_noise_bank_t *bank, arena_t *arena, uint64_t seed, uint32_t capacity);

/* Empties the handle. The fields go back with the arena. */
void terrain_noise_bank_deinit(terrain_noise_bank_t *bank);

/* Makes a field from the bank's seed, the name and the shape, so the same three always give the same field, whatever was
 * added before it. For use while terrain_init runs, before any worker reads the bank. A bad shape, a name already in the
 * bank (it would be the same noise twice, which is never what was meant) or a full bank logs why, sets *out to nullptr and
 * returns false. name must outlive the bank. */
[[nodiscard]] bool terrain_noise_bank_add(terrain_noise_bank_t *bank, const char *name,
                                          const terrain_noise_shape_t *shape, const terrain_noise_field_t **out);

/* How many floats a grid fills. */
uint64_t terrain_noise_grid_points(const terrain_noise_grid_t *grid);

/* Fills out with the field at each point of the grid. out holds terrain_noise_grid_points(grid) values and needs only
 * float32_t alignment. Allocates nothing, and a value is a pure function of the field and the point: splitting a grid into
 * parts, in any order, on any thread, gives the same bits. */
void terrain_noise_sample_grid(const terrain_noise_field_t *field, const terrain_noise_grid_t *grid, float32_t *out);

/* The same at count points anywhere, in blocks, for warped coordinates. Must be finite. A whole-number point gives the same
 * bits as a grid does there. */
void terrain_noise_sample_points(const terrain_noise_field_t *field, const float64_t *xs, const float64_t *ys,
                                 const float64_t *zs, uint64_t count, float32_t *out);

#endif
