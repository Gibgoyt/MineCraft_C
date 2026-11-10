/* See run/src/player/interaction/README.md. */
#ifndef MC_RUN_PLAYER_INTERACTION_INDEX_H
#define MC_RUN_PLAYER_INTERACTION_INDEX_H

#include "player/camera/index.h"
#include "player/movement/index.h"
#include "types/index.h"
#include "types/sound_blob/index.h"
#include "world/edit/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Twice the most rows one block has, so that a block and a plant on it fit between two looks at this. */
    PLAYER_INTERACTION_DROPS = 32u,
    /* Ticks with nothing mined after a break that took more than one tick: Minecraft's published delay. */
    PLAYER_INTERACTION_MINE_DELAY_TICKS = 5u,
    /* With the button held, a block goes down this often: Minecraft's published rate. */
    PLAYER_INTERACTION_PLACE_EVERY_TICKS = 4u,
    PLAYER_INTERACTION_HAND_MAX = 64u,
    /* A short hit, this often, while the button stays on one block. Minecraft's published rate. */
    PLAYER_INTERACTION_HIT_EVERY_TICKS = 4u,
    /* What one tick can make is a hit or a break, a place, and a log of a tree coming down. Whoever takes them does so each tick. */
    PLAYER_SOUNDS_RING = 8u,
    /* How many plants standing on one another go with what they stood on. Sugar cane is the tallest, by far. */
    PLAYER_INTERACTION_STACK_MAX = 32u,
    /* player_interaction_ticks for what can never be mined. */
    PLAYER_INTERACTION_TICKS_NEVER = UINT32_MAX,
};

/* What came out of a broken block, waiting for whoever spawns it in the world. */
typedef struct player_drop_t {
    float64_t position[COLLISION_AXES]; /* 0: the middle of the cell it came from */
    uint16_t item;                      /* 24: into block_drop_items */
    uint8_t count;                      /* 26 */
    uint8_t _pad0[5];                   /* 27 */
} player_drop_t;
static_assert(sizeof(player_drop_t) == 32);

/* Something the hands did that can be heard. */
typedef struct player_sound_t {
    int32_t cell[COLLISION_AXES]; /* 0: where it happened */
    uint8_t family;               /* 12: sound_family_t */
    uint8_t kind;                 /* 13: sound_kind_t */
    uint8_t _pad0[2];             /* 14 */
} player_sound_t;
static_assert(sizeof(player_sound_t) == 16);

/* Those waiting for whoever plays them. A ring, as drops are, that loses its oldest if nobody comes. It is apart from player_interaction_t, as it is not saved, and nothing
 * else depends on it: the same world and buttons give the same drops and the same bytes with sound or without. */
typedef struct player_sounds_t {
    player_sound_t sounds[PLAYER_SOUNDS_RING]; /* 0 */
    uint64_t lost_count;                       /* 128: pushed out before anyone took them */
    uint32_t head;                             /* 136 */
    uint32_t count;                            /* 140 */
} player_sounds_t;
static_assert(sizeof(player_sounds_t) == 144);

