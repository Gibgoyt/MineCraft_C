/* See run/src/save/state/README.md. */
#ifndef MC_RUN_SAVE_STATE_INDEX_H
#define MC_RUN_SAVE_STATE_INDEX_H

#include "arena/index.h"
#include "entity/index.h"
#include "file/index.h"
#include "item/index.h"
#include "mob/index.h"
#include "mob/spawn/index.h"
#include "player/index.h"
#include "save/names/index.h"
#include "save/state/translate/index.h"
#include "save/world/index.h"

#include <stddef.h>
#include <stdint.h>

enum : uint32_t {
    /* Raised by any change an older reader could not read. A file of another version is refused and left alone. Version 2: a file ends in the names of
     * the tables its numbers are places in, and a build with other tables translates them. */
    SAVE_STATE_VERSION = 2u,
    SAVE_STATE_VERSION_FIRST_WITH_NAMES = 2u,
    /* The slot tables a file's bodies come with. */
    SAVE_STATE_SLOT_TABLES = 3u,
    /* What one body comes to in a file: see save_state_body_fields. */
    SAVE_STATE_BODY_BYTES = 156u,
};

/* A part of a file. Each is there at most once, and one the game has no use for is not there at all. */
typedef enum save_state_section_id_t : uint32_t {
    SAVE_STATE_SECTION_TIME = 1,     /* one save_state_time_t */
    SAVE_STATE_SECTION_PLAYER = 2,   /* one save_state_player_t */
    SAVE_STATE_SECTION_ITEMS = 3,    /* one save_state_items_t */
    SAVE_STATE_SECTION_CHESTS = 4,   /* container_chest_t, as many as fit */
    SAVE_STATE_SECTION_FURNACES = 5, /* smelting_furnace_t, as many as fit */
    SAVE_STATE_SECTION_DROPS = 6,    /* one save_state_drops_t, then its item_drop_t */
    SAVE_STATE_SECTION_ARROWS = 7,   /* one save_state_arrows_t, then its projectile_t */
    SAVE_STATE_SECTION_BODIES = 8,   /* one save_state_bodies_t, three slot tables, then each array for count bodies */
    SAVE_STATE_SECTION_SPAWN = 9,    /* one save_state_spawn_t, then the idle counts */
    SAVE_STATE_SECTION_MINDS = 10,   /* one save_state_minds_t, then zombie, skeleton, creeper and combat bodies */
    SAVE_STATE_SECTION_NAMES =
        11, /* the lists of run/src/save/names/, one after another: what every number here means */
    SAVE_STATE_SECTION_COUNT = 11,
} save_state_section_id_t;

/* ---- The file: all little-endian, as written by the only machine this builds for ------------------------------------ */

/* At the start of <world>/state.<generation>. After it, section_count sections, each padded to 8 bytes. */
typedef struct save_state_header_t {
    uint8_t magic[8];        /* 0: "MCSTATE1" */
    uint64_t seed;           /* 8 */
    uint64_t generation;     /* 16: the save of the blocks this goes with: the N of its name */
    uint64_t table_hash;     /* 24: of the names in the last section, as save_state_table_hash does a build's */
    uint64_t sections_bytes; /* 32: what follows this header */
    uint64_t sections_hash;  /* 40: of those bytes */
    uint32_t version;        /* 48: SAVE_STATE_VERSION */
    uint32_t header_bytes;   /* 52: sizeof this */
    uint32_t section_count;  /* 56 */
    uint8_t _pad0[4];        /* 60 */
    uint64_t checksum;       /* 64: of the 64 bytes before this */
} save_state_header_t;
static_assert(sizeof(save_state_header_t) == 72);
static_assert(alignof(save_state_header_t) == 8);
static_assert(offsetof(save_state_header_t, magic) == 0);
static_assert(offsetof(save_state_header_t, seed) == 8);
static_assert(offsetof(save_state_header_t, generation) == 16);
static_assert(offsetof(save_state_header_t, table_hash) == 24);
static_assert(offsetof(save_state_header_t, sections_bytes) == 32);
static_assert(offsetof(save_state_header_t, sections_hash) == 40);
static_assert(offsetof(save_state_header_t, version) == 48);
static_assert(offsetof(save_state_header_t, header_bytes) == 52);
static_assert(offsetof(save_state_header_t, section_count) == 56);
static_assert(offsetof(save_state_header_t, _pad0) == 60);
static_assert(offsetof(save_state_header_t, checksum) == 64);

