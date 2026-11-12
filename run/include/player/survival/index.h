/* See run/src/player/survival/README.md. */
#ifndef MC_RUN_PLAYER_SURVIVAL_INDEX_H
#define MC_RUN_PLAYER_SURVIVAL_INDEX_H

#include "physics/index.h"
#include "player/camera/index.h"
#include "player/movement/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

/* Every number here is a published one, in half hearts, half drumsticks and ticks at 20 Hz. README.md has them in a table. */
enum : uint32_t {
    PLAYER_SURVIVAL_HEALTH_MAX = 20u,
    PLAYER_SURVIVAL_FOOD_MAX = 20u,
    /* The most armour points that count, as EQUIPMENT_ARMOR_POINTS_MOST. */
    PLAYER_SURVIVAL_ARMOR_MAX = 20u,
    /* Food at or below this stops a sprint. */
    PLAYER_SURVIVAL_FOOD_NO_SPRINT = 6u,
    /* Food at or above this heals, slowly. */
    PLAYER_SURVIVAL_FOOD_HEALS = 18u,
    PLAYER_SURVIVAL_AIR_MAX = 300u,
    PLAYER_SURVIVAL_AIR_REFILL = 4u,
    PLAYER_SURVIVAL_DROWN_EVERY_TICKS = 20u,
    PLAYER_SURVIVAL_INVULNERABLE_TICKS = 10u,
    PLAYER_SURVIVAL_BURN_TICKS = 300u,
    PLAYER_SURVIVAL_BURN_EVERY_TICKS = 20u,
    PLAYER_SURVIVAL_HEAL_FAST_TICKS = 10u,
    PLAYER_SURVIVAL_HEAL_SLOW_TICKS = 80u,
    PLAYER_SURVIVAL_STARVE_TICKS = 80u,
    PLAYER_SURVIVAL_EAT_TICKS = 32u,
    PLAYER_SURVIVAL_SLEEP_TICKS = 100u,
    /* How long a body stays down before a press brings it back. */
    PLAYER_SURVIVAL_RESPAWN_WAIT_TICKS = 20u,
};

#define PLAYER_SURVIVAL_SATURATION_START 5.0f32
#define PLAYER_SURVIVAL_EXHAUSTION_STEP 4.0f32
#define PLAYER_SURVIVAL_EXHAUSTION_MAX 40.0f32
#define PLAYER_SURVIVAL_FALL_SAFE 3.0f32
/* A bed works while the sun is lower than this: sin of -8.13 degrees. It is cos(2 pi (0.772583 - 0.5)), so on the equator at the
 * equinox a bed works from 12542 to 23459 ticks after sunrise, as it did with a fixed sun (plan 004 phase 02). */
#define PLAYER_SURVIVAL_NIGHT_SUN (-0.14141751442824427f64)
/* Where a night slept through comes out. */
#define PLAYER_SURVIVAL_MORNING 0.25f64
/* How far from a bed's middle a body can use it, in blocks. */
#define PLAYER_SURVIVAL_BED_REACH_ACROSS 3.0f64
#define PLAYER_SURVIVAL_BED_REACH_UP 2.0f64
/* Where the eye goes on a body that is lying down, and how fast it gets there. */
#define PLAYER_SURVIVAL_EYE_LYING 0.2f64
#define PLAYER_SURVIVAL_EYE_LOWERS 0.2f64

/* What a body is doing. */
typedef enum player_survival_state_t : uint8_t {
    PLAYER_SURVIVAL_ALIVE = 0,
    PLAYER_SURVIVAL_SLEEPING = 1,
    PLAYER_SURVIVAL_DEAD = 2,
} player_survival_state_t;

/* Why it hurts, which decides whether armor helps and whether it tires. */
typedef enum player_survival_cause_t : uint8_t {
    PLAYER_SURVIVAL_CAUSE_NONE = 0,
    PLAYER_SURVIVAL_CAUSE_FALL = 1,
    PLAYER_SURVIVAL_CAUSE_LAVA = 2,
    PLAYER_SURVIVAL_CAUSE_BURN = 3,
    PLAYER_SURVIVAL_CAUSE_CACTUS = 4,
    PLAYER_SURVIVAL_CAUSE_DROWN = 5,
    PLAYER_SURVIVAL_CAUSE_SUFFOCATE = 6,
    PLAYER_SURVIVAL_CAUSE_STARVE = 7,
    PLAYER_SURVIVAL_CAUSE_ATTACK = 8, /* from outside: a mob, an arrow, an explosion */
    PLAYER_SURVIVAL_CAUSE_COUNT = 9,
} player_survival_cause_t;

