/* See run/src/terrain/landform/README.md. */
#ifndef MC_RUN_TERRAIN_LANDFORM_INDEX_H
#define MC_RUN_TERRAIN_LANDFORM_INDEX_H

#include "arena/index.h"
#include "terrain/density/index.h"
#include "terrain/noise/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* The noise fields one world's landform takes from the bank. */
    TERRAIN_LANDFORM_NOISE_FIELDS = 6u,
    /* The landform's lattice: its nodes are at whole multiples of this, in x and y, lined up on the world. */
    TERRAIN_LANDFORM_CELL = 32u,
    /* The most columns one call of terrain_landform_uplift_points takes, and so the most a profile batch hands it. */
    TERRAIN_LANDFORM_BATCH_COLUMNS = 128u,
};

/* The tallest plateau, in blocks: tier 2's top. The density's budget adds it up: see its header. */
enum : int32_t {
    TERRAIN_LANDFORM_PLATEAU_BLOCKS = 650,
};

/* One world's landform. Nothing changes it after terrain_landform_init, so any number of workers may read it. */
typedef struct terrain_landform_t {
    terrain_density_spline_t plateau; /* 0: the plateau input L', after the stretch, to a share of the tallest */
    const terrain_noise_field_t *borrowed_uplift; /* 296: U, where the land is lifted */
    const terrain_noise_field_t *borrowed_edge_x; /* 304: W, which moves where U is read, so a rim has bays and spurs */
    const terrain_noise_field_t *borrowed_edge_y; /* 312 */
    const terrain_noise_field_t *borrowed_rugged; /* 320: G, how sharp a region's mountains are: on the nodes */
    const terrain_noise_field_t *borrowed_knobs;  /* 328: K, where knobs stand: at each column */
    const terrain_noise_field_t *borrowed_peak;   /* 336: the ridged summit rock: at each column high on a range */
} terrain_landform_t;
static_assert(sizeof(terrain_landform_t) == 344);

/* What the plateau comes to at a column. */
typedef struct terrain_landform_shape_t {
    float64_t plateau; /* 0: from 0 to 1, a share of TERRAIN_LANDFORM_PLATEAU_BLOCKS */
    float64_t face;    /* 8: from 0 to 1: the plateau's slope over 2, at the typical gradient of its input */
    float64_t input;   /* 16: L', what the spline read, after the stretch */
    float64_t stretch; /* 24: k, from 0.2 to 1: how much wider than the seaward face this edge is drawn, upside down */
} terrain_landform_shape_t;
static_assert(sizeof(terrain_landform_shape_t) == 32);

/* Takes the landform from arena and adds its fields to bank, for bank's seed. For use while terrain_init runs. If the bank
 * refuses a field, which has logged why, sets *out to nullptr and returns false. Fields added before that stay in the bank,
 * so the caller gives up on the whole terrain_t. */
[[nodiscard]] bool terrain_landform_init(terrain_landform_t **out, arena_t *arena, terrain_noise_bank_t *bank);

/* Empties the handle. The memory goes back with the arena. */
void terrain_landform_deinit(terrain_landform_t *landform);

/* The uplift U, spread to -1 to 1, at count lattice nodes (i, j), the node at (TERRAIN_LANDFORM_CELL * i,
 * TERRAIN_LANDFORM_CELL * j), read at that point moved by the edge warp; and the ruggedness G there, from 0 to 1, into
 * rugged. A pure function of the landform and the node. */
void terrain_landform_nodes(const terrain_landform_t *landform, const int32_t *is, const int32_t *js, uint32_t count,
                            float32_t *uplift, float32_t *rugged);

/* The uplift at count columns anywhere (up to TERRAIN_LANDFORM_BATCH_COLUMNS), each blended from the four nodes round it, x
 * first and then y, in float64_t, so a column's bits do not depend on which other columns came with it. A node is found
 * once per call however many columns share it. Must be finite, and within 2^35 blocks of 0. */
void terrain_landform_uplift_points(const terrain_landform_t *landform, const float64_t *xs, const float64_t *ys,
                                    uint32_t count, float64_t *uplift);
/* The same, and the ruggedness G blended the same way into rugged, from the same nodes. */
void terrain_landform_lattice_points(const terrain_landform_t *landform, const float64_t *xs, const float64_t *ys,
                                     uint32_t count, float64_t *uplift, float64_t *rugged);

/* The knob field K, spread to -1 to 1, into knobs, and its second octave alone, spread to 0 to 1, into size, at count
 * columns anywhere (any number). For the density's knobs: see run/src/terrain/density/README.md. */
void terrain_landform_knobs_points(const terrain_landform_t *landform, const float64_t *xs, const float64_t *ys,
                                   uint64_t count, float32_t *knobs, float32_t *size);

/* The summit field, spread to -1 to 1, at count columns anywhere (any number). Its ridged form, 1 - |n|, is the density's. */
void terrain_landform_peak_points(const terrain_landform_t *landform, const float64_t *xs, const float64_t *ys,
                                  uint64_t count, float32_t *peak);

/* The README's plateau: from the blended uplift and broad continentalness at a column to its plateau and face. Plain
 * arithmetic, so a test can hand it any input it likes, such as a straight ramp. */
terrain_landform_shape_t terrain_landform_shape_from(const terrain_landform_t *landform, float64_t uplift,
                                                     float64_t broad);

#endif
