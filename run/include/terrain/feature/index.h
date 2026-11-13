/* See run/src/terrain/feature/README.md. */
#ifndef MC_RUN_TERRAIN_FEATURE_INDEX_H
#define MC_RUN_TERRAIN_FEATURE_INDEX_H

#include "arena/index.h"
#include "tables/biomes/index.h"
#include "terrain/biome/index.h"
#include "terrain/cave/index.h"
#include "terrain/density/index.h"
#include "terrain/hydrology/index.h"
#include "terrain/surface/index.h"
#include "types/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Everything is placed from chunks of this many blocks a side, lined up on the world. */
    TERRAIN_FEATURE_CHUNK = 16u,
    TERRAIN_FEATURE_CHUNK_SHIFT = 4u,
    TERRAIN_FEATURE_CHUNK_COLUMNS = TERRAIN_FEATURE_CHUNK * TERRAIN_FEATURE_CHUNK,
    /* What the biome table lists, and the ice spikes, which it does not. */
    TERRAIN_FEATURE_ROW_SPIKE = BIOME_FEATURE_COUNT,
    TERRAIN_FEATURE_ROWS = BIOME_FEATURE_COUNT + 1u,
    /* The deepest a lake's middle is cut below its water's top. */
    TERRAIN_FEATURE_LAKE_DEPTH_MAX = 5u,
    /* Tree kinds: their values are the biome table's, which is what lets a row be a kind. */
    TERRAIN_FEATURE_TREES = 8u,
};
static_assert((1u << TERRAIN_FEATURE_CHUNK_SHIFT) == TERRAIN_FEATURE_CHUNK);

enum : int32_t {
    /* No voxel anything here puts on the ground is further above the block it stands on. */
    TERRAIN_FEATURE_TALLEST = 26,
    /* Ground over a lake's water is cut away, up to this many layers of it. */
    TERRAIN_FEATURE_LAKE_CLEARS = 4,
    /* What a column's top says when the ground there is cut away: see the README. */
    TERRAIN_FEATURE_NO_GROUND = INT16_MIN,
};

typedef enum terrain_feature_tree_t : uint8_t {
    TERRAIN_FEATURE_TREE_OAK = BIOME_FEATURE_TREE_OAK,
    TERRAIN_FEATURE_TREE_BIRCH = BIOME_FEATURE_TREE_BIRCH,
    TERRAIN_FEATURE_TREE_SPRUCE = BIOME_FEATURE_TREE_SPRUCE,
    TERRAIN_FEATURE_TREE_JUNGLE = BIOME_FEATURE_TREE_JUNGLE,
    TERRAIN_FEATURE_TREE_ACACIA = BIOME_FEATURE_TREE_ACACIA,
    TERRAIN_FEATURE_TREE_DARK_OAK = BIOME_FEATURE_TREE_DARK_OAK,
    TERRAIN_FEATURE_TREE_MANGROVE = BIOME_FEATURE_TREE_MANGROVE,
    TERRAIN_FEATURE_TREE_CHERRY = BIOME_FEATURE_TREE_CHERRY,
} terrain_feature_tree_t;
static_assert(BIOME_FEATURE_TREE_OAK == 0 && BIOME_FEATURE_TREE_CHERRY == 7);

/* One world's features. Nothing changes it after terrain_feature_init, so any number of workers may read it. */
typedef struct terrain_feature_t {
    uint64_t row_seeds[TERRAIN_FEATURE_ROWS];      /* 0: one stream of sites per row, per chunk */
    uint64_t lake_seed;                            /* after the rows, as are the rest */
    const terrain_biome_t *borrowed_biome;         /* */
    const terrain_hydrology_t *borrowed_hydrology; /* */
    const terrain_surface_t *borrowed_surface;     /* */
    const terrain_cave_t *borrowed_cave;           /* */
} terrain_feature_t;
static_assert(sizeof(terrain_feature_t) == 8u * TERRAIN_FEATURE_ROWS + 40u);

/* What one column has on it, before anything grows there. */
typedef struct terrain_feature_column_t {
    int16_t top;          /* 0: its highest block not air or water; TERRAIN_FEATURE_NO_GROUND if the ground is cut away */
    uint16_t water;       /* 2: how many layers of water lie directly on that */
    uint16_t block;       /* 4: what top is */
    uint8_t biome;        /* 6: a biome_id_t: the one on the surface */
    uint8_t family;       /* 7: plan 006 phase 04: terrain_biome_column_t's, and the three after it */
    int16_t snow_line_z;  /* 8 */
    int16_t t_cold_cc;    /* 10 */
    uint16_t rain_factor; /* 12: terrain_feature_rain_factor, thousandths */
    uint8_t _pad0[2];     /* 14 */
} terrain_feature_column_t;
static_assert(sizeof(terrain_feature_column_t) == 16);

