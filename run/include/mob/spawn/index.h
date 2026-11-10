/* See run/src/mob/spawn/README.md. */
#ifndef MC_RUN_MOB_SPAWN_INDEX_H
#define MC_RUN_MOB_SPAWN_INDEX_H

#include "arena/index.h"
#include "entity/store/index.h"
#include "physics/index.h"
#include "tables/mobs/index.h"
#include "terrain/biome/index.h"
#include "terrain/rng/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Places looked at per tick. Each costs at most MOB_SPAWN_SCAN_CELLS reads of a block. */
    MOB_SPAWN_TRIES_PER_TICK = 32u,
    /* How far down a try looks for a floor. A try starting further above one than this finds none, which is cheap. */
    MOB_SPAWN_SCAN_CELLS = 48u,
    /* How far from the player, along x or y, a try starts, in blocks. */
    MOB_SPAWN_REACH = 128u,
    /* The most hostile mobs at once, when MOB_SPAWN_CAP_COLUMNS or more columns are loaded; fewer columns hold fewer, in
     * proportion. */
    MOB_SPAWN_CAP = 70u,
    MOB_SPAWN_CAP_COLUMNS = 289u,
    /* The same for passive mobs, the animals, which never despawn: the published game's 10. Only those in loaded columns count. */
    MOB_SPAWN_ANIMAL_CAP = 10u,
    /* An animal's chance per tick of a look for a place for a herd, as the published game looks every 400 ticks. Every tick on a
     * world's first day. */
    MOB_SPAWN_ANIMAL_ONE_IN = 400u,
    /* Places looked at for a herd, on a tick that looks. */
    MOB_SPAWN_ANIMAL_TRIES = 4u,
    /* On a world's first day, herds come within this of the player along x and y, and as near as MOB_SPAWN_FIRST_DAY_NEAR, so
     * that a few are found early, for a bed. */
    MOB_SPAWN_FIRST_DAY_REACH = 48u,
    MOB_SPAWN_FIRST_DAY_NEAR = 12u,
    /* The most each step between one of a pack and the next moves along x or along y, in blocks. */
    MOB_SPAWN_PACK_STEP = 5u,
    /* The sky at midnight gives this much less than at noon. */
    MOB_SPAWN_DARKEN_MAX = 11u,
};

/* What a store's world is like to the rules, this tick. */
typedef struct mob_spawn_view_t {
    float64_t position[COLLISION_AXES]; /* 0: the player's feet */
    float64_t sun_height;               /* 24: sin of the sun's elevation at the player: 1 overhead, -1 straight down */
    /* 32: MOB_COUNT of them, by mob_id_t: how often each is picked, in place of its spawn.weight in config/mobs/. nullptr, as the game
     * has it, for those. For a test or a bench that wants the rules at work on mobs the files turn off. */
    const uint16_t *weights;
    /* 40: what spawn.biomes is asked of. nullptr, as a test of something else may have it, lets every mob come in any biome. */
    const terrain_biome_t *biomes;
    uint8_t first_day; /* 48: 1 on the world's first day: see MOB_SPAWN_FIRST_DAY_REACH */
    uint8_t _pad0[7];  /* 49 */
} mob_spawn_view_t;
static_assert(sizeof(mob_spawn_view_t) == 56);
static_assert(offsetof(mob_spawn_view_t, weights) == 32);
static_assert(offsetof(mob_spawn_view_t, biomes) == 40);
static_assert(offsetof(mob_spawn_view_t, first_day) == 48);

/* Why a place will not do. */
typedef enum mob_spawn_status_t : uint8_t {
    MOB_SPAWN_OK = 0,
    MOB_SPAWN_TOO_NEAR = 1,    /* the player would see it arrive */
    MOB_SPAWN_TOO_FAR = 2,     /* it would be taken away again at once */
    MOB_SPAWN_NOT_LOADED = 3,  /* a column there, or under its box, is not loaded */
    MOB_SPAWN_TOO_BRIGHT = 4,  /* by block light, or by the sky with the sun where it is */
    MOB_SPAWN_BAD_SURFACE = 5, /* what is under its feet is not whole and opaque, or is on its never_on */
    MOB_SPAWN_NO_ROOM = 6,     /* something solid is in its box */
    MOB_SPAWN_IN_FLUID = 7,    /* water or lava is in its box */
    MOB_SPAWN_TOO_DARK = 8,    /* under its spawn.min_light: an animal comes by day */
    MOB_SPAWN_BAD_HEIGHT = 9,  /* outside its spawn.min_height to max_height above the sea */
    MOB_SPAWN_BAD_BIOME = 10,  /* not one of its spawn.biomes */
} mob_spawn_status_t;