/* What the bed said to the last click on one. */
typedef enum player_survival_bed_answer_t : uint8_t {
    PLAYER_SURVIVAL_BED_NOTHING = 0,
    PLAYER_SURVIVAL_BED_TOO_FAR = 1,
    PLAYER_SURVIVAL_BED_NOT_NIGHT = 2, /* it is where the body comes back to from now on, all the same */
    PLAYER_SURVIVAL_BED_ASLEEP = 3,
} player_survival_bed_answer_t;

/* Plan 006 phase 01: where a teleported body is in arriving. */
typedef enum player_survival_arriving_t : uint8_t {
    PLAYER_SURVIVAL_ARRIVING_NONE = 0,
    PLAYER_SURVIVAL_ARRIVING_WAIT = 1, /* held still and unharmed until the column under it loads */
    PLAYER_SURVIVAL_ARRIVING_FALL = 2, /* stood where it goes, and no fall hurts it until it lands, swims or flies */
} player_survival_arriving_t;

/* How a teleported body stands once its ground loads. */
typedef enum player_survival_arrive_t : uint8_t {
    PLAYER_SURVIVAL_ARRIVE_EXACT = 0,  /* the feet where they were put; out of rock to the first room above */
    PLAYER_SURVIVAL_ARRIVE_GROUND = 1, /* on the top ground of the column, with room for the body */
} player_survival_arrive_t;

/* All that a body's health is. Plain numbers, so that the same world and buttons give the same bytes. The sim's. */
typedef struct player_survival_t {
    float64_t spawn[COLLISION_AXES];  /* 0: the feet at the world's own place to come back to */
    float64_t sun_height;             /* 24: sin of the sun's elevation at the body, as the sim last worked it out */
    uint64_t died_count;              /* 32 */
    uint64_t respawned_count;         /* 40 */
    uint64_t eaten_count;             /* 48: whoever holds the food takes one off when this goes up */
    uint64_t slept_count;             /* 56: nights slept through */
    uint64_t hurt_count;              /* 64: hits that took effect */
    uint64_t mined_seen;              /* 72: the hands' mined_count, as of the last tick */
    float32_t health;                 /* 80: 0 to PLAYER_SURVIVAL_HEALTH_MAX */
    float32_t saturation;             /* 84: 0 to food */
    float32_t exhaustion;             /* 88 */
    float32_t last_hurt;              /* 92: the size of the hit the body is invulnerable after */
    float32_t armor_points;           /* 96: 0 to 20, as worn */
    float32_t held_saturation;        /* 100: of held_item */
    int32_t bed_cell[COLLISION_AXES]; /* 104: with has_bed */
    uint32_t food_timer;              /* 116: toward the next heal or the next pang */
    uint32_t eat_ticks;               /* 120: of holding the button on this food */
    uint32_t state_ticks;             /* 124: how long it has been asleep or dead */
    int32_t air;                      /* 128: ticks left, and below 0 towards the next gasp */
    uint32_t burn_ticks;              /* 132 */
    uint16_t held_item;        /* 136: whatever identifies what is in hand, so that changing it starts eating over */
    uint8_t food;              /* 138: 0 to PLAYER_SURVIVAL_FOOD_MAX */
    uint8_t state;             /* 139: player_survival_state_t */
    uint8_t held_food;         /* 140: what eating held_item gives, and 0 if it is not food */
    uint8_t invulnerable;      /* 141: ticks left */
    uint8_t has_bed;           /* 142: bed_cell means something */
    uint8_t placing;           /* 143: it has come back, and waits for the ground there to load */
    uint8_t placing_at_bed;    /* 144: and it means to stand by the bed */
    uint8_t died_new;          /* 145: for player_survival_died_take */
    uint8_t morning_new;       /* 146: for player_survival_morning_take */
    uint8_t killed_by;         /* 147: a player_survival_cause_t */
    uint8_t buttons_before;    /* 148: what was held last tick, so that a press is told from a hold */
    uint8_t was_on_ground;     /* 149 */
    uint8_t bed_answer;        /* 150: a player_survival_bed_answer_t */
    uint8_t arriving;          /* 151: a player_survival_arriving_t (plan 006 phase 01) */
    float32_t armor_toughness; /* 152: all that is worn, added up */
    float32_t armor_hit;       /* 156: what armor has stood against since player_survival_armor_hit_take */
    uint8_t arrive_kind;       /* 160: a player_survival_arrive_t, while arriving */
    uint8_t _pad1[31];         /* 161 */
} player_survival_t;
static_assert(sizeof(player_survival_t) == 192);
static_assert(alignof(player_survival_t) == 8);