/* Before each section's bytes. */
typedef struct save_state_section_t {
    uint64_t bytes;   /* 0: of what follows, before its padding */
    uint32_t id;      /* 8: a save_state_section_id_t */
    uint8_t _pad0[4]; /* 12 */
} save_state_section_t;
static_assert(sizeof(save_state_section_t) == 16);
static_assert(alignof(save_state_section_t) == 8);
static_assert(offsetof(save_state_section_t, bytes) == 0);
static_assert(offsetof(save_state_section_t, id) == 8);
static_assert(offsetof(save_state_section_t, _pad0) == 12);

/* How far a world has run. */
typedef struct save_state_time_t {
    uint64_t world_ticks; /* 0 */
    uint32_t day_units;   /* 8: of 2^32 in a day */
    float32_t yaw;        /* 12: where the player looks, which main holds and not the body */
    float32_t pitch;      /* 16 */
    uint8_t _pad0[4];     /* 20 */
} save_state_time_t;
static_assert(sizeof(save_state_time_t) == 24);
static_assert(alignof(save_state_time_t) == 8);
static_assert(offsetof(save_state_time_t, world_ticks) == 0);
static_assert(offsetof(save_state_time_t, day_units) == 8);
static_assert(offsetof(save_state_time_t, yaw) == 12);
static_assert(offsetof(save_state_time_t, pitch) == 16);
static_assert(offsetof(save_state_time_t, _pad0) == 20);

/* The sim's plain numbers of a player. What it aims at is found again by its next tick. */
typedef struct save_state_player_t {
    player_movement_t movement;       /* 0 */
    player_survival_t survival;       /* 112 */
    player_interaction_t interaction; /* 304 */
    player_combat_t combat;           /* 1456 */
    uint64_t tick_count;              /* 1464 */
} save_state_player_t;
static_assert(sizeof(save_state_player_t) == 1472);
static_assert(alignof(save_state_player_t) == 8);
static_assert(offsetof(save_state_player_t, movement) == 0);
static_assert(offsetof(save_state_player_t, survival) == 112);
static_assert(offsetof(save_state_player_t, interaction) == 304);
static_assert(offsetof(save_state_player_t, combat) == 1456);
static_assert(offsetof(save_state_player_t, tick_count) == 1464);

/* All a player has about them, and each store's books. */
typedef struct save_state_items_t {
    crafting_grid_t crafting_hands;      /* 0 */
    crafting_grid_t crafting_table;      /* 96 */
    equipment_t equipment;               /* 192 */
    uint64_t added_count;                /* 240 */
    uint64_t removed_count;              /* 248 */
    uint64_t refused_count;              /* 256 */
    uint64_t spilled_count;              /* 264 */
    uint64_t chests_refused_count;       /* 272 */
    uint64_t smelted_count;              /* 280 */
    uint64_t burned_count;               /* 288 */
    uint64_t furnaces_refused_count;     /* 296 */
    item_stack_t slots[INVENTORY_SLOTS]; /* 304 */
    item_stack_t held;                   /* 592: what the pointer carries on a screen */
    uint8_t selected;                    /* 600 */
    uint8_t _pad0[7];                    /* 601 */
} save_state_items_t;
static_assert(sizeof(save_state_items_t) == 608);
static_assert(alignof(save_state_items_t) == 8);
static_assert(offsetof(save_state_items_t, crafting_hands) == 0);
static_assert(offsetof(save_state_items_t, crafting_table) == 96);
static_assert(offsetof(save_state_items_t, equipment) == 192);
static_assert(offsetof(save_state_items_t, added_count) == 240);
static_assert(offsetof(save_state_items_t, removed_count) == 248);
static_assert(offsetof(save_state_items_t, refused_count) == 256);
static_assert(offsetof(save_state_items_t, spilled_count) == 264);
static_assert(offsetof(save_state_items_t, chests_refused_count) == 272);
static_assert(offsetof(save_state_items_t, smelted_count) == 280);
static_assert(offsetof(save_state_items_t, burned_count) == 288);
static_assert(offsetof(save_state_items_t, furnaces_refused_count) == 296);
static_assert(offsetof(save_state_items_t, slots) == 304);
static_assert(offsetof(save_state_items_t, held) == 592);
static_assert(offsetof(save_state_items_t, selected) == 600);
static_assert(offsetof(save_state_items_t, _pad0) == 601);

