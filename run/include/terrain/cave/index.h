/* See run/src/terrain/cave/README.md. */
#ifndef MC_RUN_TERRAIN_CAVE_INDEX_H
#define MC_RUN_TERRAIN_CAVE_INDEX_H

#include "arena/index.h"
#include "machine/index.h"
#include "terrain/density/index.h"
#include "terrain/noise/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* The noise fields one world's caves take from the bank. */
    TERRAIN_CAVE_NOISE_FIELDS = 9u,
    /* Caves are worked out on a lattice this many blocks apart, on all three axes. */
    TERRAIN_CAVE_CELL = 4u,
};

/* What cut a voxel. A voxel can be cut by more than one. */
enum : uint8_t {
    TERRAIN_CAVE_CHEESE = 1u,
    TERRAIN_CAVE_SPAGHETTI = 2u,
    TERRAIN_CAVE_NOODLE = 4u,
};

enum : int32_t {
    /* No cave at or below this: the lattice level two above the bedrock's, so there is always a solid floor. */
    TERRAIN_CAVE_FLOOR_TOP = MC_WORLD_Z_MIN + 2 * (int32_t)TERRAIN_CAVE_CELL,
    /* The highest lattice level that has to keep the sea out: every voxel up to the sea's surface blends only from such
     * levels. */
    TERRAIN_CAVE_SEALED_TOP = MC_WORLD_Z_MIN + (int32_t)TERRAIN_CAVE_CELL *
                                                   ((MC_WORLD_Z_SEA - MC_WORLD_Z_MIN + (int32_t)TERRAIN_CAVE_CELL - 1) /
                                                    (int32_t)TERRAIN_CAVE_CELL),
    /* Blocks of certain ground kept over each kind, above what the 3D noise could take away. See the README. */
    TERRAIN_CAVE_ROOF_CHEESE = 8,
    TERRAIN_CAVE_ROOF_NOODLE = 6,
    TERRAIN_CAVE_ROOF_SEA = 6,
};
static_assert(TERRAIN_CAVE_FLOOR_TOP > MC_WORLD_Z_MIN);
static_assert(TERRAIN_CAVE_SEALED_TOP >= MC_WORLD_Z_SEA);
static_assert(TERRAIN_CAVE_ROOF_CHEESE >= TERRAIN_CAVE_ROOF_SEA && TERRAIN_CAVE_ROOF_NOODLE >= TERRAIN_CAVE_ROOF_SEA);

/* One world's caves. Nothing changes it after terrain_cave_init, so any number of workers may read it. */
typedef struct terrain_cave_t {
    const terrain_density_t *borrowed_density;             /* 0: where the ground is */
    const terrain_noise_field_t *borrowed_cheese;          /* 8 */
    const terrain_noise_field_t *borrowed_cheese_region;   /* 16 */
    const terrain_noise_field_t *borrowed_spaghetti_a;     /* 24 */
    const terrain_noise_field_t *borrowed_spaghetti_b;     /* 32 */
    const terrain_noise_field_t *borrowed_spaghetti_gate;  /* 40 */
    const terrain_noise_field_t *borrowed_spaghetti_width; /* 48 */
    const terrain_noise_field_t *borrowed_noodle_a;        /* 56 */
    const terrain_noise_field_t *borrowed_noodle_b;        /* 64 */
    const terrain_noise_field_t *borrowed_noodle_gate;     /* 72 */
} terrain_cave_t;
static_assert(sizeof(terrain_cave_t) == 80);

/* Takes the caves from arena and adds their fields to bank, for bank's seed. For use while terrain_init runs. density must
 * outlive it. If the bank refuses a field, which has logged why, sets *out to nullptr and returns false. Fields added before
 * that stay in the bank, so the caller gives up on the whole terrain_t. */
[[nodiscard]] bool terrain_cave_init(terrain_cave_t **out, arena_t *arena, terrain_noise_bank_t *bank,
                                     const terrain_density_t *density);

/* Empties the handle. The memory goes back with the arena. */
void terrain_cave_deinit(terrain_cave_t *cave);

/* Fills out with what would cut each voxel, as TERRAIN_CAVE_* bits, 0 for none, in the box's order: x fastest, then y, then z.
 * It says what a cave wants, and may say so of a voxel that holds no rock. Allocates nothing, and a value is a pure function
 * of the caves and the voxel: splitting a box into parts, in any order, on any thread, gives the same bytes. */
void terrain_cave_mask_box(const terrain_cave_t *cave, const terrain_density_box_t *box, uint8_t *out);

/* Cuts the caves out of blocks, which holds the box's voxels in the same order: a cut voxel becomes air, unless it is air,
 * water, lava or bedrock, which are left as they are. Returns how many voxels it changed. As pure and as free of allocation
 * as the mask. */
uint64_t terrain_cave_carve_box(const terrain_cave_t *cave, const terrain_density_box_t *box, uint16_t *blocks);

/* terrain_cave_carve_box from a mask already asked for, which costs nothing more: blocks becomes what carving gives, and mask
 * becomes 1 at each voxel this changed, 0 elsewhere, which is what terrain_hydrology_flood_box takes. */
void terrain_cave_cut_with_mask(const terrain_density_box_t *box, uint8_t *mask, uint16_t *blocks);

/* The mask at one voxel, for something that asks about a single place. Voxels in bulk cost far less each through a box. */
uint8_t terrain_cave_at(const terrain_cave_t *cave, int32_t x, int32_t y, int32_t z);

#endif
