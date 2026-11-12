/* See run/src/player/README.md. */
#ifndef MC_RUN_PLAYER_INDEX_H
#define MC_RUN_PLAYER_INDEX_H

#include "machine/index.h"
#include "physics/index.h"
#include "player/camera/index.h"
#include "player/combat/index.h"
#include "player/felling/index.h"
#include "player/interaction/index.h"
#include "player/movement/index.h"
#include "player/survival/index.h"
#include "render/frame/index.h"
#include "world/map/index.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum : int64_t {
    PLAYER_TICK_NS = 1000000000 / MC_GAME_TICK_HZ,
    /* An intent older than this means no one is at the controls: main has stopped, and most likely gone to another VT. */
    PLAYER_INTENT_STALE_NS = 250000000,
};

/* How far past its body the player picks things up: the published numbers. */
#define PLAYER_PICKUP_REACH_ACROSS 1.0f64
#define PLAYER_PICKUP_REACH_UP 0.5f64

/* The two buttons, in hands_word and hands_latch, and R, only ever in hands_latch: a press, not something held. */
enum : uint32_t {
    PLAYER_HAND_ATTACK = 1u,
    PLAYER_HAND_PLACE = 2u,
    PLAYER_HAND_TURN = 4u,
};

/* In player_t.animal_use. */
enum : uint8_t {
    PLAYER_ANIMAL_USE_HELD = 1u, /* the right button was down last tick */
    PLAYER_ANIMAL_USE_ON =
        2u, /* it went down on an animal that yields to what is in hand: until it is let go the hands do nothing */
};

/* Plan 006 phase 01: what main may ask of the sim through command_word. The kind is the word's low byte. */
typedef enum player_command_kind_t : uint8_t {
    PLAYER_COMMAND_NONE = 0,
    PLAYER_COMMAND_TP_EXACT = 1,  /* feet at the target; out of rock to the first room above if it is inside it */
    PLAYER_COMMAND_TP_GROUND = 2, /* x and y of the target, standing on the top ground there, once it loads */
    PLAYER_COMMAND_FLY = 3,       /* fly mode on if it is off, and off if it is on */
    PLAYER_COMMAND_SPEED = 4,     /* the speed factor, PLAYER_MOVEMENT_FACTOR_MIN to _MAX, in the argument */
} player_command_kind_t;

enum : uint32_t {
    PLAYER_COMMAND_RELATIVE_X = 1u << 8, /* command_target[0] is a step from the feet */
    PLAYER_COMMAND_RELATIVE_Y = 1u << 9,
    PLAYER_COMMAND_RELATIVE_Z = 1u << 10,
    PLAYER_COMMAND_ARGUMENT_SHIFT = 16u,
    /* In flight_word. */
    PLAYER_FLIGHT_FLYING = 1u << 0,
    PLAYER_FLIGHT_SPEED_SHIFT = 8u,   /* bits 8-23: what it flies at, blocks a second, the factor in, to the nearest */
    PLAYER_FLIGHT_FACTOR_SHIFT = 24u, /* bits 24-31: the speed factor */
};

/* How far from 0 a teleport may put the feet across, in blocks: inside what a save keeps of a place (save/state, 1e9), with room. */
#define PLAYER_COMMAND_PLACE_MOST 999000000.0f64

/* One tick's worth of where the eye was and is and what it aims at, from sim to main. */
typedef struct player_view_t {
    float64_t previous[3];  /* 0 */
    float64_t current[3];   /* 24 */
    int64_t tick_ns;        /* 48: when this tick was due */
    uint64_t tick;          /* 56: which one, counting from 1, and 0 before any */
    player_target_t target; /* 64 */
} player_view_t;
static_assert(sizeof(player_view_t) == 160);

/* A body's health as main may see it, for a screen to draw. Goes across in one word, so that it stays whole. */
typedef struct player_vitals_t {
    float32_t health; /* 0 */
    uint16_t air;     /* 4: ticks left, and 0 when drowning */
    uint8_t food;     /* 6 */
    uint8_t state;    /* 7: player_survival_state_t */
} player_vitals_t;
static_assert(sizeof(player_vitals_t) == 8);

