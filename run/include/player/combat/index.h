/* See run/src/player/combat/README.md. */
#ifndef MC_RUN_PLAYER_COMBAT_INDEX_H
#define MC_RUN_PLAYER_COMBAT_INDEX_H

#include "entity/store/index.h"
#include "player/movement/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

/* The published numbers. README.md has them in a table. */
#define PLAYER_COMBAT_REACH 3.0f64     /* how far from the eye a body can be hit: less than a block */
#define PLAYER_COMBAT_WEAKEST 0.2f32   /* of its damage, a blow with no strength has */
#define PLAYER_COMBAT_FULL_OVER 0.9f32 /* strength over this is a full swing: it can be a critical hit or a sprint's */
#define PLAYER_COMBAT_CRITICAL 1.5f32  /* what a critical hit multiplies damage by */
#define PLAYER_COMBAT_SPRINT_KNOCKBACK 0.5f32 /* a full swing at a sprint pushes this much more, along where it faces */
#define PLAYER_COMBAT_SPRINT_SLOWS 0.6f64     /* and what is left of the sprinter's own speed across */
#define PLAYER_COMBAT_TIRES 0.1f32            /* a hit that lands wears food by this much */
#define PLAYER_COMBAT_HAND_SPEED 4.0f32       /* full swings a second, for a hand and whatever is no tool */

/* What the hand remembers of swinging. The sim's. */
typedef struct player_combat_t {
    uint16_t since_swing;  /* 0: ticks since the last swing, or since this item came into the hand */
    uint16_t held_item;    /* 2: what a change of starts it over */
    uint8_t attack_before; /* 4: the button last tick, so that a press is told from a hold */
    uint8_t aiming_body;   /* 5: the button is on a body this tick, so the hands do not mine what is behind it */
    uint8_t _pad0[2];      /* 6 */
} player_combat_t;
static_assert(sizeof(player_combat_t) == 8);

/* What is in hand, as far as hitting goes. */
typedef struct player_combat_weapon_t {
    float32_t damage; /* 0: a full swing's, in half hearts */
    float32_t speed;  /* 4: full swings a second */
    uint16_t item;    /* 8 */
    uint8_t _pad0[2]; /* 10 */
} player_combat_weapon_t;
static_assert(sizeof(player_combat_weapon_t) == 12);

/* What the eye finds along its aim. */
typedef struct player_combat_aim_t {
    float64_t distance;     /* 0: to the body, with a handle */
    entity_handle_t handle; /* 8: ENTITY_HANDLE_NONE if none */
    uint8_t blocked;        /* 12: something that can be aimed at, or a column not loaded, is in reach along it */
    uint8_t _pad0[3];       /* 13 */
} player_combat_aim_t;
static_assert(sizeof(player_combat_aim_t) == 16);

/* What one tick is given. All of it is read and none of it kept. */
typedef struct player_combat_input_t {
    const player_movement_t *borrowed_movement; /* 0: whose eye, and how it is moving */
    const entity_store_t *borrowed_store;       /* 8: every body */
    const world_map_t *borrowed_map;            /* 16 */
    player_combat_weapon_t weapon;              /* 24 */
    float32_t yaw;                              /* 36 */
    float32_t pitch;                            /* 40 */
    uint8_t attack;                             /* 44: the left button */
    uint8_t may_hit;                            /* 45: 0: it is dead or asleep, and the button only counts as held */
    uint8_t _pad0[2];                           /* 46 */
} player_combat_input_t;
static_assert(sizeof(player_combat_input_t) == 48);

/* A swing that has found a body, for whoever applies it. */
typedef struct player_combat_swing_t {
    float64_t facing[2];    /* 0: a unit vector, x and y */
    entity_handle_t handle; /* 16 */
    float32_t damage;       /* 20 */
    float32_t knockback;    /* 24: more than a hit's own, along facing: a sprint's */
    float32_t strength;     /* 28: 0 to 1 */
    uint8_t critical;       /* 32 */
    uint8_t _pad0[7];       /* 33 */
} player_combat_swing_t;
static_assert(sizeof(player_combat_swing_t) == 40);

/* A hand that has just come up. */
void player_combat_start(player_combat_t *combat);

/* Full swings a second with this item: each tool's published speed, and a hand's for anything else. */
float32_t player_combat_attack_speed(uint16_t item);

/* How far this many ticks since a swing have got back to a full one, 0 to 1. */
float32_t player_combat_strength(uint32_t since_swing, float32_t speed);

/* The nearest body along this line, which is a unit vector, within PLAYER_COMBAT_REACH, and in front of anything that can be aimed at. A
 * body with no health, or waiting for its column, is not there. An eye inside a box is at 0. Whichever thread holds the map. */
void player_combat_aim(const world_map_t *map, const entity_store_t *store, const float64_t eye[COLLISION_AXES],
                       const float64_t direction[COLLISION_AXES], player_combat_aim_t *out);

/* One tick of the hand, before the hands mine. A press on a body swings, and returns true with what it does, which the caller applies and,
 * if it lands, tells player_combat_landed. A press on nothing wastes the swing's strength, and a press on a block is mining's. A button held on
 * a body does nothing more, but sets aiming_body. Never waits and never touches the heap. */
bool player_combat_tick(player_combat_t *combat, const player_combat_input_t *input, player_combat_swing_t *out);

/* What a swing's landing does to the striker's own body: a sprint's push stops the sprint, and slows it. */
void player_combat_landed(player_movement_t *movement, const player_combat_swing_t *swing);

#endif
