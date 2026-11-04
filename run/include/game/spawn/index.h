/* See run/src/game/spawn/README.md. */
#ifndef MC_RUN_GAME_SPAWN_INDEX_H
#define MC_RUN_GAME_SPAWN_INDEX_H

#include "game/index.h"
#include "terrain/index.h"

#include <stdint.h>

enum : int32_t {
    /* How far from the middle the search goes, in blocks, along either axis. */
    GAME_SPAWN_REACH = 32768,
};
enum : uint32_t {
    /* Places the search looks at when none is land: game_spawn_walk's three bands, 129^2, then 257^2 - 33^2, then 257^2 - 65^2. */
    GAME_SPAWN_PLACES = 143425u,
    /* Squares it goes round, each of which tells the game it is alive. */
    GAME_SPAWN_SQUARES = 273u,
};

/* Whether something could start on the place at this x and y. */
typedef bool (*game_spawn_probe_t)(void *borrowed_context, int32_t x, int32_t y);

/* The highest z with anything in it at this x and y, water and what grows included. Only job's two buffers are used. */
int32_t game_ground_z(const terrain_t *terrain, const game_column_job_t *job, int32_t x, int32_t y);

/* Whether a body could start on a column's top, from the column's blocks, which start at COLUMN_Z_STORAGE_MIN: bare ground. Not water, what
 * grows, a tree's leaves or a cactus. Not ice of any kind, nor snow on it, and nothing that lies directly on water. */
bool game_spawn_top_is_dry_land(const uint16_t *states, int32_t ground_z);

/* False only where a column's fields leave its top no way to be dry land, by which the search skips making the blocks of a sea. */
bool game_spawn_may_be_dry(const terrain_hydrology_column_t *column);

/* Goes round squares of growing size about the middle, nearest first, each square in the same order, and stops at the first place probe
 * takes, which goes in out_x and out_y. If none in GAME_SPAWN_REACH is, the middle does, and it returns false. game, which may be nullptr,
 * is told it is still going once a square. */
bool game_spawn_walk(const game_t *game, game_spawn_probe_t probe, void *borrowed_context, int32_t middle_x,
                     int32_t middle_y, int32_t *out_x, int32_t *out_y);

/* Where a new world's body starts: game_spawn_walk over this terrain's real dry land. Only job's two buffers are used, and the same seed
 * gives the same place. False if there is no land to start on, and the place is then the middle, for the body to start in the water.
 * bench/world_map/ asks this too, so that what it reports is where the game really starts. */
bool game_spawn_find(const game_t *game, const terrain_t *terrain, const game_column_job_t *job, int32_t middle_x,
                     int32_t middle_y, int32_t *out_x, int32_t *out_y);

#endif