/* The player. There is one, under app_t. The sim writes everything up to the intent, and main the rest up to survival. */
typedef struct player_t {
    player_movement_t movement;                      /* 0: the sim's */
    physics_t *borrowed_physics;                     /* 112 */
    uint64_t tick_count;                             /* 120: the sim's */
    render_exchange_t views;                         /* 128: sim to main, over view_slots */
    player_view_t view_slots[RENDER_EXCHANGE_SLOTS]; /* 320 */
    player_target_t target;                          /* 800: the sim's: what it aims at, as of the last tick */
    player_interaction_t interaction;                /* 896: the sim's: mining, placing and what is in the hand */
    /* Main to sim. None of these carries a pointer or vouches for anything written before it, so all are relaxed. */
    _Atomic uint64_t intent_word;   /* 2048: the newest player_intent_t, packed */
    _Atomic int64_t intent_sent_ns; /* 2056 */
    _Atomic uint64_t look_word;     /* 2064: the newest yaw and pitch, packed */
    _Atomic uint32_t jump_latch;    /* 2072: a jump pressed since the sim last looked */
    uint32_t view_slot;             /* 2076: main's: the slot it reads */
    _Atomic uint32_t hands_word;    /* 2080: PLAYER_HAND_* held now */
    _Atomic uint32_t hands_latch;   /* 2084: PLAYER_HAND_* pressed since the sim last looked */
    /* Sim to main: the newest player_vitals_t, relaxed, as only what is on the screen depends on it. */
    _Atomic uint64_t vitals_word; /* 2088 */
    uint8_t hands_held_off;       /* 2096: the sim's: a screen has the buttons, so the hands do nothing */
    /* Sim to main, with vitals_word: the armour points worn, 0 to 20, relaxed for the same reason. */
    _Atomic uint8_t armor_points; /* 2097 */
    /* 2098: the sim's: PLAYER_ANIMAL_USE_*, the right button and an animal, as run/src/game/ shears, milks and holds the hands off */
    uint8_t animal_use;
    uint8_t _pad0[1]; /* 2099 */
    /* Sim to the thread that draws: movement.eyes_under_water, relaxed, as only the picture depends on it. */
    _Atomic uint32_t eyes_under_water; /* 2100 */
    player_combat_t combat;            /* 2104: the sim's: what the hand remembers of swinging */
    /* A line of its own: main writes the line above all the time. */
    player_survival_t survival; /* 2112: the sim's */
    /* Plan 006 phase 01: main's commands to the sim, a line of their own. A mailbox of one: main writes command_target only while
     * command_word is 0 (acquire), then the word (release); the sim reads the target after the word (acquire), then puts 0 back
     * (release). */
    float64_t command_target
        [3]; /* 2304: per axis, a place or, with its PLAYER_COMMAND_RELATIVE_* bit, a step from the feet */
    _Atomic uint32_t
        command_word; /* 2328: a player_command_kind_t, PLAYER_COMMAND_RELATIVE_* and an argument; 0 for none */
    _Atomic int32_t fly_wheel; /* 2332: wheel steps main has seen while flying, added up, for the sim to take */
    /* 2336: sim to main, relaxed, as only the HUD depends on it: PLAYER_FLIGHT_* bits, the fly speed and the speed factor */
    _Atomic uint32_t flight_word;
    uint32_t teleports; /* 2340: the sim's: teleports taken since the body was spawned */
    uint8_t _pad1[24];  /* 2344 */
} player_t;
static_assert(sizeof(player_t) == 2368);
static_assert(alignof(player_t) == MC_CPU_CACHE_LINE_BYTES);
static_assert(offsetof(player_t, views) == 128);
static_assert(offsetof(player_t, target) == 800);
static_assert(offsetof(player_t, interaction) == 896);
static_assert(offsetof(player_t, intent_word) == 2048);
static_assert(offsetof(player_t, survival) == 2112);
static_assert(offsetof(player_t, command_target) == 2304);
static_assert(offsetof(player_t, command_word) == 2328);
static_assert(offsetof(player_t, flight_word) == 2336);

/* Remembers what it moves with, and takes nothing. Physics must outlive it. */
void player_init(player_t *player, physics_t *physics);

/* Safe on a zeroed player_t. */
void player_deinit(player_t *player);

/* A body at rest, its feet at this place, with nothing wanted yet. Before the threads start. */
void player_spawn(player_t *player, float64_t x, float64_t y, float64_t z);

/* What the player wants now. Main, as often as it likes, with now_ns from the same clock as the ticks'. A jump pressed between
 * two ticks still jumps. Never waits. */
void player_send(player_t *player, const player_intent_t *intent, int64_t now_ns);

/* Which way main last said the player looks, turned in radians. The sim. */
float32_t player_yaw_sent(const player_t *player);

/* Whether the keys main sent last say to sneak. The sim. */
bool player_sneak_sent(const player_t *player);

/* Where the player looks now, for what it aims at. Main, as often as it likes. A yaw and pitch stay a pair. Never waits. */
void player_look_send(player_t *player, float32_t yaw, float32_t pitch);

/* What the two buttons are doing now. Main, as often as it likes, right after player_send, whose time also says how fresh this is.
 * A click between two ticks still counts for one tick. Never waits. */
void player_hands_send(player_t *player, bool attack, bool place);

/* R was pressed: what is in the hand turns a quarter before it is placed, player_interaction_turn, on the next tick. Main. Two presses
 * between two ticks are one. Never waits. */
void player_turn_send(player_t *player);

/* The hands' tick, on what player_tick last aimed at, so on the block the player sees outlined: the sim, or whichever one thread holds
 * the map, right before player_tick, so that the body and the next aim both meet the world as this leaves it. Never waits and never
 * touches the heap. What it broke waits in the interaction's ring, for whoever puts items in the world. */
void player_interact_tick(player_t *player, world_edit_t *edit, int64_t tick_ns);

/* The same, with somewhere for a log the hands cut to set the rest of its tree going, and somewhere for what can be heard: player_interact_tick is this with nullptrs, and an
 * log cut is one log, silently.
 * The caller then runs player_felling_tick. */