typedef struct save_state_drops_t {
    uint64_t random;          /* 0 */
    uint64_t tick_count;      /* 8 */
    uint64_t dropped_count;   /* 16 */
    uint64_t picked_up_count; /* 24 */
    uint64_t despawned_count; /* 32 */
    uint64_t destroyed_count; /* 40 */
    uint64_t lost_count;      /* 48 */
    uint64_t merged_count;    /* 56 */
    uint32_t count;           /* 64 */
    uint32_t next_id;         /* 68 */
} save_state_drops_t;
static_assert(sizeof(save_state_drops_t) == 72);
static_assert(alignof(save_state_drops_t) == 8);
static_assert(offsetof(save_state_drops_t, random) == 0);
static_assert(offsetof(save_state_drops_t, tick_count) == 8);
static_assert(offsetof(save_state_drops_t, dropped_count) == 16);
static_assert(offsetof(save_state_drops_t, picked_up_count) == 24);
static_assert(offsetof(save_state_drops_t, despawned_count) == 32);
static_assert(offsetof(save_state_drops_t, destroyed_count) == 40);
static_assert(offsetof(save_state_drops_t, lost_count) == 48);
static_assert(offsetof(save_state_drops_t, merged_count) == 56);
static_assert(offsetof(save_state_drops_t, count) == 64);
static_assert(offsetof(save_state_drops_t, next_id) == 68);

typedef struct save_state_arrows_t {
    uint64_t random;        /* 0 */
    uint64_t fired_count;   /* 8 */
    uint64_t body_hits;     /* 16 */
    uint64_t target_hits;   /* 24 */
    uint64_t expired_count; /* 32 */
    uint64_t burned_count;  /* 40 */
    uint64_t lost_count;    /* 48 */
    uint64_t stuck_count;   /* 56 */
    uint32_t count;         /* 64 */
    uint8_t _pad0[4];       /* 68 */
} save_state_arrows_t;
static_assert(sizeof(save_state_arrows_t) == 72);
static_assert(alignof(save_state_arrows_t) == 8);
static_assert(offsetof(save_state_arrows_t, random) == 0);
static_assert(offsetof(save_state_arrows_t, fired_count) == 8);
static_assert(offsetof(save_state_arrows_t, body_hits) == 16);
static_assert(offsetof(save_state_arrows_t, target_hits) == 24);
static_assert(offsetof(save_state_arrows_t, expired_count) == 32);
static_assert(offsetof(save_state_arrows_t, burned_count) == 40);
static_assert(offsetof(save_state_arrows_t, lost_count) == 48);
static_assert(offsetof(save_state_arrows_t, stuck_count) == 56);
static_assert(offsetof(save_state_arrows_t, count) == 64);
static_assert(offsetof(save_state_arrows_t, _pad0) == 68);