/* What a player's hands are doing. Plain numbers, so that the same world and buttons always give the same bytes. The sim's. */
typedef struct player_interaction_t {
    player_drop_t drops[PLAYER_INTERACTION_DROPS]; /* 0: a ring */
    uint64_t random;                               /* 1024: splitmix64's state */
    uint64_t mined_count;                          /* 1032 */
    uint64_t placed_count;                         /* 1040 */
    uint64_t drops_lost_count;                     /* 1048: pushed out of a full ring before anyone took them */
    uint64_t no_memory_count;                      /* 1056: edits the section pool refused */
    uint64_t progress;                             /* 1064: on the block being mined, in the units of a tick's work */
    uint64_t picked_up_count;                      /* 1072: items that have come into the hand from the ground */
    int32_t cell[COLLISION_AXES];                  /* 1080: what is being mined */
    uint32_t drop_head;                            /* 1092: the oldest in the ring */
    uint32_t drop_count;                           /* 1096 */
    uint16_t mining_block;                         /* 1100 */
    uint16_t hand_block;                           /* 1102: what placing puts down, meaningful with a hand_count */
    uint16_t last_mined;                           /* 1104: air until one is */
    uint8_t hand_count;                            /* 1106 */
    uint8_t held_tool;                             /* 1107: a block_tool_t */
    uint8_t held_tier;                             /* 1108: a block_tier_t */
    uint8_t mining;                                /* 1109: cell and mining_block mean something */
    uint8_t mine_delay;                            /* 1110: ticks until it may mine again */
    uint8_t place_delay;                           /* 1111: ticks until it may place again */
    uint32_t held_speed_milli;                     /* 1112: on its own blocks, if not 0, over what its tier says */
    uint8_t hit_wait;                              /* 1116: ticks of mining until the next hit is heard */
    uint8_t turns;                                 /* 1117: quarter turns R has given what is in the hand, 0 to 3 */
    uint8_t _pad0[34];                             /* 1118 */
} player_interaction_t;
static_assert(sizeof(player_interaction_t) == 1152);
static_assert(alignof(player_interaction_t) == 8);

/* See run/include/player/felling/index.h, which needs what is in here. */
typedef struct player_felling_t player_felling_t;

/* What one tick is given. All of it is read and none of it kept. */
typedef struct player_interaction_input_t {
    const player_target_t *borrowed_target;     /* 0: what the player sees outlined */
    const player_movement_t *borrowed_movement; /* 8: the body, which nothing may be placed into */
    player_felling_t *borrowed_felling;         /* 16: what a log cut sets going, or nullptr for one block */
    uint8_t attack;                             /* 24: the left button */
    uint8_t place;                              /* 25: the right button */
    uint8_t look_facing;                        /* 26: a block_facing_t: the nearest of the four to where the player looks */
    uint8_t _pad0[5];                           /* 27 */
    player_sounds_t *borrowed_sounds; /* 32: what it does that can be heard goes here, or nullptr for silent hands */
} player_interaction_input_t;
static_assert(sizeof(player_interaction_input_t) == 40);

enum : uint32_t {
    /* The most cells one block takes when placed: a bench is two. */
    PLAYER_PLACE_CELLS = 2u,
};

/* What a hand would do with the right button now. */
typedef enum player_place_status_t : uint8_t {
    PLAYER_PLACE_NONE = 0, /* nothing, nowhere: an empty hand, no face aimed at, or a torch aimed at an underside */
    PLAYER_PLACE_OK = 1,   /* it would go down */
    PLAYER_PLACE_BLOCKED =
        2, /* it has a place that cannot take it: something there, the body in the way, no ground under what
                                 needs it, no room for a bench's other half, or a column or height that is not loaded or played */
} player_place_status_t;

/* What and where a hand would place, worked out without changing anything: for placing, and for a preview to draw. With
 * PLAYER_PLACE_NONE only status means anything. With BLOCKED the cells and voxels are what it would have been. */
typedef struct player_place_plan_t {
    int32_t cells[PLAYER_PLACE_CELLS][COLLISION_AXES]; /* 0: the cell aimed into, then a bench's other half */
    uint16_t voxels[PLAYER_PLACE_CELLS];               /* 24: what goes in each, with its facing: types/block_state/ */
    uint16_t was_there;                                /* 28: what the first cell holds now, id and state */
    uint8_t count;                                     /* 30: cells it takes, 1 or 2, and 0 with nothing to place */
    uint8_t status;                                    /* 31: player_place_status_t */
    uint8_t facing;                                    /* 32: the block_facing_t it goes down with */
    uint8_t _pad0[3];                                  /* 33 */
} player_place_plan_t;
static_assert(sizeof(player_place_plan_t) == 36);

/* Empty hands, nothing under way, and random numbers from this seed. */
void player_interaction_start(player_interaction_t *interaction, uint64_t seed);

/* Whether a block broken with this tool gives its drops: a block with a tier wants its own tool, of that tier or better. */
bool player_interaction_harvests(uint16_t block, uint8_t tool, uint8_t tier);