/* One lake: two overlapping ellipses inside one chunk, so that whether it holds is that chunk's question alone. Offsets are
 * from the chunk's corner. exists 0 is no lake. */
typedef struct terrain_feature_lake_t {
    int16_t level;      /* 0: the layer of its water's top */
    uint8_t exists;     /* 2 */
    uint8_t depth;      /* 3: at its middle */
    uint8_t centres[4]; /* 4: x, y of one ellipse, x, y of the other */
    uint8_t radii[4];   /* 8: the same */
} terrain_feature_lake_t;
static_assert(sizeof(terrain_feature_lake_t) == 12);

/* Where a tree stands, and what it needs to know of the ground round it. */
typedef struct terrain_feature_tree_site_t {
    uint64_t seed;    /* 0: its shape comes from this and nothing else */
    int32_t x;        /* 8 */
    int32_t y;        /* 12 */
    int32_t z;        /* 16: the lowest log. The block under it is what it stands on */
    int16_t foot[4];  /* 20: tops of x+1, y+1 and both for a wide trunk; of x+1, x-1, y+1, y-1 for a mangrove's roots */
    uint8_t kind;     /* 28: a terrain_feature_tree_t */
    uint8_t strands;  /* 29: vines up its trunk */
    uint8_t lift;     /* 30: layers of water it stands in */
    uint8_t _pad0[1]; /* 31 */
} terrain_feature_tree_site_t;
static_assert(sizeof(terrain_feature_tree_site_t) == 32);

/* The part of a box that may be written. */
typedef struct terrain_feature_canvas_t {
    const terrain_density_box_t *borrowed_box; /* 0 */
    uint16_t *borrowed_blocks;                 /* 8: the box's voxels */
    int32_t x_low;                             /* 16: all six ends are included */
    int32_t x_high;                            /* 20 */
    int32_t y_low;                             /* 24 */
    int32_t y_high;                            /* 28 */
    int32_t z_low;                             /* 32 */
    int32_t z_high;                            /* 36 */
} terrain_feature_canvas_t;
static_assert(sizeof(terrain_feature_canvas_t) == 40);

/* One chunk, as a tile's pass works with it. */
typedef struct terrain_feature_chunk_t {
    terrain_feature_column_t columns[TERRAIN_FEATURE_CHUNK_COLUMNS]; /* 0: top and water are good once walked is 1 */
    terrain_feature_lake_t lake;                                     /* 4096: good once walked is 1 */
    int32_t chunk_x;                                                 /* 4108 */
    int32_t chunk_y;                                                 /* 4112 */
    int32_t z_low;      /* 4116: at or under this, no column's top is sought */
    int32_t z_high;     /* 4120: over this, all is air or the sea */
    uint8_t walked;     /* 4124 */
    uint8_t tops_known; /* 4125: top and water came from a box's blocks, and only the lake is left to find */
    uint8_t _pad0[2];   /* 4126 */
} terrain_feature_chunk_t;
static_assert(sizeof(terrain_feature_chunk_t) == 4128);

enum : uint32_t {
    /* Chunks one cache holds: the ones round a row of a far field column's lines, at its coarsest level with features. */
    TERRAIN_FEATURE_CACHE_CHUNKS = 48u,
};

/* The chunks a run of small boxes close together have needed, each loaded and walked once: for the far field, whose lines are a few blocks
 * apart and each a box of its own. Nothing in it depends on which boxes asked, so the bytes are terrain_feature_fill_box's. */
typedef struct terrain_feature_cache_t {
    terrain_feature_chunk_t chunks[TERRAIN_FEATURE_CACHE_CHUNKS]; /* 0 */
    uint32_t count;                                               /* 198144: slots in use */
    uint32_t next;                                                /* 198148: the slot to load into next once all are */
    uint32_t loads;                                               /* 198152: chunks loaded, for a bench */
    uint32_t _pad0;                                               /* 198156 */
} terrain_feature_cache_t;
static_assert(sizeof(terrain_feature_cache_t) == 198160);

/* Takes the features from arena, for this world's seed. For use while terrain_init runs. What it is given must outlive it. */
void terrain_feature_init(terrain_feature_t **out, arena_t *arena, uint64_t seed, const terrain_biome_t *biome,
                          const terrain_hydrology_t *hydrology, const terrain_surface_t *surface,
                          const terrain_cave_t *cave);

/* Empties the handle. The memory goes back with the arena. */
void terrain_feature_deinit(terrain_feature_t *feature);

/* How far from its site, on x or y, any voxel of a tree of this kind can be. Under a chunk, so a tree reaches only the chunks
 * next to its own. */
int32_t terrain_feature_tree_reach(terrain_feature_tree_t kind);

/* True if a tree of this kind and seed has a trunk 2 by 2, from its site towards x+1 and y+1. */
bool terrain_feature_tree_is_wide(terrain_feature_tree_t kind, uint64_t seed);