/* The bodies' books, and those of blasts, whose numbers a body's world goes on with. */
typedef struct save_state_bodies_t {
    uint64_t spawned_count; /* 0 */
    uint64_t removed_count; /* 8 */
    uint64_t refused_count; /* 16 */
    uint64_t blast_random;  /* 24 */
    uint64_t broken_count;  /* 32 */
    uint32_t count;         /* 40 */
    uint32_t free_head;     /* 44 */
    uint32_t blast_count;   /* 48 */
    uint8_t _pad0[4];       /* 52 */
} save_state_bodies_t;
static_assert(sizeof(save_state_bodies_t) == 56);
static_assert(alignof(save_state_bodies_t) == 8);
static_assert(offsetof(save_state_bodies_t, spawned_count) == 0);
static_assert(offsetof(save_state_bodies_t, removed_count) == 8);
static_assert(offsetof(save_state_bodies_t, refused_count) == 16);
static_assert(offsetof(save_state_bodies_t, blast_random) == 24);
static_assert(offsetof(save_state_bodies_t, broken_count) == 32);
static_assert(offsetof(save_state_bodies_t, count) == 40);
static_assert(offsetof(save_state_bodies_t, free_head) == 44);
static_assert(offsetof(save_state_bodies_t, blast_count) == 48);
static_assert(offsetof(save_state_bodies_t, _pad0) == 52);

typedef struct save_state_spawn_t {
    uint64_t random;          /* 0 */
    uint64_t spawned_count;   /* 8 */
    uint64_t despawned_count; /* 16 */
    uint64_t refused_count;   /* 24 */
} save_state_spawn_t;
static_assert(sizeof(save_state_spawn_t) == 32);
static_assert(alignof(save_state_spawn_t) == 8);
static_assert(offsetof(save_state_spawn_t, random) == 0);
static_assert(offsetof(save_state_spawn_t, spawned_count) == 8);
static_assert(offsetof(save_state_spawn_t, despawned_count) == 16);
static_assert(offsetof(save_state_spawn_t, refused_count) == 24);

/* What the minds go on with. What a search last answered is not here: a mind asks again. */
typedef struct save_state_minds_t {
    uint64_t zombie_random;    /* 0 */
    uint64_t zombie_struck;    /* 8 */
    uint64_t zombie_died;      /* 16 */
    uint64_t zombie_dropped;   /* 24 */
    uint64_t skeleton_random;  /* 32 */
    uint64_t skeleton_shot;    /* 40 */
    uint64_t skeleton_died;    /* 48 */
    uint64_t skeleton_dropped; /* 56 */
    uint64_t creeper_random;   /* 64 */
    uint64_t creeper_burst;    /* 72 */
    uint64_t creeper_died;     /* 80 */
    uint64_t creeper_dropped;  /* 88 */
    uint64_t combat_landed;    /* 96 */
    uint64_t combat_killed;    /* 104 */
    uint64_t combat_knocked;   /* 112 */
} save_state_minds_t;
static_assert(sizeof(save_state_minds_t) == 120);
static_assert(alignof(save_state_minds_t) == 8);
static_assert(offsetof(save_state_minds_t, zombie_random) == 0);
static_assert(offsetof(save_state_minds_t, zombie_struck) == 8);
static_assert(offsetof(save_state_minds_t, zombie_died) == 16);
static_assert(offsetof(save_state_minds_t, zombie_dropped) == 24);
static_assert(offsetof(save_state_minds_t, skeleton_random) == 32);
static_assert(offsetof(save_state_minds_t, skeleton_shot) == 40);
static_assert(offsetof(save_state_minds_t, skeleton_died) == 48);
static_assert(offsetof(save_state_minds_t, skeleton_dropped) == 56);
static_assert(offsetof(save_state_minds_t, creeper_random) == 64);
static_assert(offsetof(save_state_minds_t, creeper_burst) == 72);
static_assert(offsetof(save_state_minds_t, creeper_died) == 80);
static_assert(offsetof(save_state_minds_t, creeper_dropped) == 88);
static_assert(offsetof(save_state_minds_t, combat_landed) == 96);
static_assert(offsetof(save_state_minds_t, combat_killed) == 104);
static_assert(offsetof(save_state_minds_t, combat_knocked) == 112);