void player_interact_fell_tick(player_t *player, world_edit_t *edit, player_felling_t *felling, player_sounds_t *sounds,
                               int64_t tick_ns);

/* What the right button would put down and where, as player_interact_tick would place it on the next tick: player_interaction_place_plan
 * with the same input, from what player_tick aimed at last and the yaw main last sent. With a screen holding the buttons, nothing. For a
 * preview of placing (plan 005 phase 10): the sim, after player_tick. Changes nothing, never waits and never touches the heap. */
player_place_status_t player_place_preview(const player_t *player, const world_map_t *map, player_place_plan_t *out);

/* The hand's tick, right before player_interact_tick on the same thread: a press of the left button on a body in reach returns true and
 * says what it does, for the caller to apply. While the button is on a body the hands that follow do not mine. A run that does not call this
 * has hands that only mine. Never waits and never touches the heap. */
bool player_attack_tick(player_t *player, const entity_store_t *store, const world_map_t *map,
                        const player_combat_weapon_t *weapon, int64_t tick_ns, player_combat_swing_t *out);

/* That swing has landed: it tires the body, and a sprint's push stops the sprint. */
void player_attack_landed(player_t *player, const player_combat_swing_t *swing);

/* The body the eye looks at, as a swing would find it: within PLAYER_COMBAT_REACH, in front of any block, by the newest yaw and
 * pitch. ENTITY_HANDLE_NONE if none. For the right button on an animal. */
entity_handle_t player_body_aimed(const player_t *player, const entity_store_t *store, const world_map_t *map);

/* What an item's box has to overlap to be picked up, with the body where it now is. The thread its ticks are on. */
void player_pickup_reach(const player_t *player, collision_box_t *out);

/* Plan 006 phase 01: main asks the sim for a teleport, fly mode or a speed factor (player_command_kind_t). target is for a teleport,
 * a place per axis or, with its PLAYER_COMMAND_RELATIVE_* bit in relative, a step from the feet; argument is the speed factor. False,
 * with nothing sent, while the last one is still waiting for the sim: a mailbox of one. Main. Never waits. */
bool player_command_send(player_t *player, player_command_kind_t kind, const float64_t target[3], uint32_t relative,
                         uint32_t argument);

/* The sim, first in its tick: what main sent, done. A teleport puts the feet there (held inside PLAYER_COMMAND_PLACE_MOST across and
 * the gameplay heights) with player_movement_start, so that nothing is drawn smeared between, and the body then waits, held and
 * unharmed, for its ground (player_survival_arrive_begin). Refused by a body that is dead or asleep. Returns the kind done, or
 * PLAYER_COMMAND_NONE. Also takes the wheel main has sent while flying. Never waits. */
player_command_kind_t player_command_take(player_t *player);

/* Main: wheel steps while flying, for the fly speed. Never waits. */
void player_fly_wheel_send(player_t *player, int32_t steps);

/* Main: flying, what it flies at in blocks a second (the factor in), and the speed factor, as of the last tick, from flight_word.
 * Never waits. */
void player_flight_read(const player_t *player, bool *out_flying, uint32_t *out_speed, uint32_t *out_factor);

/* One tick. The sim, or whichever one thread holds the map, with tick_ns the time this tick was due. Never waits and never
 * touches the heap. */
void player_tick(player_t *player, const world_map_t *map, int64_t tick_ns);
/* The same in a wind, blocks a second, x east and y north, which the sim works out from the climate (plan 004 phase 08):
 * player_tick is this with 0, 0. */
void player_tick_wind(player_t *player, const world_map_t *map, int64_t tick_ns, float32_t wind_u, float32_t wind_v);

/* One tick of what keeps the body going, right after player_tick on the same thread, so on what the body has just done and now touches.
 * Tests of how a body moves can leave it out, and then nothing ever hurts it. Never waits and never touches the heap. */
void player_survive_tick(player_t *player, const world_map_t *map);

/* The body's health as of the last tick. Main, as often as it likes. Never waits. */
player_vitals_t player_vitals_read(const player_t *player);

/* Puts the body's health and the armour it wears where main reads them, from survival as it stands. The sim's, after each tick;
 * and whoever has just set survival by hand, such as a save being read, before the threads start. */
void player_vitals_publish(player_t *player);

/* The armour points worn as of the last tick, 0 to 20, whole, as run/src/game/ last told survival. Main. Never waits. */
uint32_t player_armor_points_read(const player_t *player);

/* Whether the eyes were under water as of the last tick. Any thread. */
bool player_eyes_under_water_read(const player_t *player);

/* Where the eye is to be drawn at now_ns: from a tick's start to its end, over the tick after it was due, so that a picture
 * moves smoothly at any rate and is never more than a tick behind. Main. Never waits. */
void player_eye_at(player_t *player, int64_t now_ns, float64_t out[3]);

/* What the tick player_eye_at last read aims at. Main. Stays whole until player_eye_at is next called. */
const player_target_t *player_target_view(const player_t *player);

#endif