/* How many times faster than a bare hand this tool breaks this block, in thousandths: 1000 unless it is the block's own tool. */
uint32_t player_interaction_speed_milli(uint16_t block, uint8_t tool, uint8_t tier);

/* How many ticks of holding the button break this block, at least 1. PLAYER_INTERACTION_TICKS_NEVER if nothing does. */
uint32_t player_interaction_ticks(uint16_t block, uint8_t tool, uint8_t tier, bool on_ground, bool eyes_under_water);

/* How much of the block being mined is done, from 0 to 1, for a breaking overlay. 0 if none is. */
float32_t player_interaction_progress(const player_interaction_t *interaction);

/* From now on it mines with this. Stage 13.3's tools come in here: until then a hand is BLOCK_TOOL_NONE and BLOCK_TIER_NONE. */
void player_interaction_hold_tool(player_interaction_t *interaction, uint8_t tool, uint8_t tier);

/* What the tool held mines its own blocks at, in thousandths, whatever its tier says: gold, which breaks what wood breaks, far faster. 0
 * goes by its tier. Nothing else changes: not what it harvests, nor what a sword or shears do. */
void player_interaction_hold_speed(player_interaction_t *interaction, uint32_t speed_milli);

/* One tick, on the thread holding the map: the left button mines the target, or if it is not held the right button places what is
 * in the hand against it. The target may be a tick old, and a block that has changed since is not touched. Never waits and never
 * touches the heap. */
void player_interaction_tick(player_interaction_t *interaction, world_edit_t *edit,
                             const player_interaction_input_t *input);

/* What the right button would put down and where, as player_interaction_tick would place it with input->place, but changing nothing:
 * the target, what is in the hand, the facing from input->look_facing turned by R's turns, and whether the world and the body let it
 * go there. Returns out->status. Any thread holding the map; reads only. */
player_place_status_t player_interaction_place_plan(const player_interaction_t *interaction, const world_map_t *map,
                                                    const player_interaction_input_t *input, player_place_plan_t *out);

/* R: what is in the hand turns a quarter clockwise, seen from above, before it is placed. It stays turned until the hand holds
 * something else. */
void player_interaction_turn(player_interaction_t *interaction);

/* Rolls a block's table, as breaking it with the right tool would, and adds what comes out to the ring. */
void player_interaction_drops_spawn(player_interaction_t *interaction, uint16_t block, int32_t x, int32_t y, int32_t z);

/* What stood on a block just broken goes with it, and gives what it gives to a hand. */
void player_interaction_stack_break(player_interaction_t *interaction, world_edit_t *edit, int32_t x, int32_t y,
                                    int32_t z);

/* The oldest drop waiting, which is now the caller's. False if there is none. */
bool player_interaction_drop_next(player_interaction_t *interaction, player_drop_t *out);

/* A block's sound of this kind, at this cell, for whoever plays them. A block whose family is none is not heard. */
void player_sounds_post(player_sounds_t *sounds, uint16_t block, sound_kind_t kind, int32_t x, int32_t y, int32_t z);

/* The oldest sound waiting, which is now the caller's. False if there is none. */
bool player_sounds_next(player_sounds_t *sounds, player_sound_t *out);

/* In the hand, up to PLAYER_INTERACTION_HAND_MAX. A different block takes the place of what is there, as there is one slot, and
 * starts with no turns. */
void player_interaction_hand_give(player_interaction_t *interaction, uint16_t block, uint32_t count);

/* What the hand holds from now on, whatever it held. What game makes it hold is the hotbar slot in hand. */
void player_interaction_hand_set(player_interaction_t *interaction, uint16_t block, uint32_t count);

/* What a picked up item goes into in a run with no inventory: how many of count fit in the hand, which are now in it. None if the
 * item is no block, or the hand holds a different one, so that what it has is not lost and the item stays on the ground. */
uint32_t player_interaction_hand_take(player_interaction_t *interaction, uint16_t item, uint32_t count);

#endif
