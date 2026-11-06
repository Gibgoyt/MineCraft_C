/* See run/src/player/movement/README.md. */
#ifndef MC_RUN_PLAYER_MOVEMENT_INDEX_H
#define MC_RUN_PLAYER_MOVEMENT_INDEX_H

#include "physics/index.h"
#include "types/index.h"
#include "world/map/index.h"

#include <stddef.h>
#include <stdint.h>

/* The body, in blocks. */
#define PLAYER_MOVEMENT_WIDTH 0.6f64
#define PLAYER_MOVEMENT_HEIGHT_STANDING 1.8f64
#define PLAYER_MOVEMENT_HEIGHT_CROUCHED 1.5f64
#define PLAYER_MOVEMENT_EYE_STANDING 1.62f64
#define PLAYER_MOVEMENT_EYE_CROUCHED 1.27f64

typedef enum player_movement_mode_t : uint8_t {
    PLAYER_MOVEMENT_MODE_GROUND = 0,
    PLAYER_MOVEMENT_MODE_AIR = 1,
    PLAYER_MOVEMENT_MODE_WATER = 2,
    PLAYER_MOVEMENT_MODE_LAVA = 3,
    PLAYER_MOVEMENT_MODE_CLIMB = 4,
} player_movement_mode_t;

/* What the player wants for one tick, wherever it came from. */
typedef struct player_intent_t {
    float32_t yaw;    /* 0: radians, where it looks: see render_camera_t */
    int8_t forward;   /* 4: 1 forward, -1 back */
    int8_t right;     /* 5: 1 right, -1 left */
    uint8_t jump;     /* 6: also: rise in a fluid, and climb */
    uint8_t sneak;    /* 7: also: sink in a fluid, and hold on a ladder */
    uint8_t sprint;   /* 8 */
    uint8_t _pad0[3]; /* 9 */
} player_intent_t;
static_assert(sizeof(player_intent_t) == 12);

/* All that a body is. Plain numbers, so that the same world and intents always give the same bytes. */
typedef struct player_movement_t {
    float64_t position[3];      /* 0: the middle of the soles of its feet */
    float64_t previous[3];      /* 24: position at the start of the last tick */
    float64_t velocity[3];      /* 48: blocks per tick */
    float64_t fall_distance;    /* 72: how far it has fallen without landing, for Stage 13.4 */
    float64_t landed_fall;      /* 80: how far it fell, on the tick it landed; otherwise 0 */
    float64_t eye_height;       /* 88: above position */
    float64_t previous_eye;     /* 96: eye_height at the start of the last tick */
    uint8_t on_ground;          /* 104 */
    uint8_t mode;               /* 105: player_movement_mode_t */
    uint8_t crouched;           /* 106 */
    uint8_t sprinting;          /* 107 */
    uint8_t eyes_under_water;   /* 108: for Stage 13.4 */
    uint8_t waiting_for_column; /* 109: its column is not loaded, so it has not moved */
    uint8_t _pad0[2];           /* 110 */
} player_movement_t;
static_assert(sizeof(player_movement_t) == 112);

/* Plan 004 phase 08: storms push the body, gently (master plan Q4). Over PLAYER_WIND_THRESHOLD a body standing on the ground
 * drifts downwind at PLAYER_WIND_SHARE of the wind over it: 2 blocks a second in a 60 blocks a second hurricane, 0.8 in a 30 storm.
 * In the air a fifth of the push, behind a solid block within 3 blocks upwind a quarter, and none in water, lava or on a ladder. */
#define PLAYER_WIND_THRESHOLD 10.0f64     /* blocks a second: trade winds and westerlies push nothing */
#define PLAYER_WIND_SHARE 0.04f64         /* drift on the ground, per block a second over the threshold */
#define PLAYER_WIND_AIR_SHARE 0.2f64      /* of the ground push: the game's air against ground acceleration */
#define PLAYER_WIND_SHELTER_SHARE 0.25f64 /* of the push */
enum : uint32_t {
    PLAYER_WIND_SHELTER_BLOCKS = 3u, /* blocks upwind a solid block shelters from */
};

/* A body standing still at this place, with its feet there. */
void player_movement_start(player_movement_t *movement, float64_t x, float64_t y, float64_t z);

/* The box of a body at this place, this tall. */
void player_movement_box(const float64_t position[3], float64_t height, collision_box_t *out);

/* One tick, at 20 Hz. Uses physics's arena, and the map's columns, so it runs on the thread holding the map. While the column
 * its feet are in is not loaded it stays exactly where it is. */
void player_movement_tick(player_movement_t *movement, physics_t *physics, const world_map_t *map,
                          const player_intent_t *intent);

/* The same in a wind of (wind_u, wind_v) blocks a second, x east and y north: what player_movement_tick is with 0, 0, to the byte.
 * The wind is an input like the intent, so the same world, intents and winds still give the same bytes. */
void player_movement_tick_wind(player_movement_t *movement, physics_t *physics, const world_map_t *map,
                               const player_intent_t *intent, float32_t wind_u, float32_t wind_v);

#endif