/* All of a box, as a canvas. */
terrain_feature_canvas_t terrain_feature_canvas_of(const terrain_density_box_t *box, uint16_t *blocks);

/* Draws a tree on a canvas. What is drawn is a pure function of the site and of what is on the canvas at each voxel. */
void terrain_feature_paint_tree(const terrain_feature_canvas_t *canvas, const terrain_feature_tree_site_t *site);

enum : uint32_t {
    /* Plan 006 master 2.5: a feature row's rate is scaled by min(1.5, P_year / P_biome-mid), in thousandths. */
    TERRAIN_FEATURE_RAIN_ONE = 1000u,
    TERRAIN_FEATURE_RAIN_MOST = 1500u,
};

/* Plan 006 master 2.5: what a biome's feature rates are scaled by at a column with this yearly rain, in thousandths:
 * min(1500, 1000 P / mid), mid the middle of the biome's rain_mm, or 1000 for a biome that gives none. Integers. */
uint32_t terrain_feature_rain_factor(uint8_t biome, int32_t rain_mm_year);

/* A rate scaled by a rain factor in thousandths, as every site is decided by. Plain float arithmetic. */
float32_t terrain_feature_rate_scaled(float32_t rate, uint32_t rain_factor);

/* Plan 006 phase 04: feature_cold_paint for one column with nothing grown on it, as a far field level too coarse for
 * features needs it: column holds count blocks from origin_z up, and its top, if water, freezes by the coldest month, and if
 * dry ground, takes a snow block over it where the snowline says. climate as for terrain_feature_fill_box. */
void terrain_feature_cold_column(const terrain_feature_t *feature, const terrain_climate_sample_t *climate, int32_t x,
                                 int32_t y, int32_t origin_z, uint32_t count, uint16_t *column);

/* How deep a lake is at a column, by offsets from its chunk's corner. 0 is outside it. Plain arithmetic. */
uint32_t terrain_feature_lake_depth(const terrain_feature_lake_t *lake, uint32_t x, uint32_t y);

/* What every stage before this one left on a chunk's columns: x fastest, by a chunk's offsets. out holds
 * TERRAIN_FEATURE_CHUNK_COLUMNS values. */
void terrain_feature_chunk_columns(const terrain_feature_t *feature, const terrain_climate_sample_t *climate,
                                   int32_t chunk_x, int32_t chunk_y, terrain_feature_column_t *out);

/* The lake of a chunk, if it has one and it would hold its water. */
terrain_feature_lake_t terrain_feature_chunk_lake(const terrain_feature_t *feature,
                                                  const terrain_climate_sample_t *climate, int32_t chunk_x,
                                                  int32_t chunk_y);

/* Puts what grows on the ground, lakes, ice and snow into blocks, which holds the box's voxels in x, then y, then z order and has
 * every earlier stage in it: see the README's order. Allocates nothing, and given what those stages left, a voxel is a pure
 * function of the features and the voxel: splitting a box into parts, in any order, on any thread, gives the same bytes.
 * Plan 006 phase 03: climate, here and in every function of this header that takes one, is the sample round the box, read
 * through terrain_biome_columns_area, or nullptr for the fallback climate. */
void terrain_feature_fill_box(const terrain_feature_t *feature, const terrain_climate_sample_t *climate,
                              const terrain_density_box_t *box, uint16_t *blocks);

/* Empty, for a new run of boxes. */
void terrain_feature_cache_start(terrain_feature_cache_t *cache);

/* terrain_feature_fill_box, the same bytes, with the chunks it needs taken from cache and kept there: for many small boxes close together,
 * which would otherwise each walk the same chunks again. */
void terrain_feature_fill_box_cached(const terrain_feature_t *feature, const terrain_climate_sample_t *climate,
                                     const terrain_density_box_t *box, uint16_t *blocks,
                                     terrain_feature_cache_t *cache);

/* The same, for a box that is one whole chunk, whose own columns were read by terrain_feature_chunk_read_box. Costs a look at the
 * chunks round it, and none at its own, and gives the same bytes. */
void terrain_feature_fill_chunk_box(const terrain_feature_t *feature, const terrain_climate_sample_t *climate,
                                    const terrain_density_box_t *box, terrain_feature_chunk_t *chunk, uint16_t *blocks);

/* If the box is one whole chunk and reaches all the layers its ground can be in, fills out from blocks and returns true. blocks
 * must be just as the stages up to the flood left them, as lining and ore can change a top. Otherwise returns false, and out is
 * not for use: terrain_feature_fill_box finds the same for itself. */
bool terrain_feature_chunk_read_box(const terrain_feature_t *feature, const terrain_climate_sample_t *climate,
                                    const terrain_density_box_t *box, const uint16_t *blocks,
                                    terrain_feature_chunk_t *out);

#endif
