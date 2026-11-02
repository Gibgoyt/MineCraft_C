/* See run/src/player/interaction/README.md. */
#ifndef MC_RUN_PLAYER_INTERACTION_INDEX_H
#define MC_RUN_PLAYER_INTERACTION_INDEX_H

#include "player/camera/index.h"
#include "player/movement/index.h"
#include "types/index.h"
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
    uint8_t _pad0[36];                             /* 1116 */
} player_interaction_t;
static_assert(sizeof(player_interaction_t) == 1152);
static_assert(alignof(player_interaction_t) == 8);

/* What one tick is given. All of it is read and none of it kept. */
typedef struct player_interaction_input_t {
    const player_target_t *borrowed_target;     /* 0: what the player sees outlined */
    const player_movement_t *borrowed_movement; /* 8: the body, which nothing may be placed into */
    uint8_t attack;                             /* 16: the left button */
    uint8_t place;                              /* 17: the right button */
    uint8_t _pad0[6];                           /* 18 */
} player_interaction_input_t;
static_assert(sizeof(player_interaction_input_t) == 24);

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

/* Rolls a block's table, as breaking it with the right tool would, and adds what comes out to the ring. */
void player_interaction_drops_spawn(player_interaction_t *interaction, uint16_t block, int32_t x, int32_t y, int32_t z);

/* The oldest drop waiting, which is now the caller's. False if there is none. */
bool player_interaction_drop_next(player_interaction_t *interaction, player_drop_t *out);

/* In the hand, up to PLAYER_INTERACTION_HAND_MAX. A different block takes the place of what is there, as there is one slot. */
void player_interaction_hand_give(player_interaction_t *interaction, uint16_t block, uint32_t count);

/* What the hand holds from now on, whatever it held. What game makes it hold is the hotbar slot in hand. */
void player_interaction_hand_set(player_interaction_t *interaction, uint16_t block, uint32_t count);

/* What a picked up item goes into in a run with no inventory: how many of count fit in the hand, which are now in it. None if the
 * item is no block, or the hand holds a different one, so that what it has is not lost and the item stays on the ground. */
uint32_t player_interaction_hand_take(player_interaction_t *interaction, uint16_t item, uint32_t count);

#endif