/* ---- In memory ------------------------------------------------------------------------------------------------------ */

/* What a save is of. Any but the time may be nullptr, and then that part is not in the file, and what is in a file for it is not used. */
typedef struct save_state_parts_t {
    player_t *borrowed_player;   /* 0 */
    item_t *borrowed_item;       /* 8 */
    entity_t *borrowed_entity;   /* 16 */
    mob_t *borrowed_mob;         /* 24 */
    mob_spawn_t *borrowed_spawn; /* 32 */
    item_stack_t *borrowed_held; /* 40: what the pointer carries on a screen, which goes with borrowed_item */
    save_state_time_t time;      /* 48 */
} save_state_parts_t;
static_assert(sizeof(save_state_parts_t) == 72);
static_assert(alignof(save_state_parts_t) == 8);
static_assert(offsetof(save_state_parts_t, borrowed_player) == 0);
static_assert(offsetof(save_state_parts_t, borrowed_item) == 8);
static_assert(offsetof(save_state_parts_t, borrowed_entity) == 16);
static_assert(offsetof(save_state_parts_t, borrowed_mob) == 24);
static_assert(offsetof(save_state_parts_t, borrowed_spawn) == 32);
static_assert(offsetof(save_state_parts_t, borrowed_held) == 40);
static_assert(offsetof(save_state_parts_t, time) == 48);

/* One world's state, as last read or copied out. */
typedef struct save_state_t {
    uint8_t *borrowed_image;  /* 0: SAVE_STATE_IMAGE_BYTES: a whole file */
    char *borrowed_directory; /* 8: <world> */
    uint64_t seed;            /* 16 */
    uint64_t table_hash;      /* 24 */
    uint64_t image_bytes;     /* 32 */
    uint64_t files_written;   /* 40 */
    uint8_t open;             /* 48 */
    uint8_t kept;             /* 49: the image is a file that was read, and nothing has replaced it */
    uint8_t file_status;      /* 50: file_status_t: what the disk last said */
    uint8_t _pad0[5];         /* 51 */
    uint16_t *borrowed_maps;  /* 56: SAVE_STATE_TRANSLATE_MAPS_BYTES: for a file from other tables, on opening it */
} save_state_t;
static_assert(sizeof(save_state_t) == 64);
static_assert(alignof(save_state_t) == 8);
static_assert(offsetof(save_state_t, borrowed_image) == 0);
static_assert(offsetof(save_state_t, borrowed_directory) == 8);
static_assert(offsetof(save_state_t, seed) == 16);
static_assert(offsetof(save_state_t, table_hash) == 24);
static_assert(offsetof(save_state_t, image_bytes) == 32);
static_assert(offsetof(save_state_t, files_written) == 40);
static_assert(offsetof(save_state_t, open) == 48);
static_assert(offsetof(save_state_t, kept) == 49);
static_assert(offsetof(save_state_t, file_status) == 50);
static_assert(offsetof(save_state_t, _pad0) == 51);
static_assert(offsetof(save_state_t, borrowed_maps) == 56);

/* The most its section of names can be. */
enum : uint64_t {
    SAVE_STATE_NAMES_BYTES_MAX = (uint64_t)SAVE_NAMES_KIND_COUNT * SAVE_NAMES_TABLE_BYTES_MAX,
};