/* What the buttons are doing for one tick, on their way to the hands. It takes what it uses, and clears those. All but the buttons are
 * read and not kept. */
typedef struct player_survival_buttons_t {
    const player_target_t *borrowed_target; /* 0: what the player sees outlined */
    player_movement_t *borrowed_movement;   /* 8: the body */
    physics_t *borrowed_physics;            /* 16 */
    const world_map_t *borrowed_map;        /* 24 */
    uint8_t attack;                         /* 32 */
    uint8_t place;                          /* 33 */
    uint8_t jump;                           /* 34 */
    uint8_t sneak;                          /* 35 */
    uint8_t _pad0[4];                       /* 36 */
} player_survival_buttons_t;
static_assert(sizeof(player_survival_buttons_t) == 40);

/* Full health, full food, and the place it comes back to if it has no bed: where its feet are now. */
void player_survival_start(player_survival_t *survival, float64_t x, float64_t y, float64_t z);

/* Whether a bed works with the sun this high: sun_height is the sine of its elevation, 1 overhead and -1 straight down. */
bool player_survival_is_night(float64_t sun_height);

/* What the sim is told of the sun at the body, each tick before the buttons: the sine of its elevation there. */
void player_survival_sun_set(player_survival_t *survival, float64_t sun_height);

/* What is worn: its points, in half shirts, and its toughness. run/src/game/ says so each tick, from run/src/item/equipment/. */
void player_survival_armor_set(player_survival_t *survival, float32_t points, float32_t toughness);

/* What a hit of this size does through this much armor: the published sum. */
float32_t player_survival_after_armor(float32_t amount, float32_t points, float32_t toughness);

/* The size of the hits armor has stood against since this was last asked, and 0 if none. Each piece worn pays for them. */
float32_t player_survival_armor_hit_take(player_survival_t *survival);

/* What a fall of this distance does on landing. A bed under the feet halves it. */
float32_t player_survival_fall_damage(float64_t distance, bool on_bed);

/* What is in hand: anything that tells one item from another, what eating it gives, and 0 if it is no food. Changing it starts eating
 * over. Each tick before the buttons. */
void player_survival_hold_food(player_survival_t *survival, uint16_t item, uint32_t food, float32_t saturation);

/* A hit, from this module's own tick or from outside, on the thread its ticks are on. Within PLAYER_SURVIVAL_INVULNERABLE_TICKS of the last
 * one only what is more than that one counts, and only the difference. Wakes it, and may kill it. Returns the health it took. */
float32_t player_survival_hurt(player_survival_t *survival, float32_t amount, player_survival_cause_t cause);

/* Adds to what food is being worn down by. */
void player_survival_exhaust(player_survival_t *survival, float32_t amount);

/* Food and saturation from eating, up to their limits: what finishing a bite does. */
void player_survival_eat(player_survival_t *survival, uint32_t food, float32_t saturation);

/* Plan 006 phase 01: the body has just been put where its feet now are, by a teleport: it waits there, held and unharmed, until the
 * ground loads, then stands as kind says (player_survival_arrive), and takes no fall until it lands. */
void player_survival_arrive_begin(player_survival_t *survival, player_survival_arrive_t kind);

/* The wait: nothing until the column under the feet is loaded, then the body stands as player_survival_arrive_begin said. Each tick
 * before the body moves, on the map's thread. Never waits and never touches the heap. */
void player_survival_arrive(player_survival_t *survival, player_movement_t *movement, physics_t *physics,
                            const world_map_t *map);

/* Whether what wants to move this body counts for nothing now: it is dead, asleep, or waiting for the ground it has come back to. */
bool player_survival_holds_body(const player_survival_t *survival);

/* Whether what it has eaten, and is eating, lets it sprint. */
bool player_survival_can_sprint(const player_survival_t *survival);

/* Before the hands' tick, with the map's thread: a press brings a dead body back and wakes a sleeping one; the right button on a bed uses
 * it and on food eats. What it used it clears, so that the hands see the rest. */
void player_survival_buttons(player_survival_t *survival, player_survival_buttons_t *buttons);

/* One tick, after the body's on the same thread: what the body now touches hurts, food wears down, it heals or starves, and it sleeps on.
 * mined_count is the hands', whose work tires it. Never waits and never touches the heap. */
void player_survival_tick(player_survival_t *survival, player_movement_t *movement, physics_t *physics,
                          const world_map_t *map, uint64_t mined_count);

/* Whether it has died since this was last asked. */
bool player_survival_died_take(player_survival_t *survival);

/* Whether a night has been slept through since this was last asked, and the day should go to PLAYER_SURVIVAL_MORNING. */
bool player_survival_morning_take(player_survival_t *survival);

#endif
