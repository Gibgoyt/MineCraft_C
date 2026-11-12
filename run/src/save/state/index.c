/* See README.md. */
#include "save/state/index.h"

#include "assert/index.h"
#include "calendar/index.h"
#include "log/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "tables/mobs/index.h"
#include "tables/recipes/index.h"

#include <dirent.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t SAVE_STATE_MAGIC[8] = {'M', 'C', 'S', 'T', 'A', 'T', 'E', '1'};

enum : uint64_t {
    SAVE_STATE_HASH_START = 0xCBF29CE484222325ull,
    SAVE_STATE_HASH_PRIME = 0x100000001B3ull,
};

enum : uint32_t {
    /* Three axes three times, the two of wish, two falls, ten numbers of 4 bytes, and the handle, age, kind, flags and swing. */
    SAVE_STATE_BODY_FIELDS = 9u + 2u + 2u + 10u + 5u,
    SAVE_STATE_SEEN_WORDS = ENTITY_STORE_CAPACITY / 64u,
};
static_assert(ENTITY_STORE_CAPACITY % 64u == 0u);

/* As far from the middle as a place may be believed: past this a cell's number stops fitting what columns are counted in. */
#define SAVE_STATE_PLACE_MOST 1000000000.0f64
/* Blocks a tick, far past anything that happens, and with a wish on top still well inside PHYSICS_MOVE_BLOCKS_MAX, which physics stops the program for. */
#define SAVE_STATE_SPEED_MOST 1024.0f64
/* Blocks above the feet: a line to an eye further than this could run a cell's number off the end of an int32_t. */
#define SAVE_STATE_EYE_MOST 1024.0f64
/* A fall, or a hit or anything else a body adds up: far past anything that happens, and far from what a float32_t holds, so that a body is never hurt by infinity. */
#define SAVE_STATE_TALLY_MOST 1000000.0f64

static uint64_t save_state_hash_more(uint64_t hash, const void *data, uint64_t bytes) {
    const uint8_t *const cursor = data;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ cursor[i]) * SAVE_STATE_HASH_PRIME;
    }
    return hash;
}

static uint64_t save_state_hash_name(uint64_t hash, const char *name) {
    /* With its NUL, so that "ab", "c" is not "a", "bc". */
    return save_state_hash_more(hash, name, strlen(name) + 1u);
}

uint64_t save_state_table_hash(void) {
    uint64_t hash = SAVE_STATE_HASH_START;
    for (uint32_t i = 0u; i < BLOCK_COUNT; i++) {
        hash = save_state_hash_name(hash, block_defs[i].name);
    }
    for (uint32_t i = 0u; i < BLOCK_DROP_ITEM_COUNT; i++) {
        hash = save_state_hash_name(hash, block_drop_items[i].name);
    }
    for (uint32_t i = 0u; i < ITEM_COUNT; i++) {
        hash = save_state_hash_name(hash, item_defs[i].name);
    }
    for (uint32_t i = 0u; i < MOB_COUNT; i++) {
        hash = save_state_hash_name(hash, mob_defs[i].name);
    }
    return hash;
}

/* ---- One body's arrays, in the order a file has them ------------------------------------------------------------------ */

typedef struct save_state_field_t {
    void *borrowed_array;   /* 0 */
    uint32_t element_bytes; /* 8 */
    uint8_t _pad0[4];       /* 12 */
} save_state_field_t;
static_assert(sizeof(save_state_field_t) == 16);

