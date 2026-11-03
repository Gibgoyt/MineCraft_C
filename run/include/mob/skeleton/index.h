/* See run/src/mob/skeleton/README.md. */
#ifndef MC_RUN_MOB_SKELETON_INDEX_H
#define MC_RUN_MOB_SKELETON_INDEX_H

#include "arena/index.h"
#include "entity/index.h"
#include "mob/path/index.h"
#include "tables/mobs/index.h"
#include "terrain/rng/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* The published numbers, in ticks: a bow takes this long to draw, then rests, so a shot comes every two seconds. */
    MOB_SKELETON_DRAW_TICKS = 20u,
    MOB_SKELETON_REST_TICKS = 20u,
    /* Out of sight this long, and it forgets. */
    MOB_SKELETON_FORGET_TICKS = 60u,
    /* How often it thinks again about which way to circle, and the chance in a hundred that it changes its mind. */
    MOB_SKELETON_CIRCLE_TICKS = 20u,
    MOB_SKELETON_CIRCLE_FLIP_PERCENT = 30u,
    /* How often it asks for a new way to what it is after. */
    MOB_SKELETON_ASK_TICKS = 20u,
    /* With nothing to do: about how often it sets off, and how far along each axis. */
    MOB_SKELETON_STROLL_ONE_IN = 120u,
    MOB_SKELETON_STROLL_REACH = 10u,
    /* A way to what it is after ends when it is this near, and a stroll's when it is this near. */
    MOB_SKELETON_CHASE_RADIUS = 8u,
    MOB_SKELETON_STROLL_RADIUS = 1u,
    /* How far down it will step, when it steps back or aside without a path. */
    MOB_SKELETON_STEP_DOWN_CELLS = 3u,
};

/* In blocks, from its feet to the target's, in three dimensions. */
#define MOB_SKELETON_SIGHT 16.0f64
#define MOB_SKELETON_RANGE 15.0f64
/* What keeping its distance means: it steps back inside near, comes on outside far, and between them only circles. */
#define MOB_SKELETON_NEAR 6.0f64
#define MOB_SKELETON_FAR 11.25f64
/* Of its speed: going aside, and coming on. Stepping back has all of it. */
#define MOB_SKELETON_CIRCLE_SPEED 0.5f64
#define MOB_SKELETON_COME_ON_SPEED 0.5f64
/* The published shot: how fast, how far astray, and how much it aims above the target, per block away. */
#define MOB_SKELETON_ARROW_SPEED 1.6f64
#define MOB_SKELETON_ARROW_INACCURACY 6.0f64
#define MOB_SKELETON_ARROW_LOB 0.2f64
/* The published eye, of its height, and how far under it an arrow starts. */
#define MOB_SKELETON_EYE 0.875f64
#define MOB_SKELETON_MUZZLE_DROP 0.1f64
/* How far up the target's height it aims. */
#define MOB_SKELETON_AIM_AT (1.0f64 / 3.0f64)
/* How far its arms get to where they should be, each tick. */
#define MOB_SKELETON_AIM_EASE 0.25f32

/* What one skeleton has in mind. By the slot of its handle. */
typedef struct mob_skeleton_body_t {
    entity_handle_t owner; /* 0: whose this is, or it starts over */
    uint16_t ask_wait;     /* 4: ticks until it may ask for a way again */
    uint16_t unseen;       /* 6: ticks since it last saw the target */
    uint16_t draw;         /* 8: ticks the bow has been drawn */
    uint16_t rest;         /* 10: ticks until it may draw again */
    uint16_t circle;       /* 12: ticks since it last thought about which way to circle */
    uint8_t circle_left;   /* 14: which way it circles */
    uint8_t engaged;       /* 15: it has seen the target, and has not forgotten */
    uint8_t strolling;     /* 16 */
    uint8_t _pad0[3];      /* 17 */
} mob_skeleton_body_t;
static_assert(sizeof(mob_skeleton_body_t) == 20);

/* Every skeleton's mind in one world. Plain numbers, so that the same world and the same calls give the same bytes. The sim's. */
typedef struct mob_skeleton_t {
    mob_skeleton_body_t *borrowed_bodies; /* 0: ENTITY_STORE_CAPACITY */
    terrain_rng_t random;                 /* 8 */
    uint64_t shot_count;                  /* 16 */
    uint64_t died_count;                  /* 24 */
    uint64_t dropped_count;               /* 32: items */
} mob_skeleton_t;
static_assert(sizeof(mob_skeleton_t) == 40);

/* What it takes from its arena: the minds, with an alignment's slack. */
enum : uint64_t {
    MOB_SKELETON_BYTES = (uint64_t)ENTITY_STORE_CAPACITY * sizeof(mob_skeleton_body_t) + alignof(mob_skeleton_body_t),
};

/* What it may be after, this tick. */
typedef struct mob_skeleton_view_t {
    float64_t position[COLLISION_AXES]; /* 0: its feet */
    float64_t eye_height;               /* 24: above its feet */
    float64_t height;                   /* 32 */
    uint8_t alive;                      /* 40: 0: nothing to be after */
    uint8_t _pad0[7];                   /* 41 */
} mob_skeleton_view_t;
static_assert(sizeof(mob_skeleton_view_t) == 48);

/* Nobody has anything in mind, and what it rolls is of this seed until mob_skeleton_seed says otherwise. */
void mob_skeleton_start(mob_skeleton_t *skeleton, arena_t *arena, uint64_t seed);

/* What it rolls from here on depends only on this seed. */
void mob_skeleton_seed(mob_skeleton_t *skeleton, uint64_t seed);

/* What dying leaves: each row of a mob's drops rolled by itself, thrown out around where it was. Returns how many items. */
uint32_t mob_skeleton_drops(mob_skeleton_t *skeleton, item_drop_store_t *drops, const mob_def_t *def,
                            const float64_t position[COLLISION_AXES]);

/* One tick of every skeleton in the entity's store: one with no health left drops what it had and is taken away; the rest look, keep
 * their distance, circle, draw and shoot. Only bodies of MOB_ID_SKELETON are touched. On the thread holding the map, after mob_path_tick
 * and before entity_tick, which moves them. Never waits and never touches the heap. */
void mob_skeleton_tick(mob_skeleton_t *skeleton, mob_path_t *path, entity_t *entity, const world_map_t *map,
                       const mob_skeleton_view_t *view);

#endif