/* Spawning and despawning in one world. Plain numbers, so that the same world and the same calls give the same bytes. The sim's. */
typedef struct mob_spawn_t {
    uint32_t *borrowed_idle;  /* 0: ENTITY_STORE_CAPACITY, by a handle's slot: ticks it has been past despawn_near */
    terrain_rng_t random;     /* 8 */
    uint64_t spawned_count;   /* 16 */
    uint64_t despawned_count; /* 24 */
    uint64_t refused_count;   /* 32: chosen, and the store was full */
} mob_spawn_t;
static_assert(sizeof(mob_spawn_t) == 40);

/* What a spawn takes from its arena: the idle counts, with an alignment's slack. */
enum : uint64_t {
    MOB_SPAWN_BYTES = (uint64_t)ENTITY_STORE_CAPACITY * sizeof(uint32_t) + 64ull,
};

/* Nothing has come or gone yet, and what comes next depends only on this seed. From the arena, which lasts as long as the world. */
void mob_spawn_start(mob_spawn_t *spawn, arena_t *arena, uint64_t seed);

/* How much less the sky gives with the sun this high, from 0 with it well up to MOB_SPAWN_DARKEN_MAX with it 14.5 degrees or more
 * below the horizon. sun_height is the sine of its elevation: on the equator at the equinox that is cos(2 pi (f - 0.5)) of the time
 * of day f, so there it is the published curve. */
uint32_t mob_spawn_sky_darken(float64_t sun_height);

/* What a body of this mob with its feet at position would make of being put there, on a roll of light_roll, which is drawn from 0 to
 * its max_light by the tick for a hostile mob, and is its max_light for a passive one. Position is the middle of the soles of its
 * feet. Its box is checked at the larger of its sexes' scales. Looks and changes nothing. */
mob_spawn_status_t mob_spawn_check(physics_t *physics, const world_map_t *map, mob_id_t mob,
                                   const float64_t position[COLLISION_AXES], const mob_spawn_view_t *view,
                                   uint32_t light_roll);

/* The body of this mob, at this place and facing, at its full health, by no rule at all: what the rules choose goes through here, and
 * so does anything else that makes a mob. A mob with male: and female: parts is either, half and half, by the spawn's own random
 * numbers; any other is ENTITY_STORE_SEX_NONE and draws none. ENTITY_HANDLE_NONE if the store is full, which is counted. */
entity_handle_t mob_spawn_body(mob_spawn_t *spawn, entity_store_t *store, mob_id_t mob,
                               const float64_t position[COLLISION_AXES], float32_t yaw);

/* The same, of this sex: ENTITY_STORE_SEX_MALE or _FEMALE for a mob with those parts, and ENTITY_STORE_SEX_NONE for any other, or it
 * aborts. Its box is its sex's scale times the mob's. */
entity_handle_t mob_spawn_body_as(mob_spawn_t *spawn, entity_store_t *store, mob_id_t mob,
                                  const float64_t position[COLLISION_AXES], float32_t yaw, uint8_t sex);

/* Whether a mob has a male and a female. */
bool mob_spawn_has_sexes(mob_id_t mob);

/* Whether spawn.biomes lets a mob come in this biome: always, for a mob with none. */
bool mob_spawn_biome_allows(mob_id_t mob, biome_id_t biome);

/* The most hostile mobs at once, with this many columns loaded. */
uint32_t mob_spawn_cap(uint64_t columns_loaded);

/* The most passive mobs at once, in loaded columns, with this many loaded. */
uint32_t mob_spawn_animal_cap(uint64_t columns_loaded);

/* One tick: takes away the bodies the despawn rules say, then tries MOB_SPAWN_TRIES_PER_TICK places for packs of hostile mobs, and
 * now and then MOB_SPAWN_ANIMAL_TRIES for herds of passive ones, each under its own cap. Only a body whose kind is a mob_id_t but
 * none is a mob: nothing else is touched or counted. A passive mob is never taken away. Before entity_tick, so that the grid sees
 * what this leaves. Never waits and never touches the heap. */
void mob_spawn_tick(mob_spawn_t *spawn, entity_store_t *store, physics_t *physics, const world_map_t *map,
                    const mob_spawn_view_t *view);

/* Takes away every mob whose spawn.weight in config/mobs/ is 0, as a world opens from a save: a mob that never comes by itself is not
 * kept from a save made when it did (plan 005 phase 3). Adds how many of each kind went to out_removed, by mob_id_t, and returns how
 * many in all. Nothing that is not a mob is touched, and nothing is counted as despawned. */
uint32_t mob_spawn_retire(entity_store_t *store, uint32_t out_removed[MOB_COUNT]);

#endif