/* Every array that has one number per body. They come to SAVE_STATE_BODY_BYTES a body. */
static void save_state_body_fields(const entity_store_t *store, save_state_field_t out[SAVE_STATE_BODY_FIELDS]) {
    uint32_t next = 0u;
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        out[next++] = (save_state_field_t){store->borrowed_position[axis], sizeof(float64_t), {0}};
    }
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        out[next++] = (save_state_field_t){store->borrowed_previous[axis], sizeof(float64_t), {0}};
    }
    for (uint32_t axis = 0u; axis < COLLISION_AXES; axis++) {
        out[next++] = (save_state_field_t){store->borrowed_velocity[axis], sizeof(float64_t), {0}};
    }
    out[next++] = (save_state_field_t){store->borrowed_wish[0], sizeof(float64_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_wish[1], sizeof(float64_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_fall_distance, sizeof(float64_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_landed_fall, sizeof(float64_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_half_width, sizeof(float32_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_height, sizeof(float32_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_yaw, sizeof(float32_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_health, sizeof(float32_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_limb_phase, sizeof(float32_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_limb_amount, sizeof(float32_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_head_yaw, sizeof(float32_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_head_pitch, sizeof(float32_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_aim, sizeof(float32_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_swell, sizeof(float32_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_handle, sizeof(entity_handle_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_age, sizeof(uint32_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_kind, sizeof(uint16_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_flags, sizeof(uint8_t), {0}};
    out[next++] = (save_state_field_t){store->borrowed_swing_ticks, sizeof(uint8_t), {0}};
    ASSERT_ALWAYS(next == SAVE_STATE_BODY_FIELDS);
}

static_assert(13u * 8u + 10u * 4u + 2u * 4u + 2u + 2u * 1u == SAVE_STATE_BODY_BYTES);

/* ---- Copying out -------------------------------------------------------------------------------------------------------- */

/* A file the game writes is one it must believe, or the world will not open again, and what is alive can pass what a check believes: an arrow keeps flying, a fall keeps
 * adding up. So what goes into a file stops at those limits. Only the file: it changes nothing a game that has them in range would write. */
static void save_state_settle_f64s(uint8_t *numbers, uint32_t count, float64_t most) {
    for (uint32_t i = 0u; i < count; i++) {
        float64_t value;
        memcpy(&value, numbers + (uint64_t)i * sizeof(value), sizeof(value));
        const float64_t settled = !isfinite(value) ? 0.0f64 : (fabs(value) > most ? copysign(most, value) : value);
        memcpy(numbers + (uint64_t)i * sizeof(value), &settled, sizeof(value));
    }
}

static void save_state_settle_f32s(uint8_t *numbers, uint32_t count, float32_t most) {
    for (uint32_t i = 0u; i < count; i++) {
        float32_t value;
        memcpy(&value, numbers + (uint64_t)i * sizeof(value), sizeof(value));
        const float32_t settled = !isfinite(value) ? 0.0f32 : (fabsf(value) > most ? copysignf(most, value) : value);
        memcpy(numbers + (uint64_t)i * sizeof(value), &settled, sizeof(value));
    }
}

/* Where a file is being put together. */
typedef struct save_state_out_t {
    uint8_t *borrowed_file; /* 0 */
    uint64_t used;          /* 8 */
    uint64_t section_at;    /* 16: of the section open now */
    uint32_t section_count; /* 24 */
    uint8_t _pad0[4];       /* 28 */
} save_state_out_t;
static_assert(sizeof(save_state_out_t) == 32);

/* Where what has been written of the section open now begins. */
static uint8_t *save_state_out_section(const save_state_out_t *out) {
    return out->borrowed_file + out->section_at + sizeof(save_state_section_t);
}

static void save_state_out_begin(save_state_out_t *out, uint32_t id) {
    out->section_at = out->used;
    const save_state_section_t section = {.bytes = 0u, .id = id, ._pad0 = {0}};
    memcpy(out->borrowed_file + out->used, &section, sizeof(section));
    out->used += sizeof(section);
}

static void save_state_out_bytes(save_state_out_t *out, const void *data, uint64_t bytes) {
    ASSERT_ALWAYS(out->used + bytes <= SAVE_STATE_IMAGE_BYTES);
    memcpy(out->borrowed_file + out->used, data, bytes);
    out->used += bytes;
}

static void save_state_out_end(save_state_out_t *out) {
    const uint64_t bytes = out->used - out->section_at - sizeof(save_state_section_t);
    memcpy(out->borrowed_file + out->section_at + offsetof(save_state_section_t, bytes), &bytes, sizeof(bytes));
    /* Written down, so that the same numbers are the same file. */
    while ((out->used & 7u) != 0u) {
        out->borrowed_file[out->used] = 0u;
        out->used++;
    }
    out->section_count++;
}

static void save_state_out_time(save_state_out_t *out, const save_state_time_t *time) {
    save_state_out_begin(out, SAVE_STATE_SECTION_TIME);
    save_state_out_bytes(out, time, sizeof(*time));
    save_state_out_end(out);
}

static void save_state_out_calendar(save_state_out_t *out, const save_state_calendar_t *calendar) {
    save_state_calendar_t written = *calendar;
    memset(written._pad0, 0, sizeof(written._pad0));
    save_state_out_begin(out, SAVE_STATE_SECTION_CALENDAR);
    save_state_out_bytes(out, &written, sizeof(written));
    save_state_out_end(out);
}

static void save_state_out_player(save_state_out_t *out, const player_t *player) {
    save_state_out_begin(out, SAVE_STATE_SECTION_PLAYER);
    save_state_out_bytes(out, &player->movement, sizeof(player->movement));
    save_state_out_bytes(out, &player->survival, sizeof(player->survival));
    save_state_out_bytes(out, &player->interaction, sizeof(player->interaction));
    save_state_out_bytes(out, &player->combat, sizeof(player->combat));
    save_state_out_bytes(out, &player->tick_count, sizeof(player->tick_count));
    uint8_t *const movement = save_state_out_section(out) + offsetof(save_state_player_t, movement);
    save_state_settle_f64s(movement + offsetof(player_movement_t, position), 2u * COLLISION_AXES,
                           SAVE_STATE_PLACE_MOST);
    save_state_settle_f64s(movement + offsetof(player_movement_t, velocity), COLLISION_AXES, SAVE_STATE_SPEED_MOST);
    save_state_settle_f64s(movement + offsetof(player_movement_t, fall_distance), 2u, SAVE_STATE_TALLY_MOST);
    save_state_settle_f64s(movement + offsetof(player_movement_t, eye_height), 2u, SAVE_STATE_EYE_MOST);
    uint8_t *const survival = save_state_out_section(out) + offsetof(save_state_player_t, survival);
    save_state_settle_f64s(survival + offsetof(player_survival_t, spawn), COLLISION_AXES, SAVE_STATE_PLACE_MOST);
    save_state_settle_f32s(survival + offsetof(player_survival_t, saturation), 5u, (float32_t)SAVE_STATE_TALLY_MOST);
    save_state_settle_f32s(survival + offsetof(player_survival_t, armor_toughness), 2u,
                           (float32_t)SAVE_STATE_TALLY_MOST);
    uint8_t *const hands = save_state_out_section(out) + offsetof(save_state_player_t, interaction);
    for (uint32_t i = 0u; i < PLAYER_INTERACTION_DROPS; i++) {
        save_state_settle_f64s(hands + offsetof(player_interaction_t, drops) + (uint64_t)i * sizeof(player_drop_t) +
                                   offsetof(player_drop_t, position),
                               COLLISION_AXES, SAVE_STATE_PLACE_MOST);
    }
    save_state_out_end(out);
}

static void save_state_out_items(save_state_out_t *out, const item_t *item, const item_stack_t *held) {
    save_state_out_begin(out, SAVE_STATE_SECTION_ITEMS);
    save_state_items_t head;
    memset(&head, 0, sizeof(head));
    head.crafting_hands = item->crafting_hands;
    head.crafting_table = item->crafting_table;
    head.crafting_bench = *item->borrowed_crafting_bench;
    head.equipment = item->equipment;
    head.added_count = item->inventory.added_count;
    head.removed_count = item->inventory.removed_count;
    head.refused_count = item->inventory.refused_count;
    head.spilled_count = item->spilled_count;
    head.chests_refused_count = item->chests.refused_count;
    head.smelted_count = item->furnaces.smelted_count;
    head.burned_count = item->furnaces.burned_count;
    head.furnaces_refused_count = item->furnaces.refused_count;
    memcpy(head.slots, item->inventory.borrowed_slots, sizeof(head.slots));
    if (held != nullptr) {
        head.held = *held;
    }
    head.selected = item->inventory.selected;
    save_state_out_bytes(out, &head, sizeof(head));
    save_state_out_end(out);
    save_state_out_begin(out, SAVE_STATE_SECTION_ALLOY);
    crafting_alloy_t alloy = *item->borrowed_crafting_alloy;
    /* What the slots make and what burns nearby are worked out again on opening, with this build's recipes. */
    alloy.slots[CRAFTING_ALLOY_OUTPUT] = (item_stack_t){0};
    alloy.recipe = CRAFTING_NO_RECIPE;
    alloy.heat = 0u;
    save_state_out_bytes(out, &alloy, sizeof(alloy));
    save_state_out_end(out);
    save_state_out_begin(out, SAVE_STATE_SECTION_CHESTS);
    save_state_out_bytes(out, item->chests.borrowed_chests, (uint64_t)item->chests.count * sizeof(container_chest_t));
    save_state_out_end(out);
    save_state_out_begin(out, SAVE_STATE_SECTION_FURNACES);
    save_state_out_bytes(out, item->furnaces.borrowed_furnaces,
                         (uint64_t)item->furnaces.count * sizeof(smelting_furnace_t));
    save_state_out_end(out);
}

static void save_state_out_entity(save_state_out_t *out, const entity_t *entity) {
    const item_drop_store_t *const drops = &entity->item_drops;
    const save_state_drops_t drops_head = {.random = drops->random.state,
                                           .tick_count = drops->tick_count,
                                           .dropped_count = drops->dropped_count,
                                           .picked_up_count = drops->picked_up_count,
                                           .despawned_count = drops->despawned_count,
                                           .destroyed_count = drops->destroyed_count,
                                           .lost_count = drops->lost_count,
                                           .merged_count = drops->merged_count,
                                           .count = drops->count,
                                           .next_id = drops->next_id};
    save_state_out_begin(out, SAVE_STATE_SECTION_DROPS);
    save_state_out_bytes(out, &drops_head, sizeof(drops_head));
    save_state_out_bytes(out, drops->borrowed_items, (uint64_t)drops->count * sizeof(item_drop_t));
    for (uint32_t i = 0u; i < drops->count; i++) {
        uint8_t *const drop = save_state_out_section(out) + sizeof(drops_head) + (uint64_t)i * sizeof(item_drop_t);
        save_state_settle_f64s(drop + offsetof(item_drop_t, position), COLLISION_AXES, SAVE_STATE_PLACE_MOST);
        save_state_settle_f64s(drop + offsetof(item_drop_t, velocity), COLLISION_AXES, SAVE_STATE_SPEED_MOST);
    }
    save_state_out_end(out);

    const projectile_store_t *const arrows = &entity->projectiles;
    const save_state_arrows_t arrows_head = {.random = arrows->random.state,
                                             .fired_count = arrows->fired_count,
                                             .body_hits = arrows->body_hits,
                                             .target_hits = arrows->target_hits,
                                             .expired_count = arrows->expired_count,
                                             .burned_count = arrows->burned_count,
                                             .lost_count = arrows->lost_count,
                                             .stuck_count = arrows->stuck_count,
                                             .count = arrows->count,
                                             ._pad0 = {0}};
    save_state_out_begin(out, SAVE_STATE_SECTION_ARROWS);
    save_state_out_bytes(out, &arrows_head, sizeof(arrows_head));
    save_state_out_bytes(out, arrows->borrowed_items, (uint64_t)arrows->count * sizeof(projectile_t));
    for (uint32_t i = 0u; i < arrows->count; i++) {
        uint8_t *const arrow = save_state_out_section(out) + sizeof(arrows_head) + (uint64_t)i * sizeof(projectile_t);
        save_state_settle_f64s(arrow + offsetof(projectile_t, position), COLLISION_AXES, SAVE_STATE_PLACE_MOST);
        save_state_settle_f64s(arrow + offsetof(projectile_t, velocity), COLLISION_AXES, SAVE_STATE_SPEED_MOST);
    }
    save_state_out_end(out);

    const entity_store_t *const store = &entity->store;
    const save_state_bodies_t bodies_head = {.spawned_count = store->spawned_count,
                                             .removed_count = store->removed_count,
                                             .refused_count = store->refused_count,
                                             .blast_random = entity->explosion.random.state,
                                             .broken_count = entity->explosion.broken_count,
                                             .count = store->count,
                                             .free_head = store->free_head,
                                             .blast_count = entity->explosion.blast_count,
                                             ._pad0 = {0}};
    save_state_out_begin(out, SAVE_STATE_SECTION_BODIES);
    save_state_out_bytes(out, &bodies_head, sizeof(bodies_head));
    save_state_out_bytes(out, store->borrowed_slot_index, ENTITY_STORE_CAPACITY * sizeof(uint32_t));
    save_state_out_bytes(out, store->borrowed_slot_generation, ENTITY_STORE_CAPACITY * sizeof(uint32_t));
    save_state_out_bytes(out, store->borrowed_free_slots, ENTITY_STORE_CAPACITY * sizeof(uint32_t));
    save_state_field_t fields[SAVE_STATE_BODY_FIELDS];
    save_state_body_fields(store, fields);
    const uint64_t arrays_at = out->used;
    for (uint32_t i = 0u; i < SAVE_STATE_BODY_FIELDS; i++) {
        save_state_out_bytes(out, fields[i].borrowed_array, (uint64_t)store->count * fields[i].element_bytes);
    }
    /* In the order of save_state_body_fields, as save_state_check_bodies reads them. */
    uint8_t *const arrays = out->borrowed_file + arrays_at;
    const uint64_t each = (uint64_t)store->count * sizeof(float64_t);
    save_state_settle_f64s(arrays, 6u * store->count, SAVE_STATE_PLACE_MOST);
    save_state_settle_f64s(arrays + 6u * each, 5u * store->count, SAVE_STATE_SPEED_MOST);
    save_state_settle_f64s(arrays + 11u * each, 2u * store->count, SAVE_STATE_TALLY_MOST);
    /* All but half_width and height, which never change and are looked at more closely. */
    save_state_settle_f32s(arrays + 13u * each + 2ull * store->count * sizeof(float32_t), 8u * store->count, FLT_MAX);
    save_state_out_end(out);

    const save_state_sexes_t sexes_head = {.count = store->count, ._pad0 = {0}};
    save_state_out_begin(out, SAVE_STATE_SECTION_SEXES);
    save_state_out_bytes(out, &sexes_head, sizeof(sexes_head));
    save_state_out_bytes(out, store->borrowed_sex, store->count);
    save_state_out_end(out);
}

static void save_state_out_spawn(save_state_out_t *out, const mob_spawn_t *spawn) {
    const save_state_spawn_t head = {.random = spawn->random.state,
                                     .spawned_count = spawn->spawned_count,
                                     .despawned_count = spawn->despawned_count,
                                     .refused_count = spawn->refused_count};
    save_state_out_begin(out, SAVE_STATE_SECTION_SPAWN);
    save_state_out_bytes(out, &head, sizeof(head));
    save_state_out_bytes(out, spawn->borrowed_idle, ENTITY_STORE_CAPACITY * sizeof(uint32_t));
    save_state_out_end(out);
}

/* Always there, and always last: what every number before it means. */
static void save_state_out_names(save_state_out_t *out) {
    save_state_out_begin(out, SAVE_STATE_SECTION_NAMES);
    ASSERT_ALWAYS(out->used + SAVE_STATE_NAMES_BYTES_MAX <= SAVE_STATE_IMAGE_BYTES);
    out->used += save_names_write_all(out->borrowed_file + out->used);
    save_state_out_end(out);
}

static void save_state_out_minds(save_state_out_t *out, const mob_t *mob) {
    const save_state_minds_t head = {.zombie_random = mob->zombie.random.state,
                                     .zombie_struck = mob->zombie.struck_count,
                                     .zombie_died = mob->zombie.died_count,
                                     .zombie_dropped = mob->zombie.dropped_count,
                                     .skeleton_random = mob->skeleton.random.state,
                                     .skeleton_shot = mob->skeleton.shot_count,
                                     .skeleton_died = mob->skeleton.died_count,
                                     .skeleton_dropped = mob->skeleton.dropped_count,
                                     .creeper_random = mob->creeper.random.state,
                                     .creeper_burst = mob->creeper.burst_count,
                                     .creeper_died = mob->creeper.died_count,
                                     .creeper_dropped = mob->creeper.dropped_count,
                                     .combat_landed = mob->combat.landed_count,
                                     .combat_killed = mob->combat.killed_count,
                                     .combat_knocked = mob->combat.knocked_count};
    save_state_out_begin(out, SAVE_STATE_SECTION_MINDS);
    save_state_out_bytes(out, &head, sizeof(head));
    save_state_out_bytes(out, mob->zombie.borrowed_bodies, ENTITY_STORE_CAPACITY * sizeof(mob_zombie_body_t));
    save_state_out_bytes(out, mob->skeleton.borrowed_bodies, ENTITY_STORE_CAPACITY * sizeof(mob_skeleton_body_t));
    save_state_out_bytes(out, mob->creeper.borrowed_bodies, ENTITY_STORE_CAPACITY * sizeof(mob_creeper_body_t));
    save_state_out_bytes(out, mob->combat.borrowed_bodies, ENTITY_STORE_CAPACITY * sizeof(mob_combat_body_t));
    uint8_t *const zombies = save_state_out_section(out) + sizeof(head);
    uint8_t *const hurts = zombies + ENTITY_STORE_CAPACITY * (sizeof(mob_zombie_body_t) + sizeof(mob_skeleton_body_t) +
                                                              sizeof(mob_creeper_body_t));
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY; i++) {
        save_state_settle_f64s(zombies + (uint64_t)i * sizeof(mob_zombie_body_t) + offsetof(mob_zombie_body_t, asked),
                               COLLISION_AXES, SAVE_STATE_PLACE_MOST);
        save_state_settle_f32s(hurts + (uint64_t)i * sizeof(mob_combat_body_t) + offsetof(mob_combat_body_t, last_hurt),
                               1u, FLT_MAX);
    }
    save_state_out_end(out);

    const save_state_passive_t passive_head = {.random = mob->passive.borrowed_tally->random.state,
                                               .fled = mob->passive.borrowed_tally->fled_count,
                                               .died = mob->passive.borrowed_tally->died_count,
                                               .dropped = mob->passive.borrowed_tally->dropped_count};
    save_state_out_begin(out, SAVE_STATE_SECTION_PASSIVE);
    save_state_out_bytes(out, &passive_head, sizeof(passive_head));
    save_state_out_bytes(out, mob->passive.borrowed_bodies, ENTITY_STORE_CAPACITY * sizeof(mob_passive_body_t));
    uint8_t *const animals = save_state_out_section(out) + sizeof(passive_head);
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY; i++) {
        uint8_t *const animal = animals + (uint64_t)i * sizeof(mob_passive_body_t);
        save_state_settle_f64s(animal + offsetof(mob_passive_body_t, from), 2u, SAVE_STATE_PLACE_MOST);
        save_state_settle_f32s(animal + offsetof(mob_passive_body_t, last_health), 1u, FLT_MAX);
    }
    save_state_out_end(out);
}

void save_state_capture(save_state_t *state, const save_state_parts_t *parts) {
    ASSERT_ALWAYS(state->open != 0u);
    save_state_out_t out = {.borrowed_file = state->borrowed_image, .used = sizeof(save_state_header_t)};
    save_state_out_time(&out, &parts->time);
    save_state_out_calendar(&out, &parts->calendar);
    if (parts->borrowed_player != nullptr) {
        save_state_out_player(&out, parts->borrowed_player);
    }
    if (parts->borrowed_item != nullptr) {
        save_state_out_items(&out, parts->borrowed_item, parts->borrowed_held);
    }
    if (parts->borrowed_entity != nullptr) {
        save_state_out_entity(&out, parts->borrowed_entity);
    }
    if (parts->borrowed_spawn != nullptr) {
        save_state_out_spawn(&out, parts->borrowed_spawn);
    }
    if (parts->borrowed_mob != nullptr) {
        save_state_out_minds(&out, parts->borrowed_mob);
    }
    save_state_out_names(&out);
    save_state_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, SAVE_STATE_MAGIC, sizeof(header.magic));
    header.seed = state->seed;
    header.table_hash = state->table_hash;
    header.sections_bytes = out.used - sizeof(header);
    header.version = SAVE_STATE_VERSION;
    header.header_bytes = sizeof(header);
    header.section_count = out.section_count;
    memcpy(state->borrowed_image, &header, sizeof(header));
    state->image_bytes = out.used;
    state->kept = 0u;
}

/* ---- Writing ----------------------------------------------------------------------------------------------------------- */

static bool save_state_path(char *out, const char *directory, uint64_t generation) {
    const int32_t written = snprintf(out, FILE_PATH_BYTES, "%s/state.%" PRIu64, directory, generation);
    return written > 0 && written < (int32_t)FILE_PATH_BYTES - 8;
}

save_world_status_t save_state_write(save_state_t *state, uint64_t generation) {
    ASSERT_ALWAYS(state->open != 0u && state->image_bytes >= sizeof(save_state_header_t));
    save_state_header_t header;
    memcpy(&header, state->borrowed_image, sizeof(header));
    header.generation = generation;
    header.sections_hash =
        save_state_hash_more(SAVE_STATE_HASH_START, state->borrowed_image + sizeof(header), header.sections_bytes);
    header.checksum = save_state_hash_more(SAVE_STATE_HASH_START, &header, offsetof(save_state_header_t, checksum));
    memcpy(state->borrowed_image, &header, sizeof(header));
    char path[FILE_PATH_BYTES];
    if (!save_state_path(path, state->borrowed_directory, generation)) {
        LOG_ERROR("save: the path of the state in %s is too long", state->borrowed_directory);
        return SAVE_WORLD_ERROR_PATH;
    }
    const file_status_t status = file_write_atomic(path, state->borrowed_image, state->image_bytes);
    if (status != FILE_OK) {
        state->file_status = status;
        LOG_ERROR("save: %s could not be written: %s", path, file_status_name(status));
        return SAVE_WORLD_ERROR_FILE;
    }
    state->files_written++;
    return SAVE_WORLD_OK;
}

void save_state_forget(const save_state_t *state, uint64_t generation) {
    char path[FILE_PATH_BYTES];
    if (save_state_path(path, state->borrowed_directory, generation)) {
        (void)file_remove(path);
    }
}

/* ---- What a file is allowed to say ---------------------------------------------------------------------------------------- */

const uint8_t *save_state_section_find(const uint8_t *file, uint64_t bytes, uint32_t id, uint64_t *out_bytes) {
    uint64_t at = sizeof(save_state_header_t);
    while (at + sizeof(save_state_section_t) <= bytes) {
        save_state_section_t section;
        memcpy(&section, file + at, sizeof(section));
        at += sizeof(section);
        if (section.bytes > bytes - at) {
            return nullptr;
        }
        if (section.id == id) {
            *out_bytes = section.bytes;
            return file + at;
        }
        at += (section.bytes + 7u) & ~(uint64_t)7u;
    }
    return nullptr;
}

static bool save_state_f64s_within(const float64_t *values, uint32_t count, float64_t most) {
    for (uint32_t i = 0u; i < count; i++) {
        if (!isfinite(values[i]) || fabs(values[i]) > most) {
            return false;
        }
    }
    return true;
}

static bool save_state_place_fine(const float64_t *values, uint32_t count) {
    return save_state_f64s_within(values, count, SAVE_STATE_PLACE_MOST);
}

static bool save_state_tally_fine(float32_t value) {
    return isfinite(value) && fabsf(value) <= (float32_t)SAVE_STATE_TALLY_MOST;
}

static bool save_state_f32s_fine(const float32_t *values, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        if (!isfinite(values[i])) {
            return false;
        }
    }
    return true;
}

static bool save_state_stacks_fine(const item_stack_t *stacks, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        if (!item_stack_is_valid(&stacks[i])) {
            return false;
        }
    }
    return true;
}

static bool save_state_check_time(const uint8_t *data, uint64_t bytes) {
    save_state_time_t time;
    if (bytes != sizeof(time)) {
        return false;
    }
    memcpy(&time, data, sizeof(time));
    return isfinite(time.yaw) && isfinite(time.pitch);
}

/* A census cannot have run for a week the saved clock has not reached: the + 1 covers the time of day. */
static bool save_state_check_calendar(const uint8_t *data, uint64_t bytes) {
    save_state_calendar_t calendar;
    if (bytes != sizeof(calendar)) {
        return false;
    }
    memcpy(&calendar, data, sizeof(calendar));
    for (uint32_t i = 0u; i < sizeof(calendar._pad0); i++) {
        if (calendar._pad0[i] != 0u) {
            return false;
        }
    }
    return calendar.clock_days < CALENDAR_CLOCK_DAY && calendar.day_zero < CALENDAR_DAYS_PER_YEAR &&
           calendar.last_census_week <=
               ((uint64_t)calendar.day_zero + calendar.clock_days + 1u) / CALENDAR_DAYS_PER_WEEK;
}

static bool save_state_check_player(const uint8_t *data, uint64_t bytes) {
    save_state_player_t player;
    if (bytes != sizeof(player)) {
        return false;
    }
    memcpy(&player, data, sizeof(player));
    const player_movement_t *const body = &player.movement;
    const player_survival_t *const health = &player.survival;
    const player_interaction_t *const hands = &player.interaction;
    if (!save_state_place_fine(body->position, COLLISION_AXES) ||
        !save_state_place_fine(body->previous, COLLISION_AXES) ||
        !save_state_f64s_within(body->velocity, COLLISION_AXES, SAVE_STATE_SPEED_MOST) ||
        !save_state_f64s_within(&body->fall_distance, 1u, SAVE_STATE_TALLY_MOST) ||
        !save_state_f64s_within(&body->landed_fall, 1u, SAVE_STATE_TALLY_MOST) ||
        !save_state_f64s_within(&body->eye_height, 1u, SAVE_STATE_EYE_MOST) ||
        !save_state_f64s_within(&body->previous_eye, 1u, SAVE_STATE_EYE_MOST) ||
        body->mode > PLAYER_MOVEMENT_MODE_FLY || body->fly_step > PLAYER_MOVEMENT_FLY_STEP_MOST ||
        body->speed_factor > PLAYER_MOVEMENT_FACTOR_MAX) {
        return false;
    }
    if (!save_state_place_fine(health->spawn, COLLISION_AXES) || !isfinite(health->sun_height) ||
        !(health->health >= 0.0f32 && health->health <= (float32_t)PLAYER_SURVIVAL_HEALTH_MAX) ||
        !save_state_tally_fine(health->saturation) || !save_state_tally_fine(health->exhaustion) ||
        !save_state_tally_fine(health->last_hurt) || !save_state_tally_fine(health->armor_points) ||
        !save_state_tally_fine(health->held_saturation) || !save_state_tally_fine(health->armor_toughness) ||
        !save_state_tally_fine(health->armor_hit) || health->air > (int32_t)PLAYER_SURVIVAL_AIR_MAX ||
        health->air < -(int32_t)PLAYER_SURVIVAL_DROWN_EVERY_TICKS || health->food > PLAYER_SURVIVAL_FOOD_MAX ||
        health->state > PLAYER_SURVIVAL_DEAD || health->killed_by >= PLAYER_SURVIVAL_CAUSE_COUNT ||
        health->bed_answer > PLAYER_SURVIVAL_BED_ASLEEP || health->arriving > PLAYER_SURVIVAL_ARRIVING_FALL ||
        health->arrive_kind > PLAYER_SURVIVAL_ARRIVE_GROUND) {
        return false;
    }
    if (hands->drop_head >= PLAYER_INTERACTION_DROPS || hands->drop_count > PLAYER_INTERACTION_DROPS ||
        hands->hand_count > PLAYER_INTERACTION_HAND_MAX || hands->hand_block >= BLOCK_COUNT ||
        hands->mining_block >= BLOCK_COUNT || hands->last_mined >= BLOCK_COUNT) {
        return false;
    }
    for (uint32_t i = 0u; i < hands->drop_count; i++) {
        const player_drop_t *const drop = &hands->drops[(hands->drop_head + i) % PLAYER_INTERACTION_DROPS];
        if (drop->item >= BLOCK_DROP_ITEM_COUNT || !save_state_place_fine(drop->position, COLLISION_AXES)) {
            return false;
        }
    }
    return true;
}

/* Not what it makes: save_state_apply works that out from the cells, as this build's recipes need not be those it was saved with. */
static bool save_state_check_grid(const crafting_grid_t *grid, uint8_t side, uint8_t station) {
    return grid->side == side && grid->station == station && save_state_stacks_fine(grid->cells, RECIPE_CELLS);
}

/* The hands' grid was the table's station until plan 005 phase 6 gave the hands their own, and the bench's grid is any station's that has
 * a 3 by 3 of its own block, as each station's screen uses it in turn. */
static bool save_state_check_station_grids(const save_state_items_t *items) {
    const crafting_grid_t *const bench = &items->crafting_bench;
    const bool bench_station = bench->station != RECIPE_STATION_TABLE && bench->station != RECIPE_STATION_HANDS &&
                               bench->station < RECIPE_STATION_COUNT;
    return (save_state_check_grid(&items->crafting_hands, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE) ||
            save_state_check_grid(&items->crafting_hands, CRAFTING_SIDE_HANDS, RECIPE_STATION_HANDS)) &&
           bench_station && save_state_check_grid(bench, CRAFTING_SIDE_TABLE, bench->station);
}

static bool save_state_check_items(const uint8_t *data, uint64_t bytes) {
    save_state_items_t items;
    if (bytes != sizeof(items)) {
        return false;
    }
    memcpy(&items, data, sizeof(items));
    return items.selected < INVENTORY_HOTBAR_SLOTS && save_state_stacks_fine(items.slots, INVENTORY_SLOTS) &&
           item_stack_is_valid(&items.held) && save_state_check_station_grids(&items) &&
           save_state_check_grid(&items.crafting_table, CRAFTING_SIDE_TABLE, RECIPE_STATION_TABLE) &&
           save_state_stacks_fine(items.equipment.armor, EQUIPMENT_ARMOR_SLOTS);
}

static bool save_state_check_alloy(const uint8_t *data, uint64_t bytes) {
    crafting_alloy_t alloy;
    if (bytes != sizeof(alloy)) {
        return false;
    }
    memcpy(&alloy, data, sizeof(alloy));
    return alloy.station == RECIPE_STATION_SMITHING && save_state_stacks_fine(alloy.slots, CRAFTING_ALLOY_SLOTS);
}

static bool save_state_check_chests(const uint8_t *data, uint64_t bytes) {
    if (bytes % sizeof(container_chest_t) != 0u || bytes / sizeof(container_chest_t) > CONTAINER_CHESTS) {
        return false;
    }
    for (uint64_t at = 0u; at < bytes; at += sizeof(container_chest_t)) {
        container_chest_t chest;
        memcpy(&chest, data + at, sizeof(chest));
        if (!save_state_stacks_fine(chest.slots, CONTAINER_CHEST_SLOTS)) {
            return false;
        }
    }
    return true;
}

static bool save_state_check_furnaces(const uint8_t *data, uint64_t bytes) {
    if (bytes % sizeof(smelting_furnace_t) != 0u || bytes / sizeof(smelting_furnace_t) > SMELTING_FURNACES) {
        return false;
    }
    for (uint64_t at = 0u; at < bytes; at += sizeof(smelting_furnace_t)) {
        smelting_furnace_t furnace;
        memcpy(&furnace, data + at, sizeof(furnace));
        if (!save_state_stacks_fine(furnace.slots, SMELTING_SLOTS) || furnace.cooking >= ITEM_COUNT ||
            furnace.process >= RECIPE_PROCESS_COUNT) {
            return false;
        }
    }
    return true;
}

static bool save_state_check_drops(const uint8_t *data, uint64_t bytes) {
    save_state_drops_t head;
    if (bytes < sizeof(head)) {
        return false;
    }
    memcpy(&head, data, sizeof(head));
    if (head.count > ITEM_DROP_CAPACITY || bytes != sizeof(head) + (uint64_t)head.count * sizeof(item_drop_t)) {
        return false;
    }
    for (uint32_t i = 0u; i < head.count; i++) {
        item_drop_t drop;
        memcpy(&drop, data + sizeof(head) + (uint64_t)i * sizeof(drop), sizeof(drop));
        if (drop.item >= BLOCK_DROP_ITEM_COUNT || drop.count == 0u || drop.count > ITEM_DROP_STACK_MAX ||
            !save_state_place_fine(drop.position, COLLISION_AXES) ||
            !save_state_f64s_within(drop.velocity, COLLISION_AXES, SAVE_STATE_SPEED_MOST)) {
            return false;
        }
    }
    return true;
}

static bool save_state_check_arrows(const uint8_t *data, uint64_t bytes) {
    save_state_arrows_t head;
    if (bytes < sizeof(head)) {
        return false;
    }
    memcpy(&head, data, sizeof(head));
    if (head.count > PROJECTILE_CAPACITY || bytes != sizeof(head) + (uint64_t)head.count * sizeof(projectile_t)) {
        return false;
    }
    for (uint32_t i = 0u; i < head.count; i++) {
        projectile_t arrow;
        memcpy(&arrow, data + sizeof(head) + (uint64_t)i * sizeof(arrow), sizeof(arrow));
        if (!save_state_place_fine(arrow.position, COLLISION_AXES) ||
            !save_state_f64s_within(arrow.velocity, COLLISION_AXES, SAVE_STATE_SPEED_MOST)) {
            return false;
        }
    }
    return true;
}

/* Whether each slot is either one body's or waiting in the free ring, and never both: an entity_store_spawn from a store that is not so would give
 * two bodies one handle. */
static bool save_state_check_slots(const uint8_t *tables, const uint8_t *handles, uint32_t count, uint32_t free_head) {
    uint64_t seen[SAVE_STATE_SEEN_WORDS] = {0};
    for (uint32_t i = 0u; i < count; i++) {
        entity_handle_t handle;
        memcpy(&handle, handles + (uint64_t)i * sizeof(handle), sizeof(handle));
        const uint32_t slot = entity_handle_slot(handle);
        uint32_t index, generation;
        memcpy(&index, tables + (uint64_t)slot * sizeof(index), sizeof(index));
        memcpy(&generation, tables + (uint64_t)(ENTITY_STORE_CAPACITY + slot) * sizeof(generation), sizeof(generation));
        const uint64_t bit = 1ull << (slot % 64u);
        if (index != i || generation >= ENTITY_STORE_GENERATION_LIMIT ||
            ((generation << ENTITY_STORE_SLOT_BITS) | slot) != handle || (seen[slot / 64u] & bit) != 0u) {
            return false;
        }
        seen[slot / 64u] |= bit;
    }
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY - count; i++) {
        uint32_t slot;
        const uint32_t at = (free_head + i) & ENTITY_STORE_SLOT_MASK;
        memcpy(&slot, tables + (uint64_t)(2u * ENTITY_STORE_CAPACITY + at) * sizeof(slot), sizeof(slot));
        if (slot >= ENTITY_STORE_CAPACITY) {
            return false;
        }
        const uint64_t bit = 1ull << (slot % 64u);
        if ((seen[slot / 64u] & bit) != 0u) {
            return false;
        }
        seen[slot / 64u] |= bit;
    }
    return true;
}

static bool save_state_check_bodies(const uint8_t *data, uint64_t bytes) {
    save_state_bodies_t head;
    if (bytes < sizeof(head)) {
        return false;
    }
    memcpy(&head, data, sizeof(head));
    const uint64_t tables_bytes = (uint64_t)SAVE_STATE_SLOT_TABLES * ENTITY_STORE_CAPACITY * sizeof(uint32_t);
    if (head.count > ENTITY_STORE_CAPACITY || head.free_head >= ENTITY_STORE_CAPACITY ||
        bytes != sizeof(head) + tables_bytes + (uint64_t)head.count * SAVE_STATE_BODY_BYTES) {
        return false;
    }
    const uint8_t *const tables = data + sizeof(head);
    const uint8_t *const arrays = tables + tables_bytes;
    /* Thirteen numbers of 8 bytes: six of place, five of speed that go into a move, and two of fall. */
    for (uint32_t field = 0u; field < 13u; field++) {
        for (uint32_t i = 0u; i < head.count; i++) {
            float64_t value;
            memcpy(&value, arrays + (uint64_t)field * head.count * sizeof(value) + (uint64_t)i * sizeof(value),
                   sizeof(value));
            const float64_t most =
                field < 6u ? SAVE_STATE_PLACE_MOST : (field < 11u ? SAVE_STATE_SPEED_MOST : SAVE_STATE_TALLY_MOST);
            if (!isfinite(value) || fabs(value) > most) {
                return false;
            }
        }
    }
    const uint8_t *const floats = arrays + 13ull * head.count * sizeof(float64_t);
    for (uint32_t field = 0u; field < 10u; field++) {
        for (uint32_t i = 0u; i < head.count; i++) {
            float32_t value;
            memcpy(&value, floats + (uint64_t)field * head.count * sizeof(value) + (uint64_t)i * sizeof(value),
                   sizeof(value));
            if (!isfinite(value)) {
                return false;
            }
            /* What physics would refuse, by an abort, if it were spawned: half_width and then height. */
            if (field == 0u && !(value > 0.0f32 && (float64_t)value * 2.0f64 <= PHYSICS_BOX_SIZE_MAX)) {
                return false;
            }
            if (field == 1u && !(value > 0.0f32 && (float64_t)value <= PHYSICS_BOX_SIZE_MAX)) {
                return false;
            }
        }
    }
    const uint8_t *const handles = floats + 10ull * head.count * sizeof(float32_t);
    /* After the handles and the ages, and what a mind looks in mob_defs by. */
    const uint8_t *const kinds = handles + 2ull * head.count * sizeof(uint32_t);
    for (uint32_t i = 0u; i < head.count; i++) {
        uint16_t kind = 0u;
        memcpy(&kind, kinds + (uint64_t)i * sizeof(kind), sizeof(kind));
        if (kind >= MOB_COUNT) {
            return false;
        }
    }
    return save_state_check_slots(tables, handles, head.count, head.free_head);
}

static bool save_state_check_spawn(uint64_t bytes) {
    return bytes == sizeof(save_state_spawn_t) + ENTITY_STORE_CAPACITY * sizeof(uint32_t);
}

static bool save_state_check_minds(const uint8_t *data, uint64_t bytes) {
    if (bytes != sizeof(save_state_minds_t) +
                     (uint64_t)ENTITY_STORE_CAPACITY * (sizeof(mob_zombie_body_t) + sizeof(mob_skeleton_body_t) +
                                                        sizeof(mob_creeper_body_t) + sizeof(mob_combat_body_t))) {
        return false;
    }
    const uint8_t *const zombies = data + sizeof(save_state_minds_t);
    const uint8_t *const skeletons = zombies + ENTITY_STORE_CAPACITY * sizeof(mob_zombie_body_t);
    const uint8_t *const creepers = skeletons + ENTITY_STORE_CAPACITY * sizeof(mob_skeleton_body_t);
    const uint8_t *const hurts = creepers + ENTITY_STORE_CAPACITY * sizeof(mob_creeper_body_t);
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY; i++) {
        mob_zombie_body_t zombie;
        mob_creeper_body_t creeper;
        mob_combat_body_t hurt;
        memcpy(&zombie, zombies + (uint64_t)i * sizeof(zombie), sizeof(zombie));
        memcpy(&creeper, creepers + (uint64_t)i * sizeof(creeper), sizeof(creeper));
        memcpy(&hurt, hurts + (uint64_t)i * sizeof(hurt), sizeof(hurt));
        if (!save_state_place_fine(zombie.asked, COLLISION_AXES) || creeper.state > MOB_CREEPER_STATE_CHASING ||
            creeper.fuse > MOB_CREEPER_FUSE_TICKS || !save_state_f32s_fine(&hurt.last_hurt, 1u)) {
            return false;
        }
    }
    return true;
}

/* That each is a sex. That the count is the bodies' is save_state_check's, which sees both. */
static bool save_state_check_sexes(const uint8_t *data, uint64_t bytes) {
    save_state_sexes_t head;
    if (bytes < sizeof(head)) {
        return false;
    }
    memcpy(&head, data, sizeof(head));
    if (head.count > ENTITY_STORE_CAPACITY || bytes != sizeof(head) + head.count || head._pad0[0] != 0u ||
        head._pad0[1] != 0u || head._pad0[2] != 0u || head._pad0[3] != 0u) {
        return false;
    }
    for (uint32_t i = 0u; i < head.count; i++) {
        if (data[sizeof(head) + i] >= ENTITY_STORE_SEX_COUNT) {
            return false;
        }
    }
    return true;
}

static bool save_state_check_passive(const uint8_t *data, uint64_t bytes) {
    if (bytes != sizeof(save_state_passive_t) + (uint64_t)ENTITY_STORE_CAPACITY * sizeof(mob_passive_body_t)) {
        return false;
    }
    const uint8_t *const animals = data + sizeof(save_state_passive_t);
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY; i++) {
        mob_passive_body_t animal;
        memcpy(&animal, animals + (uint64_t)i * sizeof(animal), sizeof(animal));
        if (!save_state_place_fine(animal.from, 2u) || !save_state_f32s_fine(&animal.last_health, 1u) ||
            animal.flee > MOB_PASSIVE_FLEE_TICKS + 1u || animal.stroll > MOB_PASSIVE_STROLL_TICKS) {
            return false;
        }
    }
    return true;
}

static bool save_state_check_one(uint32_t id, const uint8_t *data, uint64_t bytes) {
    switch ((save_state_section_id_t)id) {
    case SAVE_STATE_SECTION_TIME:
        return save_state_check_time(data, bytes);
    case SAVE_STATE_SECTION_PLAYER:
        return save_state_check_player(data, bytes);
    case SAVE_STATE_SECTION_ITEMS:
        return save_state_check_items(data, bytes);
    case SAVE_STATE_SECTION_CHESTS:
        return save_state_check_chests(data, bytes);
    case SAVE_STATE_SECTION_FURNACES:
        return save_state_check_furnaces(data, bytes);
    case SAVE_STATE_SECTION_DROPS:
        return save_state_check_drops(data, bytes);
    case SAVE_STATE_SECTION_ARROWS:
        return save_state_check_arrows(data, bytes);
    case SAVE_STATE_SECTION_BODIES:
        return save_state_check_bodies(data, bytes);
    case SAVE_STATE_SECTION_SPAWN:
        return save_state_check_spawn(bytes);
    case SAVE_STATE_SECTION_MINDS:
        return save_state_check_minds(data, bytes);
    case SAVE_STATE_SECTION_NAMES:
        return save_names_are_live(data, bytes);
    case SAVE_STATE_SECTION_CALENDAR:
        return save_state_check_calendar(data, bytes);
    case SAVE_STATE_SECTION_SEXES:
        return save_state_check_sexes(data, bytes);
    case SAVE_STATE_SECTION_PASSIVE:
        return save_state_check_passive(data, bytes);
    case SAVE_STATE_SECTION_ALLOY:
        return save_state_check_alloy(data, bytes);
    }
    return false; /* an id from nowhere: this version has only those */
}

/* All of a file's header but what its numbers mean. */
static save_world_status_t save_state_check_header(const uint8_t *file, uint64_t bytes, uint64_t seed,
                                                   uint64_t generation, save_state_header_t *out) {
    save_state_header_t header;
    if (bytes < sizeof(header)) {
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    memcpy(&header, file, sizeof(header));
    *out = header;
    if (memcmp(header.magic, SAVE_STATE_MAGIC, sizeof(header.magic)) != 0) {
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    if (header.checksum !=
        save_state_hash_more(SAVE_STATE_HASH_START, &header, offsetof(save_state_header_t, checksum))) {
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    if (header.version < SAVE_STATE_VERSION_READ_MIN || header.version > SAVE_STATE_VERSION) {
        return SAVE_WORLD_ERROR_VERSION;
    }
    if (header.header_bytes != sizeof(header) || header.generation != generation ||
        header.sections_bytes != bytes - sizeof(header) ||
        header.sections_hash !=
            save_state_hash_more(SAVE_STATE_HASH_START, file + sizeof(header), header.sections_bytes)) {
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    if (header.seed != seed) {
        return SAVE_WORLD_ERROR_SEED;
    }
    return SAVE_WORLD_OK;
}

/* From version 5 the sexes go with the bodies, one for each, and the animals with the minds; before it neither is there. */
static save_world_status_t save_state_check_animals(const uint8_t *file, uint64_t bytes,
                                                    const save_state_header_t *header, uint32_t seen) {
    const bool animals = header->version >= SAVE_STATE_VERSION_FIRST_WITH_ANIMALS;
    const bool bodies = (seen & (1u << SAVE_STATE_SECTION_BODIES)) != 0u;
    const bool minds = (seen & (1u << SAVE_STATE_SECTION_MINDS)) != 0u;
    const bool sexes = (seen & (1u << SAVE_STATE_SECTION_SEXES)) != 0u;
    const bool passive = (seen & (1u << SAVE_STATE_SECTION_PASSIVE)) != 0u;
    if (sexes != (animals && bodies) || passive != (animals && minds)) {
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    if (sexes) {
        uint64_t bodies_bytes = 0u, sexes_bytes = 0u;
        const uint8_t *const body = save_state_section_find(file, bytes, SAVE_STATE_SECTION_BODIES, &bodies_bytes);
        const uint8_t *const sex = save_state_section_find(file, bytes, SAVE_STATE_SECTION_SEXES, &sexes_bytes);
        save_state_bodies_t body_head;
        save_state_sexes_t sex_head;
        memcpy(&body_head, body, sizeof(body_head));
        memcpy(&sex_head, sex, sizeof(sex_head));
        if (body_head.count != sex_head.count) {
            return SAVE_WORLD_ERROR_DAMAGED;
        }
    }
    return SAVE_WORLD_OK;
}

save_world_status_t save_state_check(const uint8_t *file, uint64_t bytes, uint64_t seed, uint64_t generation) {
    save_state_header_t header;
    const save_world_status_t head_status = save_state_check_header(file, bytes, seed, generation, &header);
    if (head_status != SAVE_WORLD_OK) {
        return head_status;
    }
    if (header.table_hash != save_state_table_hash()) {
        return SAVE_WORLD_ERROR_TABLES;
    }
    uint32_t seen = 0u;
    uint32_t count = 0u;
    uint64_t at = sizeof(header);
    while (at < bytes) {
        save_state_section_t section;
        if (bytes - at < sizeof(section)) {
            return SAVE_WORLD_ERROR_DAMAGED;
        }
        memcpy(&section, file + at, sizeof(section));
        at += sizeof(section);
        if (section.bytes > bytes - at || section.id == 0u || section.id > SAVE_STATE_SECTION_COUNT ||
            (seen & (1u << section.id)) != 0u) {
            return SAVE_WORLD_ERROR_DAMAGED;
        }
        seen |= 1u << section.id;
        if (!save_state_check_one(section.id, file + at, section.bytes)) {
            LOG_ERROR("save: part %" PRIu32 " of a state file says what cannot be", section.id);
            return SAVE_WORLD_ERROR_DAMAGED;
        }
        const uint64_t padded = (section.bytes + 7u) & ~(uint64_t)7u;
        if (padded > bytes - at) {
            return SAVE_WORLD_ERROR_DAMAGED;
        }
        at += padded;
        count++;
    }
    if (count != header.section_count || (seen & (1u << SAVE_STATE_SECTION_TIME)) == 0u ||
        (seen & (1u << SAVE_STATE_SECTION_NAMES)) == 0u) {
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    /* Every file of a version with a calendar holds one, as every file holds its time, and none from before has one. */
    const bool calendar = (seen & (1u << SAVE_STATE_SECTION_CALENDAR)) != 0u;
    if (calendar != (header.version >= SAVE_STATE_VERSION_FIRST_WITH_CALENDAR)) {
        return SAVE_WORLD_ERROR_DAMAGED;
    }
    return save_state_check_animals(file, bytes, &header, seen);
}

/* ---- Opening ---------------------------------------------------------------------------------------------------------- */

/* A file just read, if it was saved with other tables: its numbers in this build's, its hashes those of what it is now. In memory only: the next save writes it. Anything
 * wrong with it that is not its tables is left for save_state_check to say. */
static save_world_status_t save_state_translate_kept(save_state_t *state, uint64_t *in_out_bytes, uint64_t generation) {
    save_state_header_t header;
    if (save_state_check_header(state->borrowed_image, *in_out_bytes, state->seed, generation, &header) !=
            SAVE_WORLD_OK ||
        header.table_hash == save_state_table_hash()) {
        return SAVE_WORLD_OK;
    }
    uint32_t section_count = 0u;
    const save_world_status_t translated =
        save_state_translate(state->borrowed_image, in_out_bytes, SAVE_STATE_IMAGE_BYTES, state->borrowed_maps,
                             state->borrowed_directory, &section_count);
    if (translated != SAVE_WORLD_OK) {
        return translated;
    }
    header.table_hash = save_state_table_hash();
    header.sections_bytes = *in_out_bytes - sizeof(header);
    header.section_count = section_count;
    header.sections_hash =
        save_state_hash_more(SAVE_STATE_HASH_START, state->borrowed_image + sizeof(header), header.sections_bytes);
    header.checksum = save_state_hash_more(SAVE_STATE_HASH_START, &header, offsetof(save_state_header_t, checksum));
    memcpy(state->borrowed_image, &header, sizeof(header));
    return SAVE_WORLD_OK;
}

/* Why a file is refused, in words, where the words are not the same for all of them. */
static void save_state_refusal_log(const uint8_t *file, uint64_t bytes, const char *path) {
    save_state_header_t header;
    if (bytes >= sizeof(header)) {
        memcpy(&header, file, sizeof(header));
        if (header.version < SAVE_STATE_VERSION_FIRST_WITH_NAMES) {
            LOG_ERROR("save: %s is in version %" PRIu32
                      " of the format, from before a state kept the names of its tables, so nothing says what its "
                      "numbers mean. It is left as it was",
                      path, header.version);
        }
    }
}

/* Whether a name is one this module makes: state.<digits>, with .tmp or without. Gives its number. */
static bool save_state_name_parse(const char *name, uint64_t *out_generation, bool *out_temporary) {
    if (strncmp(name, "state.", 6u) != 0 || name[6] < '0' || name[6] > '9') {
        return false;
    }
    char *end = nullptr;
    const uint64_t generation = strtoull(name + 6, &end, 10);
    *out_generation = generation;
    *out_temporary = strcmp(end, ".tmp") == 0;
    return *end == '\0' || *out_temporary;
}

/* Whatever another save left. Not reaching a name now only leaves it for the next open. */
static void save_state_tidy(const char *directory, uint64_t generation) {
    DIR *const handle = opendir(directory);
    if (handle == nullptr) {
        return;
    }
    for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
        uint64_t number = 0u;
        bool temporary = false;
        if (save_state_name_parse(entry->d_name, &number, &temporary) && (temporary || number != generation)) {
            char path[FILE_PATH_BYTES];
            const int32_t written = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            if (written > 0 && written < (int32_t)sizeof(path)) {
                (void)file_remove(path);
            }
        }
    }
    (void)closedir(handle);
}

save_world_status_t save_state_open(save_state_t *state, arena_t *arena, const char *world_directory, uint64_t seed,
                                    uint64_t generation) {
    memset(state, 0, sizeof(*state));
    const uint64_t directory_bytes = strlen(world_directory) + 1u;
    char path[FILE_PATH_BYTES];
    if (directory_bytes > FILE_PATH_BYTES || !save_state_path(path, world_directory, generation)) {
        LOG_ERROR("save: the path of the state in %s is too long", world_directory);
        return SAVE_WORLD_ERROR_PATH;
    }
    state->borrowed_image = arena_alloc(arena, SAVE_STATE_IMAGE_BYTES, 64u);
    state->borrowed_directory = arena_alloc(arena, directory_bytes, 1u);
    state->borrowed_maps = arena_alloc(arena, SAVE_STATE_TRANSLATE_MAPS_BYTES, alignof(uint16_t));
    memcpy(state->borrowed_directory, world_directory, directory_bytes);
    state->seed = seed;
    state->table_hash = save_state_table_hash();
    uint64_t bytes = 0u;
    const file_status_t status = file_read_whole(path, state->borrowed_image, SAVE_STATE_IMAGE_BYTES, &bytes);
    if (status == FILE_OK) {
        save_world_status_t checked = save_state_translate_kept(state, &bytes, generation);
        if (checked == SAVE_WORLD_OK) {
            checked = save_state_check(state->borrowed_image, bytes, seed, generation);
        }
        if (checked != SAVE_WORLD_OK) {
            if (checked == SAVE_WORLD_ERROR_VERSION) {
                save_state_refusal_log(state->borrowed_image, bytes, path);
            }
            LOG_ERROR("save: %s cannot be believed: %s", path, save_world_status_name(checked));
            memset(state, 0, sizeof(*state));
            return checked;
        }
        state->image_bytes = bytes;
        state->kept = 1u;
        LOG_INFO("save: the state of %s was read", world_directory);
    } else if (status == FILE_ERROR_NOT_FOUND) {
        LOG_INFO("save: the state of %s has not been saved yet", world_directory);
    } else if (status == FILE_ERROR_TOO_LARGE) {
        LOG_ERROR("save: %s is larger than any state", path);
        memset(state, 0, sizeof(*state));
        return SAVE_WORLD_ERROR_DAMAGED;
    } else {
        LOG_ERROR("save: %s could not be read: %s", path, file_status_name(status));
        const uint8_t said = status;
        memset(state, 0, sizeof(*state));
        state->file_status = said;
        return SAVE_WORLD_ERROR_FILE;
    }
    save_state_tidy(world_directory, generation);
    state->open = 1u;
    return SAVE_WORLD_OK;
}

void save_state_close(save_state_t *state) {
    memset(state, 0, sizeof(*state));
}

/* ---- Putting back ------------------------------------------------------------------------------------------------------- */

bool save_state_kept_start(const save_state_t *state, float64_t out_feet[COLLISION_AXES], float32_t *out_yaw,
                           float32_t *out_pitch) {
    if (state->open == 0u || state->kept == 0u) {
        return false;
    }
    uint64_t bytes = 0u;
    const uint8_t *const player =
        save_state_section_find(state->borrowed_image, state->image_bytes, SAVE_STATE_SECTION_PLAYER, &bytes);
    const uint8_t *const time =
        save_state_section_find(state->borrowed_image, state->image_bytes, SAVE_STATE_SECTION_TIME, &bytes);
    if (player == nullptr || time == nullptr) {
        return false;
    }
    memcpy(out_feet, player + offsetof(save_state_player_t, movement) + offsetof(player_movement_t, position),
           COLLISION_AXES * sizeof(float64_t));
    save_state_time_t kept;
    memcpy(&kept, time, sizeof(kept));
    *out_yaw = kept.yaw;
    *out_pitch = kept.pitch;
    return true;
}

static void save_state_in_player(player_t *player, const uint8_t *data) {
    memcpy(&player->movement, data + offsetof(save_state_player_t, movement), sizeof(player->movement));
    memcpy(&player->survival, data + offsetof(save_state_player_t, survival), sizeof(player->survival));
    memcpy(&player->interaction, data + offsetof(save_state_player_t, interaction), sizeof(player->interaction));
    memcpy(&player->combat, data + offsetof(save_state_player_t, combat), sizeof(player->combat));
    memcpy(&player->tick_count, data + offsetof(save_state_player_t, tick_count), sizeof(player->tick_count));
    /* What a screen shows of the body, before its first tick. */
    player_vitals_publish(player);
}

static void save_state_in_items(item_t *item, item_stack_t *held, const uint8_t *data) {
    save_state_items_t head;
    memcpy(&head, data, sizeof(head));
    item->crafting_hands = head.crafting_hands;
    item->crafting_hands.station = RECIPE_STATION_HANDS; /* a file from before the hands had their own */
    item->crafting_table = head.crafting_table;
    *item->borrowed_crafting_bench = head.crafting_bench;
    item->equipment = head.equipment;
    item->inventory.added_count = head.added_count;
    item->inventory.removed_count = head.removed_count;
    item->inventory.refused_count = head.refused_count;
    item->spilled_count = head.spilled_count;
    item->chests.refused_count = head.chests_refused_count;
    item->furnaces.smelted_count = head.smelted_count;
    item->furnaces.burned_count = head.burned_count;
    item->furnaces.refused_count = head.furnaces_refused_count;
    memcpy(item->inventory.borrowed_slots, head.slots, sizeof(head.slots));
    item->inventory.selected = head.selected;
    if (held != nullptr) {
        *held = head.held;
    }
    /* What each makes, from this build's recipes, as those may be others than a file's. With the same recipes this is what the file said. */
    crafting_grid_changed(&item->crafting_hands);
    crafting_grid_changed(&item->crafting_table);
    crafting_grid_changed(item->borrowed_crafting_bench);
}

static void save_state_in_drops(item_drop_store_t *drops, const uint8_t *data) {
    save_state_drops_t head;
    memcpy(&head, data, sizeof(head));
    drops->random.state = head.random;
    drops->tick_count = head.tick_count;
    drops->dropped_count = head.dropped_count;
    drops->picked_up_count = head.picked_up_count;
    drops->despawned_count = head.despawned_count;
    drops->destroyed_count = head.destroyed_count;
    drops->lost_count = head.lost_count;
    drops->merged_count = head.merged_count;
    drops->count = head.count;
    drops->next_id = head.next_id;
    memcpy(drops->borrowed_items, data + sizeof(head), (uint64_t)head.count * sizeof(item_drop_t));
}

static void save_state_in_arrows(projectile_store_t *arrows, const uint8_t *data) {
    save_state_arrows_t head;
    memcpy(&head, data, sizeof(head));
    arrows->random.state = head.random;
    arrows->fired_count = head.fired_count;
    arrows->body_hits = head.body_hits;
    arrows->target_hits = head.target_hits;
    arrows->expired_count = head.expired_count;
    arrows->burned_count = head.burned_count;
    arrows->lost_count = head.lost_count;
    arrows->stuck_count = head.stuck_count;
    arrows->count = head.count;
    memcpy(arrows->borrowed_items, data + sizeof(head), (uint64_t)head.count * sizeof(projectile_t));
}

static void save_state_in_bodies(entity_t *entity, const uint8_t *data) {
    save_state_bodies_t head;
    memcpy(&head, data, sizeof(head));
    entity_store_t *const store = &entity->store;
    store->spawned_count = head.spawned_count;
    store->removed_count = head.removed_count;
    store->refused_count = head.refused_count;
    store->count = head.count;
    store->free_head = head.free_head;
    entity->explosion.random.state = head.blast_random;
    entity->explosion.broken_count = head.broken_count;
    entity->explosion.blast_count = head.blast_count;
    const uint8_t *cursor = data + sizeof(head);
    const uint64_t table_bytes = ENTITY_STORE_CAPACITY * sizeof(uint32_t);
    memcpy(store->borrowed_slot_index, cursor, table_bytes);
    cursor += table_bytes;
    memcpy(store->borrowed_slot_generation, cursor, table_bytes);
    cursor += table_bytes;
    memcpy(store->borrowed_free_slots, cursor, table_bytes);
    cursor += table_bytes;
    save_state_field_t fields[SAVE_STATE_BODY_FIELDS];
    save_state_body_fields(store, fields);
    for (uint32_t i = 0u; i < SAVE_STATE_BODY_FIELDS; i++) {
        const uint64_t bytes = (uint64_t)head.count * fields[i].element_bytes;
        memcpy(fields[i].borrowed_array, cursor, bytes);
        cursor += bytes;
    }
}

static void save_state_in_spawn(mob_spawn_t *spawn, const uint8_t *data) {
    save_state_spawn_t head;
    memcpy(&head, data, sizeof(head));
    spawn->random.state = head.random;
    spawn->spawned_count = head.spawned_count;
    spawn->despawned_count = head.despawned_count;
    spawn->refused_count = head.refused_count;
    memcpy(spawn->borrowed_idle, data + sizeof(head), ENTITY_STORE_CAPACITY * sizeof(uint32_t));
}

static void save_state_in_minds(mob_t *mob, const uint8_t *data) {
    save_state_minds_t head;
    memcpy(&head, data, sizeof(head));
    mob->zombie.random.state = head.zombie_random;
    mob->zombie.struck_count = head.zombie_struck;
    mob->zombie.died_count = head.zombie_died;
    mob->zombie.dropped_count = head.zombie_dropped;
    mob->skeleton.random.state = head.skeleton_random;
    mob->skeleton.shot_count = head.skeleton_shot;
    mob->skeleton.died_count = head.skeleton_died;
    mob->skeleton.dropped_count = head.skeleton_dropped;
    mob->creeper.random.state = head.creeper_random;
    mob->creeper.burst_count = head.creeper_burst;
    mob->creeper.died_count = head.creeper_died;
    mob->creeper.dropped_count = head.creeper_dropped;
    mob->combat.landed_count = head.combat_landed;
    mob->combat.killed_count = head.combat_killed;
    mob->combat.knocked_count = head.combat_knocked;
    const uint8_t *cursor = data + sizeof(head);
    memcpy(mob->zombie.borrowed_bodies, cursor, ENTITY_STORE_CAPACITY * sizeof(mob_zombie_body_t));
    cursor += ENTITY_STORE_CAPACITY * sizeof(mob_zombie_body_t);
    memcpy(mob->skeleton.borrowed_bodies, cursor, ENTITY_STORE_CAPACITY * sizeof(mob_skeleton_body_t));
    cursor += ENTITY_STORE_CAPACITY * sizeof(mob_skeleton_body_t);
    memcpy(mob->creeper.borrowed_bodies, cursor, ENTITY_STORE_CAPACITY * sizeof(mob_creeper_body_t));
    cursor += ENTITY_STORE_CAPACITY * sizeof(mob_creeper_body_t);
    memcpy(mob->combat.borrowed_bodies, cursor, ENTITY_STORE_CAPACITY * sizeof(mob_combat_body_t));
}

bool save_state_apply(const save_state_t *state, save_state_parts_t *parts) {
    if (state->open == 0u || state->kept == 0u) {
        return false;
    }
    const uint8_t *const file = state->borrowed_image;
    const uint64_t file_bytes = state->image_bytes;
    uint64_t bytes = 0u;
    const uint8_t *data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_TIME, &bytes);
    if (data != nullptr) {
        memcpy(&parts->time, data, sizeof(parts->time));
    }
    data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_CALENDAR, &bytes);
    if (data != nullptr) {
        memcpy(&parts->calendar, data, sizeof(parts->calendar));
    } else {
        LOG_INFO("save: %s has no calendar, and starts one", state->borrowed_directory);
    }
    data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_PLAYER, &bytes);
    if (data != nullptr && parts->borrowed_player != nullptr) {
        save_state_in_player(parts->borrowed_player, data);
    }
    if (parts->borrowed_item != nullptr) {
        item_t *const item = parts->borrowed_item;
        data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_ITEMS, &bytes);
        if (data != nullptr) {
            save_state_in_items(item, parts->borrowed_held, data);
        }
        data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_ALLOY, &bytes);
        if (data != nullptr) {
            memcpy(item->borrowed_crafting_alloy, data, sizeof(crafting_alloy_t));
            crafting_alloy_changed(item->borrowed_crafting_alloy);
        }
        data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_CHESTS, &bytes);
        if (data != nullptr) {
            memcpy(item->chests.borrowed_chests, data, bytes);
            item->chests.count = (uint32_t)(bytes / sizeof(container_chest_t));
        }
        data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_FURNACES, &bytes);
        if (data != nullptr) {
            memcpy(item->furnaces.borrowed_furnaces, data, bytes);
            item->furnaces.count = (uint32_t)(bytes / sizeof(smelting_furnace_t));
        }
    }
    if (parts->borrowed_entity != nullptr) {
        data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_DROPS, &bytes);
        if (data != nullptr) {
            save_state_in_drops(&parts->borrowed_entity->item_drops, data);
        }
        data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_ARROWS, &bytes);
        if (data != nullptr) {
            save_state_in_arrows(&parts->borrowed_entity->projectiles, data);
        }
        data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_BODIES, &bytes);
        if (data != nullptr) {
            save_state_in_bodies(parts->borrowed_entity, data);
        }
        /* A file from before version 5 has none: every body then was neither male nor female. */
        entity_store_t *const store = &parts->borrowed_entity->store;
        memset(store->borrowed_sex, ENTITY_STORE_SEX_NONE, store->count);
        data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_SEXES, &bytes);
        if (data != nullptr) {
            memcpy(store->borrowed_sex, data + sizeof(save_state_sexes_t), bytes - sizeof(save_state_sexes_t));
        }
    }
    data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_SPAWN, &bytes);
    if (data != nullptr && parts->borrowed_spawn != nullptr) {
        save_state_in_spawn(parts->borrowed_spawn, data);
    }
    data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_MINDS, &bytes);
    if (data != nullptr && parts->borrowed_mob != nullptr) {
        save_state_in_minds(parts->borrowed_mob, data);
    }
    data = save_state_section_find(file, file_bytes, SAVE_STATE_SECTION_PASSIVE, &bytes);
    if (data != nullptr && parts->borrowed_mob != nullptr) {
        mob_passive_t *const passive = &parts->borrowed_mob->passive;
        save_state_passive_t head;
        memcpy(&head, data, sizeof(head));
        passive->borrowed_tally->random.state = head.random;
        passive->borrowed_tally->fled_count = head.fled;
        passive->borrowed_tally->died_count = head.died;
        passive->borrowed_tally->dropped_count = head.dropped;
        memcpy(passive->borrowed_bodies, data + sizeof(head), ENTITY_STORE_CAPACITY * sizeof(mob_passive_body_t));
    }
    return true;
}