/* The largest a file can be, with every store full, and what save_state_open takes from its arena. */
enum : uint64_t {
    SAVE_STATE_IMAGE_BYTES =
        sizeof(save_state_header_t) + (uint64_t)SAVE_STATE_SECTION_COUNT * (sizeof(save_state_section_t) + 8u) +
        sizeof(save_state_time_t) + sizeof(save_state_player_t) + sizeof(save_state_items_t) +
        (uint64_t)CONTAINER_CHESTS * sizeof(container_chest_t) +
        (uint64_t)SMELTING_FURNACES * sizeof(smelting_furnace_t) + sizeof(save_state_drops_t) +
        (uint64_t)ITEM_DROP_CAPACITY * sizeof(item_drop_t) + sizeof(save_state_arrows_t) +
        (uint64_t)PROJECTILE_CAPACITY * sizeof(projectile_t) + sizeof(save_state_bodies_t) +
        3ull * ENTITY_STORE_CAPACITY * sizeof(uint32_t) + (uint64_t)ENTITY_STORE_CAPACITY * SAVE_STATE_BODY_BYTES +
        sizeof(save_state_spawn_t) + ENTITY_STORE_CAPACITY * sizeof(uint32_t) + sizeof(save_state_minds_t) +
        (uint64_t)ENTITY_STORE_CAPACITY * (sizeof(mob_zombie_body_t) + sizeof(mob_skeleton_body_t) +
                                           sizeof(mob_creeper_body_t) + sizeof(mob_combat_body_t)) +
        SAVE_STATE_NAMES_BYTES_MAX,
    SAVE_STATE_ARENA_BYTES = SAVE_STATE_IMAGE_BYTES + FILE_PATH_BYTES + SAVE_STATE_TRANSLATE_MAPS_BYTES + 3u * 64u,
};

/* Starts on <world_directory>/state.<generation>, which goes with that save of the blocks: reads it if it is there, and believes none of it until
 * all of it has been looked at. One saved with other tables is translated by name first, in memory: what this build has not got goes, and the log says so. With no such file a world starts its state afresh, and says so: a save of the blocks only ever comes after its
 * state file, so a crash does not cause this. Removes what any other save left, only once it knows what it has. Anything but SAVE_WORLD_OK has
 * logged why, holds nothing, and has changed nothing. */
[[nodiscard]] save_world_status_t save_state_open(save_state_t *state, arena_t *arena, const char *world_directory,
                                                  uint64_t seed, uint64_t generation);

/* Forgets it all. Safe on one not open. */
void save_state_close(save_state_t *state);

/* Where the player in a file that was read stands and looks, so that a world can be opened around it. False if there is no such player. */
bool save_state_kept_start(const save_state_t *state, float64_t out_feet[COLLISION_AXES], float32_t *out_yaw,
                           float32_t *out_pitch);

/* Puts what a file that was read holds into these parts, which have been started, and into parts->time. Every part with nothing in the file stays
 * as it is. False if there is no file. Before the threads start. */
bool save_state_apply(const save_state_t *state, save_state_parts_t *parts);

/* All of these parts, copied to the image, a file but for its generation and hashes. Whoever holds them, with nothing else using the image: no
 * heap and no I/O. */
void save_state_capture(save_state_t *state, const save_state_parts_t *parts);

/* What was last captured, as <world>/state.<generation>, whole or not at all. Any one thread. Anything but SAVE_WORLD_OK has logged why, and whatever
 * was at that name is as it was. */
[[nodiscard]] save_world_status_t save_state_write(save_state_t *state, uint64_t generation);

/* A file no save refers to any more goes. If that fails, the next open tidies it. */
void save_state_forget(const save_state_t *state, uint64_t generation);

/* Whether these are a file of this seed and generation that can be believed in this build's tables, and if not why. SAVE_WORLD_ERROR_TABLES is one from others, which
 * save_state_open would have translated first. Nothing is changed. */
[[nodiscard]] save_world_status_t save_state_check(const uint8_t *file, uint64_t bytes, uint64_t seed,
                                                   uint64_t generation);

/* What the numbers in a file mean: a hash of the names in each table they index. */
uint64_t save_state_table_hash(void);

/* One section of a file believed so far, or nullptr. */
const uint8_t *save_state_section_find(const uint8_t *file, uint64_t bytes, uint32_t id, uint64_t *out_bytes);

#endif
