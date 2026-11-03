/* ctest run_save_state. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "file/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "log/index.h"
#include "save/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "tables/mobs/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum : uint64_t {
    SS_PATH_BYTES = 512u,
    SS_FILE_BYTES = 8ull << 20,
    SS_COLUMN_VOXELS = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS,
};
enum : uint32_t {
    SS_ON_CELL_X = 3u,
    SS_KILL_ROUNDS = 12u,
    SS_KILL_INTERVAL = 4u,
};
static const uint64_t SS_SEED = 9191u;

/* ---- Small things ------------------------------------------------------------------------------------------------------ */

/* A separate FNV-1a, so that a change to the one under test is not also made here. */
static uint64_t ss_fnv(const void *data, uint64_t bytes) {
    const uint8_t *const cursor = data;
    uint64_t hash = 14695981039346656037ull;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ cursor[i]) * 1099511628211ull;
    }
    return hash;
}

static char *ss_join(test_context_t *t, const char *left, const char *right) {
    char *const path = test_alloc(t, SS_PATH_BYTES);
    snprintf(path, SS_PATH_BYTES, "%s/%s", left, right);
    return path;
}

static char *ss_state_path(test_context_t *t, const char *root, uint64_t generation) {
    char *const path = test_alloc(t, SS_PATH_BYTES);
    snprintf(path, SS_PATH_BYTES, "%s/state.%" PRIu64, root, generation);
    return path;
}

static char *ss_temp_directory(test_context_t *t) {
    char *const directory = test_alloc(t, SS_PATH_BYTES);
    snprintf(directory, SS_PATH_BYTES, "/tmp/mc_save_state_XXXXXX");
    return mkdtemp(directory);
}

static void ss_temp_remove(test_context_t *t, const char *directory) {
    if (directory == nullptr || strncmp(directory, "/tmp/mc_save_state_", 19u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", (char *)directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static uint8_t *ss_slurp(test_context_t *t, const char *path, uint64_t *out_bytes) {
    uint8_t *const bytes = test_alloc(t, SS_FILE_BYTES);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, SS_FILE_BYTES, out_bytes), FILE_OK)) {
        *out_bytes = 0u;
    }
    return bytes;
}

/* How many state.<g> are in a directory, leaving out those on their way. */
static uint32_t ss_state_files(const char *directory) {
    DIR *const handle = opendir(directory);
    if (handle == nullptr) {
        return 0u;
    }
    uint32_t count = 0u;
    for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
        const uint64_t bytes = strlen(entry->d_name);
        const bool temporary = bytes > 4u && strcmp(entry->d_name + bytes - 4u, ".tmp") == 0;
        if (strncmp(entry->d_name, "state.", 6u) == 0 && !temporary) {
            count++;
        }
    }
    (void)closedir(handle);
    return count;
}

/* Errors a test sets out to cause, off the screen. Returns what to give back. */
static int32_t ss_quiet_begin(void) {
    return log_set_fd(open("/dev/null", O_WRONLY | O_CLOEXEC));
}
static void ss_quiet_end(int32_t previous) {
    const int32_t quiet = log_set_fd(previous);
    (void)close(quiet);
}

/* ---- A world's worth of state ---------------------------------------------------------------------------------------- */

typedef struct ss_rig_t {
    test_world_t w;              /* 0 */
    player_t *player;            /* 24 */
    item_t *item;                /* 32 */
    entity_t *entity;            /* 40 */
    mob_t *mob;                  /* 48 */
    mob_spawn_t *spawn;          /* 56 */
    item_stack_t *held;          /* 64 */
    save_t *save;                /* 72 */
    job_system_t *job;           /* 80: only for those that run autosaves */
    thread_topology_t *topology; /* 88 */
    const char *root;            /* 96 */
    save_state_parts_t parts;    /* 104 */
} ss_rig_t;
static_assert(sizeof(ss_rig_t) == 176);

static bool ss_rig_begin(test_context_t *t, ss_rig_t *rig, const char *root, bool with_jobs) {
    memset(rig, 0, sizeof(*rig));
    if (!test_world_begin(t, &rig->w)) {
        return false;
    }
    rig->root = root;
    rig->player = test_world_aligned(t, sizeof(player_t));
    rig->item = test_world_aligned(t, sizeof(item_t));
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    rig->mob = test_world_aligned(t, sizeof(mob_t));
    rig->spawn = test_alloc(t, sizeof(mob_spawn_t));
    rig->held = test_alloc(t, sizeof(item_stack_t));
    rig->save = test_world_aligned(t, sizeof(save_t));
    test_world_fill(t, &rig->w, -16, -16, 0, 31, 31, 0, BLOCK_ID_STONE);
    player_init(rig->player, rig->w.physics);
    if (!CHECK(item_init(rig->item)) || !CHECK(entity_init(rig->entity, rig->w.physics)) ||
        !CHECK(mob_init(rig->mob, nullptr)) || !CHECK(save_init(rig->save))) {
        return false;
    }
    if (with_jobs) {
        rig->job = test_world_aligned(t, sizeof(job_system_t));
        rig->topology = test_alloc(t, sizeof(thread_topology_t));
        if (!CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology))) {
            return false;
        }
    }
    if (!CHECK_EQ_U64(save_open(rig->save, root, SS_SEED), SAVE_WORLD_OK)) {
        return false;
    }
    /* What a column coming in has: what is saved of it over what generates there, or the next save of those sections would lose it. */
    uint16_t *const states = test_alloc(t, SS_COLUMN_VOXELS * sizeof(uint16_t));
    uint8_t *const scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);
    const uint64_t mask = save_world_column_overlay(&rig->save->world, 0, 0, states, scratch);
    column_t *const column = world_map_column_at(rig->w.map, 0, 0);
    for (uint32_t section = 0u; section < COLUMN_SECTIONS && column != nullptr; section++) {
        if ((mask & (1ull << section)) != 0u) {
            CHECK(section_blocks_import(&column->sections[section], &rig->w.world->section_pool,
                                        states + (uint64_t)section * SECTION_VOXELS));
        }
    }
    player_spawn(rig->player, 3.5f64, 4.5f64, 1.0f64);
    item_open(rig->item);
    item_world_begin(rig->item, &rig->w.world->edit);
    entity_open(rig->entity, 41u);
    mob_spawn_start(rig->spawn, &rig->entity->arena_world, 43u);
    mob_open(rig->mob, rig->w.world, 47u);
    rig->parts = (save_state_parts_t){
        .borrowed_player = rig->player,
        .borrowed_item = rig->item,
        .borrowed_entity = rig->entity,
        .borrowed_mob = rig->mob,
        .borrowed_spawn = rig->spawn,
        .borrowed_held = rig->held,
    };
    return true;
}

static void ss_rig_end(ss_rig_t *rig) {
    mob_close(rig->mob);
    mob_deinit(rig->mob);
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    item_close(rig->item);
    item_deinit(rig->item);
    player_deinit(rig->player);
    save_deinit(rig->save);
    if (rig->job != nullptr) {
        job_deinit(rig->job);
    }
    test_world_end(&rig->w);
}

/* Something in every part, none of it as a part starts. */
static void ss_fill(ss_rig_t *rig) {
    player_t *const player = rig->player;
    player->movement.position[0] = 5.25f64;
    player->movement.velocity[2] = -0.125f64;
    player->movement.fall_distance = 1.5f64;
    player->survival.health = 13.5f32;
    player->survival.food = 11u;
    player->survival.saturation = 2.5f32;
    player->survival.has_bed = 1u;
    player->survival.bed_cell[1] = -7;
    player->interaction.mined_count = 77u;
    player->interaction.progress = 123u;
    player_interaction_hand_set(&player->interaction, BLOCK_ID_STONE, 9u);
    player_interaction_drops_spawn(&player->interaction, BLOCK_ID_STONE, 1, 2, 3);
    player->combat.since_swing = 7u;
    player->tick_count = 17u;

    item_t *const item = rig->item;
    (void)inventory_add(&item->inventory, ITEM_ID_DIRT, 40u);
    (void)inventory_add(&item->inventory, ITEM_ID_COAL, 5u);
    item_stack_t sword = item_stack_make(ITEM_ID_DIAMOND_SWORD, 1u);
    sword.wear = 9u;
    *inventory_slot(&item->inventory, 20u) = sword;
    inventory_select(&item->inventory, 4u);
    item_stack_t helmet = item_stack_make(ITEM_ID_IRON_HELMET, 1u);
    helmet.wear = 3u;
    item->equipment.armor[0] = helmet;
    item->equipment.tools_broken_count = 2u;
    item->crafting_hands.cells[0] = item_stack_make(ITEM_ID_DIRT, 2u);
    item->crafting_table.cells[8] = item_stack_make(ITEM_ID_COAL, 3u);
    item->crafting_table.crafted_count = 4u;
    *rig->held = item_stack_make(ITEM_ID_DIRT, 3u);
    item->spilled_count = 6u;
    container_chest_t *const chest = container_open(&item->chests, 7, 8, 1);
    *container_slot(chest, 26u) = item_stack_make(ITEM_ID_STONE, 12u);
    (void)container_open(&item->chests, -7, -8, 1);
    smelting_furnace_t *const furnace = smelting_open(&item->furnaces, 9, 9, 1);
    furnace->slots[SMELTING_SLOT_INPUT] = item_stack_make(ITEM_ID_RAW_IRON, 2u);
    furnace->slots[SMELTING_SLOT_FUEL] = item_stack_make(ITEM_ID_COAL, 2u);
    furnace->burn_ticks = 17u;
    furnace->burn_total = 1600u;
    furnace->cook_ticks = 5u;
    furnace->cooking = ITEM_ID_RAW_IRON;

    entity_t *const entity = rig->entity;
    const float64_t drop_at[3] = {6.5f64, 6.5f64, 3.0f64};
    item_drop_spawn(&entity->item_drops, 1u, 70u, drop_at);
    const float64_t feet[3] = {10.5f64, 12.5f64, 1.0f64};
    const entity_handle_t first = mob_spawn_body(rig->spawn, &entity->store, MOB_ID_ZOMBIE, feet, 0.5f32);
    const float64_t feet2[3] = {12.5f64, 12.5f64, 3.0f64};
    const entity_handle_t second = mob_spawn_body(rig->spawn, &entity->store, MOB_ID_SKELETON, feet2, 1.5f32);
    const float64_t feet3[3] = {14.5f64, 10.5f64, 1.0f64};
    const entity_handle_t third = mob_spawn_body(rig->spawn, &entity->store, MOB_ID_CREEPER, feet3, 2.5f32);
    /* So that the slots are not all in order. */
    (void)entity_store_remove(&entity->store, second);
    const entity_handle_t fourth = mob_spawn_body(rig->spawn, &entity->store, MOB_ID_ZOMBIE, feet2, 3.5f32);
    entity_store_wish_set(&entity->store, 0u, 0.05f64, 0.0f64);
    entity->store.borrowed_health[1] = 4.5f32;
    const float64_t origin[3] = {2.5f64, 2.5f64, 5.0f64};
    const float64_t along[3] = {1.0f64, 0.25f64, 0.1f64};
    projectile_fire(&entity->projectiles, third, origin, along, 1.6f64, 1.0f64);
    entity->explosion.broken_count = 5u;
    entity->explosion.blast_count = 2u;

    mob_t *const mob = rig->mob;
    mob->zombie.borrowed_bodies[entity_handle_slot(first)].unseen = 3u;
    mob->zombie.borrowed_bodies[entity_handle_slot(fourth)].asked[1] = 2.5f64;
    mob->skeleton.borrowed_bodies[entity_handle_slot(second)].draw = 4u;
    mob->creeper.borrowed_bodies[entity_handle_slot(third)].fuse = 2u;
    mob->combat.borrowed_bodies[entity_handle_slot(first)].last_hurt = 1.5f32;
    mob->combat.landed_count = 8u;
    rig->spawn->borrowed_idle[entity_handle_slot(third)] = 99u;

    rig->parts.time = (save_state_time_t){
        .world_ticks = 1234u, .day_units = 0x12345678u, .yaw = 1.25f32, .pitch = -0.5f32, ._pad0 = {0}};
}

static void ss_no_spill(void *context, const item_stack_t *stack, const int32_t cell[3]) {
    (void)context;
    (void)stack;
    (void)cell;
}

/* What goes on, with nobody at the controls and no mind ticking, as those ask for paths: see README.md. */
static void ss_tick(ss_rig_t *rig, int64_t tick) {
    item_world_tick(rig->item, &rig->w.world->edit, ss_no_spill, nullptr);
    entity_tick(rig->entity, rig->w.map);
    projectile_tick(&rig->entity->projectiles, &rig->entity->store, &rig->entity->grid, rig->w.map, nullptr, nullptr,
                    nullptr);
    player_tick(rig->player, rig->w.map, tick * PLAYER_TICK_NS);
    player_survive_tick(rig->player, rig->w.map);
    rig->parts.time.world_ticks++;
}

/* ---- The bytes ----------------------------------------------------------------------------------------------------------- */

TEST(a_file_is_these_bytes_by_hand) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    save_t *const save = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    CHECK_EQ_U64(ss_state_files(root), 0u);
    const save_state_parts_t parts = {.time = {.world_ticks = 0x0102030405060708ull,
                                               .day_units = 0x11223344u,
                                               .yaw = 1.5f32,
                                               .pitch = -2.0f32,
                                               ._pad0 = {0}}};
    save_state_capture(&save->state, &parts);
    REQUIRE(save_state_write(&save->state, 0u) == SAVE_WORLD_OK);
    uint64_t bytes = 0u;
    const uint8_t *const file = ss_slurp(t, ss_state_path(t, root, 0u), &bytes);
    REQUIRE(bytes == 112u);

    uint8_t expected[112] = {'M', 'C', 'S', 'T', 'A', 'T', 'E', '1'};
    memcpy(expected + 8, &SS_SEED, 8u);
    /* generation is 0 at 16 */
    const uint64_t table_hash = save_state_table_hash();
    memcpy(expected + 24, &table_hash, 8u);
    const uint64_t sections_bytes = 40u;
    memcpy(expected + 32, &sections_bytes, 8u);
    const uint32_t version = 1u, header_bytes = 72u, section_count = 1u;
    memcpy(expected + 48, &version, 4u);
    memcpy(expected + 52, &header_bytes, 4u);
    memcpy(expected + 56, &section_count, 4u);
    /* The section: 24 bytes of id 1, and them. */
    const uint64_t section_bytes = 24u;
    const uint32_t section_id = 1u;
    memcpy(expected + 72, &section_bytes, 8u);
    memcpy(expected + 80, &section_id, 4u);
    memcpy(expected + 88, &parts.time.world_ticks, 8u);
    memcpy(expected + 96, &parts.time.day_units, 4u);
    memcpy(expected + 100, &parts.time.yaw, 4u);
    memcpy(expected + 104, &parts.time.pitch, 4u);
    const uint64_t sections_hash = ss_fnv(expected + 72, 40u);
    memcpy(expected + 40, &sections_hash, 8u);
    const uint64_t checksum = ss_fnv(expected, 64u);
    memcpy(expected + 64, &checksum, 8u);
    CHECK_MEM_EQ(file, expected, 112u);
    CHECK_EQ_U64(save_state_check(file, bytes, SS_SEED, 0u), SAVE_WORLD_OK);
    /* The same number is not the same moment if it says it is another save. */
    CHECK_EQ_U64(save_state_check(file, bytes, SS_SEED, 1u), SAVE_WORLD_ERROR_DAMAGED);
    save_deinit(save);
    ss_temp_remove(t, root);
}

TEST(every_part_reloads_byte_for_byte_and_goes_on_the_same) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    ss_rig_t *const first = test_alloc(t, sizeof(ss_rig_t));
    REQUIRE(ss_rig_begin(t, first, root, false));
    ss_fill(first);
    save_state_capture(&first->save->state, &first->parts);
    REQUIRE(save_state_write(&first->save->state, 0u) == SAVE_WORLD_OK);
    uint64_t bytes = 0u;
    const uint8_t *const file = ss_slurp(t, ss_state_path(t, root, 0u), &bytes);
    printf("  %" PRIu64 " bytes, of %" PRIu64 " at the most\n", bytes, (uint64_t)SAVE_STATE_IMAGE_BYTES);
    REQUIRE(bytes > sizeof(save_state_header_t));
    /* Every section is there. */
    for (uint32_t id = 1u; id <= SAVE_STATE_SECTION_COUNT; id++) {
        uint64_t section_bytes = 0u;
        CHECK(save_state_section_find(file, bytes, id, &section_bytes) != nullptr);
    }
    /* What it does next, from here. */
    for (int64_t i = 1; i <= 40; i++) {
        ss_tick(first, i);
    }
    save_state_capture(&first->save->state, &first->parts);
    REQUIRE(save_state_write(&first->save->state, 0u + 1000u) == SAVE_WORLD_OK);
    uint64_t later_bytes = 0u;
    const uint8_t *const later = ss_slurp(t, ss_state_path(t, root, 1000u), &later_bytes);
    REQUIRE(file_remove(ss_state_path(t, root, 1000u)) == FILE_OK);
    CHECK(later_bytes != bytes || memcmp(later, file, bytes) != 0); /* it did move */
    ss_rig_end(first);

    /* A new rig from that file alone. */
    ss_rig_t *const second = test_alloc(t, sizeof(ss_rig_t));
    REQUIRE(ss_rig_begin(t, second, root, false));
    CHECK_EQ_U64(second->save->state.kept, 1u);
    float64_t feet[3];
    float32_t yaw = 0.0f32, pitch = 0.0f32;
    REQUIRE(save_state_kept_start(&second->save->state, feet, &yaw, &pitch));
    CHECK(feet[0] == 5.25f64 && feet[1] == 4.5f64 && feet[2] == 1.0f64);
    CHECK(yaw == 1.25f32 && pitch == -0.5f32);
    REQUIRE(save_state_apply(&second->save->state, &second->parts));
    CHECK_EQ_U64(second->parts.time.world_ticks, 1234u);
    CHECK_EQ_U64(second->parts.time.day_units, 0x12345678u);
    CHECK_EQ_U64(inventory_count(&second->item->inventory, ITEM_ID_DIRT), 40u);
    CHECK_EQ_U64(second->held->count, 3u);
    CHECK(second->player->survival.health == 13.5f32);
    CHECK_EQ_U64(player_vitals_read(second->player).food, 11u);
    REQUIRE(container_at(&second->item->chests, 7, 8, 1) != nullptr);
    CHECK_EQ_U64(container_at(&second->item->chests, 7, 8, 1)->slots[26].count, 12u);
    REQUIRE(smelting_at(&second->item->furnaces, 9, 9, 1) != nullptr);
    /* Nothing has ticked, and nothing is different. */
    save_state_capture(&second->save->state, &second->parts);
    CHECK_EQ_U64(second->save->state.kept, 0u);
    REQUIRE(save_state_write(&second->save->state, 0u) == SAVE_WORLD_OK);
    uint64_t again_bytes = 0u;
    const uint8_t *const again = ss_slurp(t, ss_state_path(t, root, 0u), &again_bytes);
    REQUIRE(again_bytes == bytes);
    CHECK_MEM_EQ(again, file, bytes);
    /* And what comes next is what came next the first time. */
    for (int64_t i = 1; i <= 40; i++) {
        ss_tick(second, i);
    }
    save_state_capture(&second->save->state, &second->parts);
    REQUIRE(save_state_write(&second->save->state, 1000u) == SAVE_WORLD_OK);
    uint64_t next_bytes = 0u;
    const uint8_t *const next = ss_slurp(t, ss_state_path(t, root, 1000u), &next_bytes);
    REQUIRE(next_bytes == later_bytes);
    CHECK_MEM_EQ(next, later, later_bytes);
    REQUIRE(file_remove(ss_state_path(t, root, 1000u)) == FILE_OK);
    ss_rig_end(second);
    ss_temp_remove(t, root);
}

/* The hashes of a file made right again, as someone forging a number would. */
static void ss_rehash(uint8_t *file, uint64_t bytes) {
    save_state_header_t header;
    memcpy(&header, file, sizeof(header));
    header.sections_hash = ss_fnv(file + sizeof(header), bytes - sizeof(header));
    header.checksum = ss_fnv(&header, offsetof(save_state_header_t, checksum));
    memcpy(file, &header, sizeof(header));
}

/* One number at one place in one section. */
typedef struct ss_forgery_t {
    uint64_t offset;  /* 0: from the start of the section */
    uint64_t value;   /* 8 */
    uint32_t section; /* 16 */
    uint32_t bytes;   /* 20 */
} ss_forgery_t;
static_assert(sizeof(ss_forgery_t) == 24);

TEST(what_cannot_be_believed_is_refused_and_left_as_it_was) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    ss_rig_t *const rig = test_alloc(t, sizeof(ss_rig_t));
    REQUIRE(ss_rig_begin(t, rig, root, false));
    ss_fill(rig);
    save_state_capture(&rig->save->state, &rig->parts);
    REQUIRE(save_state_write(&rig->save->state, 0u) == SAVE_WORLD_OK);
    ss_rig_end(rig);
    const char *const path = ss_state_path(t, root, 0u);
    uint64_t bytes = 0u;
    const uint8_t *const good = ss_slurp(t, path, &bytes);
    REQUIRE(bytes > 0u);
    CHECK_EQ_U64(save_state_check(good, bytes, SS_SEED, 0u), SAVE_WORLD_OK);
    uint8_t *const copy = test_alloc(t, bytes);
    const int32_t quiet = ss_quiet_begin();

    /* A flipped bit anywhere, at a good sample of everywhere. */
    uint32_t flips = 0u;
    for (uint64_t at = 0u; at < bytes; at += bytes / 500u + 1u) {
        memcpy(copy, good, bytes);
        copy[at] ^= (uint8_t)(1u << (at % 8u));
        CHECK(save_state_check(copy, bytes, SS_SEED, 0u) != SAVE_WORLD_OK);
        flips++;
    }
    /* Cut short, and longer. */
    CHECK(save_state_check(good, bytes - 8u, SS_SEED, 0u) != SAVE_WORLD_OK);
    CHECK(save_state_check(good, sizeof(save_state_header_t) - 1u, SS_SEED, 0u) != SAVE_WORLD_OK);

    /* Numbers that are wrong, with hashes that say they are right. */
    uint32_t handle_offset_base = (uint32_t)sizeof(save_state_bodies_t);
    const ss_forgery_t forgeries[] = {
        {offsetof(save_state_time_t, yaw), 0x7FC00000u, SAVE_STATE_SECTION_TIME, 4u},
        {offsetof(save_state_player_t, movement) + offsetof(player_movement_t, position), 0x7FF8000000000000ull,
         SAVE_STATE_SECTION_PLAYER, 8u},
        {offsetof(save_state_player_t, movement) + offsetof(player_movement_t, position), 0x4415AF1D78B58C40ull,
         SAVE_STATE_SECTION_PLAYER, 8u},
        {offsetof(save_state_player_t, movement) + offsetof(player_movement_t, mode), 99u, SAVE_STATE_SECTION_PLAYER,
         1u},
        {offsetof(save_state_player_t, survival) + offsetof(player_survival_t, state), 77u, SAVE_STATE_SECTION_PLAYER,
         1u},
        {offsetof(save_state_player_t, interaction) + offsetof(player_interaction_t, drop_head),
         PLAYER_INTERACTION_DROPS, SAVE_STATE_SECTION_PLAYER, 4u},
        {offsetof(save_state_player_t, interaction) + offsetof(player_interaction_t, hand_block), 60000u,
         SAVE_STATE_SECTION_PLAYER, 2u},
        {offsetof(save_state_items_t, slots), 60000u, SAVE_STATE_SECTION_ITEMS, 2u},
        {offsetof(save_state_items_t, selected), INVENTORY_HOTBAR_SLOTS, SAVE_STATE_SECTION_ITEMS, 1u},
        {offsetof(save_state_items_t, held) + offsetof(item_stack_t, count), 200u, SAVE_STATE_SECTION_ITEMS, 1u},
        {offsetof(save_state_items_t, crafting_hands) + offsetof(crafting_grid_t, side), 3u, SAVE_STATE_SECTION_ITEMS,
         1u},
        {offsetof(container_chest_t, slots) + offsetof(item_stack_t, item), 60000u, SAVE_STATE_SECTION_CHESTS, 2u},
        {offsetof(smelting_furnace_t, cooking), 60000u, SAVE_STATE_SECTION_FURNACES, 2u},
        {offsetof(save_state_drops_t, count), ITEM_DROP_CAPACITY + 1u, SAVE_STATE_SECTION_DROPS, 4u},
        {sizeof(save_state_drops_t) + offsetof(item_drop_t, item), 60000u, SAVE_STATE_SECTION_DROPS, 2u},
        {sizeof(save_state_drops_t) + offsetof(item_drop_t, count), 0u, SAVE_STATE_SECTION_DROPS, 2u},
        {offsetof(save_state_arrows_t, count), PROJECTILE_CAPACITY + 1u, SAVE_STATE_SECTION_ARROWS, 4u},
        {sizeof(save_state_arrows_t), 0x7FF0000000000000ull, SAVE_STATE_SECTION_ARROWS, 8u},
        {offsetof(save_state_bodies_t, count), ENTITY_STORE_CAPACITY + 1u, SAVE_STATE_SECTION_BODIES, 4u},
        {offsetof(save_state_bodies_t, free_head), ENTITY_STORE_CAPACITY, SAVE_STATE_SECTION_BODIES, 4u},
        /* A slot's body somewhere else; a free slot already taken, by the first in the free ring being body 0's. */
        {handle_offset_base, 500u, SAVE_STATE_SECTION_BODIES, 4u},
        {offsetof(save_state_minds_t, zombie_random) + sizeof(save_state_minds_t) +
             ENTITY_STORE_CAPACITY * (sizeof(mob_zombie_body_t) + sizeof(mob_skeleton_body_t)) +
             offsetof(mob_creeper_body_t, state),
         9u, SAVE_STATE_SECTION_MINDS, 1u},
    };
    const uint32_t forgery_count = (uint32_t)(sizeof(forgeries) / sizeof(forgeries[0]));
    for (uint32_t i = 0u; i < forgery_count; i++) {
        memcpy(copy, good, bytes);
        uint64_t section_bytes = 0u;
        const uint8_t *const section = save_state_section_find(copy, bytes, forgeries[i].section, &section_bytes);
        REQUIRE(section != nullptr);
        REQUIRE(forgeries[i].offset + forgeries[i].bytes <= section_bytes);
        memcpy(copy + (section - copy) + forgeries[i].offset, &forgeries[i].value, forgeries[i].bytes);
        ss_rehash(copy, bytes);
        if (!CHECK_EQ_U64(save_state_check(copy, bytes, SS_SEED, 0u), SAVE_WORLD_ERROR_DAMAGED)) {
            printf("  forgery %" PRIu32 "\n", i);
        }
    }
    /* A body's box that physics would stop the program for. */
    memcpy(copy, good, bytes);
    uint64_t section_bytes = 0u;
    const uint8_t *const bodies = save_state_section_find(copy, bytes, SAVE_STATE_SECTION_BODIES, &section_bytes);
    REQUIRE(bodies != nullptr);
    save_state_bodies_t bodies_head;
    memcpy(&bodies_head, bodies, sizeof(bodies_head));
    const uint64_t half_width_at = sizeof(save_state_bodies_t) + 3ull * ENTITY_STORE_CAPACITY * sizeof(uint32_t) +
                                   13ull * bodies_head.count * sizeof(float64_t);
    const float32_t no_width = 0.0f32;
    memcpy(copy + (bodies - copy) + half_width_at, &no_width, sizeof(no_width));
    ss_rehash(copy, bytes);
    CHECK_EQ_U64(save_state_check(copy, bytes, SS_SEED, 0u), SAVE_WORLD_ERROR_DAMAGED);
    /* A section from nowhere, and one twice. */
    memcpy(copy, good, bytes);
    uint32_t nowhere = 77u;
    memcpy(copy + sizeof(save_state_header_t) + offsetof(save_state_section_t, id), &nowhere, sizeof(nowhere));
    ss_rehash(copy, bytes);
    CHECK_EQ_U64(save_state_check(copy, bytes, SS_SEED, 0u), SAVE_WORLD_ERROR_DAMAGED);
    /* A version, a seed and tables from elsewhere: each its own answer, with a good checksum. */
    save_state_header_t header;
    memcpy(copy, good, bytes);
    memcpy(&header, copy, sizeof(header));
    header.version = SAVE_STATE_VERSION + 1u;
    memcpy(copy, &header, sizeof(header));
    ss_rehash(copy, bytes);
    CHECK_EQ_U64(save_state_check(copy, bytes, SS_SEED, 0u), SAVE_WORLD_ERROR_VERSION);
    CHECK_EQ_U64(save_state_check(good, bytes, SS_SEED + 1u, 0u), SAVE_WORLD_ERROR_SEED);
    memcpy(copy, good, bytes);
    memcpy(&header, copy, sizeof(header));
    header.table_hash ^= 1u;
    memcpy(copy, &header, sizeof(header));
    ss_rehash(copy, bytes);
    CHECK_EQ_U64(save_state_check(copy, bytes, SS_SEED, 0u), SAVE_WORLD_ERROR_TABLES);

    /* On a disk: the world is not opened, and nothing there changes, a leftover from another save included. */
    const char *const leftover = ss_state_path(t, root, 5u);
    REQUIRE(file_write_atomic(leftover, "x", 1u) == FILE_OK);
    memcpy(copy, good, bytes);
    copy[bytes / 2u] ^= 1u;
    REQUIRE(file_write_atomic(path, copy, bytes) == FILE_OK);
    save_t *const save = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));
    CHECK_EQ_U64(save_open(save, root, SS_SEED), SAVE_WORLD_ERROR_DAMAGED);
    CHECK_EQ_U64(save->world.open, 0u);
    uint64_t after_bytes = 0u;
    const uint8_t *const after = ss_slurp(t, path, &after_bytes);
    REQUIRE(after_bytes == bytes);
    CHECK_MEM_EQ(after, copy, bytes);
    CHECK_EQ_U64(ss_state_files(root), 2u);
    /* And the right bytes put back, it opens, and only then does the leftover go. */
    REQUIRE(file_write_atomic(path, good, bytes) == FILE_OK);
    REQUIRE(file_write_atomic(ss_join(t, root, "state.5.tmp"), "x", 1u) == FILE_OK);
    REQUIRE(file_write_atomic(ss_join(t, root, "statement"), "x", 1u) == FILE_OK);
    CHECK_EQ_U64(save_open(save, root, SS_SEED), SAVE_WORLD_OK);
    CHECK_EQ_U64(ss_state_files(root), 1u);
    uint64_t kept_bytes = 0u;
    CHECK_EQ_U64(file_path_size(ss_join(t, root, "statement"), &kept_bytes), FILE_OK); /* not a name of ours */
    CHECK(file_path_size(ss_join(t, root, "state.5.tmp"), &kept_bytes) == FILE_ERROR_NOT_FOUND);
    save_deinit(save);
    ss_quiet_end(quiet);
    printf("  %" PRIu32 " flipped bits and %" PRIu32 " forged numbers refused\n", flips, forgery_count);
    ss_temp_remove(t, root);
}

/* ---- Autosaves --------------------------------------------------------------------------------------------------------- */

/* A block of this number, which a model of it can look for. Each a place of its own, in the first column. */
static void ss_stamp_cell(uint64_t stamp, int32_t *x, int32_t *y, int32_t *z) {
    *x = (int32_t)(stamp & 15u);
    *y = (int32_t)((stamp >> 4u) & 15u);
    *z = 400 + (int32_t)(stamp >> 8u);
}

static void ss_stamp(test_context_t *t, ss_rig_t *rig, uint64_t stamp) {
    int32_t x, y, z;
    ss_stamp_cell(stamp, &x, &y, &z);
    if (!CHECK_EQ_U64(world_edit_block_set(&rig->w.world->edit, x, y, z, BLOCK_ID_STONE), WORLD_EDIT_STATUS_CHANGED)) {
        printf("  stamp %" PRIu64 "\n", stamp);
    }
}

/* The stamps in what is saved of the first column are exactly those up to this tick: blocks and state are one save. */
static void ss_stamps_check(test_context_t *t, save_t *save, uint64_t tick, uint64_t tried_to) {
    uint16_t *const states = test_alloc(t, SS_COLUMN_VOXELS * sizeof(uint16_t));
    uint8_t *const scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);
    (void)save_world_column_overlay(&save->world, 0, 0, states, scratch);
    uint64_t wrong = 0u;
    for (uint64_t stamp = 1u; stamp <= tried_to; stamp++) {
        int32_t x, y, z;
        ss_stamp_cell(stamp, &x, &y, &z);
        const uint32_t storage = (uint32_t)(z - COLUMN_Z_STORAGE_MIN);
        const uint32_t section = storage / SECTION_SIZE;
        const uint32_t voxel = section_voxel_index((uint32_t)x, (uint32_t)y, storage % SECTION_SIZE);
        const uint16_t found = states[(uint64_t)section * SECTION_VOXELS + voxel];
        wrong += (found == BLOCK_ID_STONE) != (stamp <= tick) ? 1u : 0u;
    }
    if (!CHECK_EQ_U64(wrong, 0u)) {
        printf("  saved as of tick %" PRIu64 ", looked up to %" PRIu64 "\n", tick, tried_to);
    }
}

static uint64_t ss_saved_tick(const save_state_t *state) {
    uint64_t bytes = 0u;
    const uint8_t *const data =
        save_state_section_find(state->borrowed_image, state->image_bytes, SAVE_STATE_SECTION_TIME, &bytes);
    if (data == nullptr) {
        return 0u;
    }
    save_state_time_t time;
    memcpy(&time, data, sizeof(time));
    return time.world_ticks;
}

/* A world on disk, for the tests below: its save and its stamp as of a first tick. */
static void ss_step_until_written(save_autosave_t *autosave, world_edit_t *edit, const save_state_parts_t *parts,
                                  uint64_t tick) {
    while (atomic_load_explicit(&autosave->writing, memory_order_acquire) != 0u) {
        time_sleep_ns(100000);
    }
    /* Looks at what it said, and starts nothing while none is due. */
    const uint64_t next = autosave->next_tick;
    autosave->next_tick = UINT64_MAX;
    save_autosave_step(autosave, edit, parts, tick);
    autosave->next_tick = next;
}

/* Holds a write at one step until told. */
typedef struct ss_hold_t {
    _Atomic uint32_t reached; /* 0 */
    _Atomic uint32_t release; /* 4 */
} ss_hold_t;
static_assert(sizeof(ss_hold_t) == 8);

static void ss_hold_hook(void *context, save_world_step_t step) {
    ss_hold_t *const hold = context;
    if (step == SAVE_WORLD_STEP_DELTAS_DONE) {
        atomic_fetch_add(&hold->reached, 1u);
        while (atomic_load(&hold->release) == 0u) {
            time_sleep_ns(100000);
        }
    }
}

TEST(a_stuck_write_holds_up_nothing_and_there_is_only_ever_one) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    ss_rig_t *const rig = test_alloc(t, sizeof(ss_rig_t));
    REQUIRE(ss_rig_begin(t, rig, root, true));
    save_t *const save = rig->save;
    save_autosave_t *const autosave = &save->autosave;
    world_edit_t *const edit = &rig->w.world->edit;
    ss_fill(rig);
    save_autosave_start(autosave, rig->job, &save->world, &save->state, 0u);
    CHECK_EQ_U64(autosave->next_tick, SAVE_AUTOSAVE_INTERVAL_TICKS);
    CHECK_EQ_U64(SAVE_AUTOSAVE_INTERVAL_TICKS, 600u);
    save_autosave_interval_set(autosave, 2u);
    autosave->next_tick = 2u;
    ss_hold_t hold = {0};
    save_world_step_hook_set(&save->world, ss_hold_hook, &hold);

    /* Not due, nothing. */
    rig->parts.time.world_ticks = 1u;
    ss_stamp(t, rig, 1u);
    save_autosave_step(autosave, edit, &rig->parts, 1u);
    CHECK_EQ_U64(atomic_load(&autosave->writing), 0u);
    /* Due, and the write gets as far as the hold. */
    rig->parts.time.world_ticks = 2u;
    ss_stamp(t, rig, 2u);
    save_autosave_step(autosave, edit, &rig->parts, 2u);
    CHECK_EQ_U64(atomic_load(&autosave->writing), 1u);
    const int64_t deadline = time_now_ns() + 20ll * TIME_NS_PER_SECOND;
    while (atomic_load(&hold.reached) == 0u && time_now_ns() < deadline) {
        time_sleep_ns(100000);
    }
    REQUIRE(atomic_load(&hold.reached) == 1u);
    /* Steps go on, each doing its work, with no sign of waiting, and with no second write. */
    int64_t longest_ns = 0;
    for (uint64_t tick = 3u; tick < 200u; tick++) {
        rig->parts.time.world_ticks = tick;
        ss_stamp(t, rig, tick);
        const int64_t started_ns = time_now_ns();
        save_autosave_step(autosave, edit, &rig->parts, tick);
        const int64_t took_ns = time_now_ns() - started_ns;
        longest_ns = took_ns > longest_ns ? took_ns : longest_ns;
    }
    printf("  the longest step past a stuck write: %.3f us\n", (libc_double_t)(time_ns_to_ms(longest_ns) * 1000.0f64));
    CHECK(time_ns_to_ms(longest_ns) < 20.0f64);
    CHECK_EQ_U64(atomic_load(&hold.reached), 1u);
    CHECK_EQ_U64(save->world.generation, 0u);
    /* What the sim sees during it, is what is on its way: a column coming in meanwhile gets its changes. */
    uint16_t *const states = test_alloc(t, SS_COLUMN_VOXELS * sizeof(uint16_t));
    uint8_t *const scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);
    CHECK(save_world_column_overlay(&save->world, 0, 0, states, scratch) != 0u);
    atomic_store(&hold.release, 1u);
    ss_step_until_written(autosave, edit, &rig->parts, 199u);
    CHECK_EQ_U64(save->world.generation, 1u);
    CHECK_EQ_U64(autosave->saves_done, 1u);
    CHECK_EQ_U64(ss_state_files(root), 1u);
    /* Stopping and closing, with one more going, waits for it. */
    autosave->next_tick = 0u;
    hold.reached = 0u;
    save_autosave_step(autosave, edit, &rig->parts, 200u);
    ss_rig_end(rig);

    /* What came out is as of the tick the save started, whichever that was. */
    save_t *const reader = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(reader));
    REQUIRE(save_open(reader, root, SS_SEED) == SAVE_WORLD_OK);
    CHECK_EQ_U64(reader->world.generation, 2u);
    CHECK_EQ_U64(ss_saved_tick(&reader->state), 199u);
    ss_stamps_check(t, reader, 199u, 210u);
    save_deinit(reader);
    ss_temp_remove(t, root);
}

TEST(a_disk_that_refuses_costs_nothing_and_is_tried_again_at_the_interval) {
    REQUIRE(geteuid() != 0u); /* permissions are nothing to root */
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    ss_rig_t *const rig = test_alloc(t, sizeof(ss_rig_t));
    REQUIRE(ss_rig_begin(t, rig, root, true));
    save_t *const save = rig->save;
    save_autosave_t *const autosave = &save->autosave;
    world_edit_t *const edit = &rig->w.world->edit;
    ss_fill(rig);
    save_autosave_start(autosave, rig->job, &save->world, &save->state, 0u);
    save_autosave_interval_set(autosave, 10u);
    autosave->next_tick = 10u;
    /* A first one. */
    rig->parts.time.world_ticks = 10u;
    ss_stamp(t, rig, 1u);
    save_autosave_step(autosave, edit, &rig->parts, 10u);
    ss_step_until_written(autosave, edit, &rig->parts, 10u);
    REQUIRE(save->world.generation == 1u);
    autosave->next_tick = 20u;

    const int32_t quiet = ss_quiet_begin();
    /* Where the state goes says no, and so the blocks do not go either. */
    REQUIRE(chmod(root, 0555) == 0);
    rig->parts.time.world_ticks = 20u;
    ss_stamp(t, rig, 2u);
    save_autosave_step(autosave, edit, &rig->parts, 20u);
    ss_step_until_written(autosave, edit, &rig->parts, 20u);
    CHECK_EQ_U64(autosave->saves_failed, 1u);
    CHECK_EQ_U64(autosave->failed_said, 1u);
    CHECK_EQ_U64(autosave->retry, 1u);
    CHECK_EQ_U64(save->world.generation, 1u);
    /* Nothing more until the interval, though something asks. */
    save_autosave_request(autosave);
    for (uint64_t i = 0u; i < 5u; i++) {
        save_autosave_step(autosave, edit, &rig->parts, 25u);
        CHECK_EQ_U64(atomic_load(&autosave->writing), 0u);
    }
    /* What a game does meanwhile goes into the next set, and the failed one is still readable. */
    rig->parts.time.world_ticks = 28u;
    ss_stamp(t, rig, 3u);
    /* Tried again, with the same set and the same image, and it fails the same way. */
    save_autosave_step(autosave, edit, &rig->parts, 30u);
    ss_step_until_written(autosave, edit, &rig->parts, 30u);
    CHECK_EQ_U64(autosave->saves_failed, 2u);
    CHECK_EQ_U64(ss_state_files(root), 1u);
    REQUIRE(chmod(root, 0755) == 0);

    /* The blocks' directory says no: the state went first, under a name nothing refers to. */
    REQUIRE(chmod(ss_join(t, root, "blocks"), 0555) == 0);
    save_autosave_step(autosave, edit, &rig->parts, 40u);
    ss_step_until_written(autosave, edit, &rig->parts, 40u);
    CHECK_EQ_U64(autosave->saves_failed, 3u);
    CHECK_EQ_U64(save->world.generation, 1u);
    REQUIRE(chmod(ss_join(t, root, "blocks"), 0755) == 0);
    ss_quiet_end(quiet);

    /* And the disk comes back: that same moment is saved, tick 20 with stamps to 2, and then the next. */
    save_autosave_step(autosave, edit, &rig->parts, 50u);
    ss_step_until_written(autosave, edit, &rig->parts, 50u);
    CHECK_EQ_U64(autosave->saves_failed, 3u);
    CHECK_EQ_U64(autosave->failed_said, 0u);
    CHECK_EQ_U64(autosave->retry, 0u);
    CHECK_EQ_U64(save->world.generation, 2u);
    uint64_t bytes = 0u;
    const uint8_t *const file = ss_slurp(t, ss_state_path(t, root, 2u), &bytes);
    REQUIRE(bytes > 0u);
    save_state_time_t time;
    uint64_t time_bytes = 0u;
    const uint8_t *const time_data = save_state_section_find(file, bytes, SAVE_STATE_SECTION_TIME, &time_bytes);
    REQUIRE(time_data != nullptr);
    memcpy(&time, time_data, sizeof(time));
    CHECK_EQ_U64(time.world_ticks, 20u);
    CHECK_EQ_U64(ss_state_files(root), 1u);
    REQUIRE(save_autosave_finish(autosave, edit, &rig->parts) == SAVE_WORLD_OK);
    CHECK_EQ_U64(save->world.generation, 3u);
    ss_rig_end(rig);

    save_t *const reader = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(reader));
    REQUIRE(save_open(reader, root, SS_SEED) == SAVE_WORLD_OK);
    CHECK_EQ_U64(ss_saved_tick(&reader->state), 28u);
    ss_stamps_check(t, reader, 3u, 3u);
    CHECK_EQ_U64(ss_state_files(root), 1u);
    save_deinit(reader);
    ss_temp_remove(t, root);
}

/* ---- Killed ------------------------------------------------------------------------------------------------------------- */

typedef struct ss_kill_plan_t {
    uint32_t step;      /* 0 */
    uint32_t remaining; /* 4: times to pass it first */
} ss_kill_plan_t;
static_assert(sizeof(ss_kill_plan_t) == 8);

static void ss_kill_hook(void *context, save_world_step_t step) {
    ss_kill_plan_t *const plan = context;
    if (step == plan->step) {
        if (plan->remaining == 0u) {
            (void)raise(SIGKILL);
        }
        plan->remaining--;
    }
}

/* The tick and the stamp of a save, which go together: each new tick is one new stamp. */
[[noreturn]] static void ss_kill_child(test_context_t *t, const char *root, const ss_kill_plan_t *plan_in) {
    ss_rig_t *const rig = test_alloc(t, sizeof(ss_rig_t));
    if (!ss_rig_begin(t, rig, root, true)) {
        _exit(10);
    }
    (void)save_state_apply(&rig->save->state, &rig->parts);
    const uint64_t tick = rig->parts.time.world_ticks + 1u;
    ss_kill_plan_t plan = *plan_in;
    save_autosave_t *const autosave = &rig->save->autosave;
    save_autosave_start(autosave, rig->job, &rig->save->world, &rig->save->state, 0u);
    autosave->next_tick = 0u;
    save_world_step_hook_set(&rig->save->world, ss_kill_hook, &plan);
    rig->parts.time.world_ticks = tick;
    ss_stamp(t, rig, tick);
    save_autosave_step(autosave, &rig->w.world->edit, &rig->parts, tick);
    time_sleep_ns(30ll * TIME_NS_PER_SECOND);
    _exit(11); /* it was to have died */
}

TEST(killed_at_every_step_of_a_save_blocks_and_state_are_one_save_or_the_other) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    /* A first save, in a child, as this process must not have threads when it forks. */
    {
        (void)fflush(stdout);
        const pid_t child = fork();
        REQUIRE(child >= 0);
        if (child == 0) {
            ss_rig_t *const rig = test_alloc(t, sizeof(ss_rig_t));
            if (!ss_rig_begin(t, rig, root, true)) {
                _exit(10);
            }
            rig->parts.time.world_ticks = 1u;
            ss_stamp(t, rig, 1u);
            save_autosave_start(&rig->save->autosave, rig->job, &rig->save->world, &rig->save->state, 0u);
            const save_world_status_t status =
                save_autosave_finish(&rig->save->autosave, &rig->w.world->edit, &rig->parts);
            ss_rig_end(rig);
            _exit(status == SAVE_WORLD_OK ? 0 : 12);
        }
        int32_t status = 0;
        REQUIRE(waitpid(child, &status, 0) == child);
        REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    static const ss_kill_plan_t plans[] = {
        {SAVE_WORLD_STEP_DELTA_RECORDS, 0u}, {SAVE_WORLD_STEP_DELTA_SYNCED, 0u},  {SAVE_WORLD_STEP_DELTA_RENAMED, 0u},
        {SAVE_WORLD_STEP_DELTAS_DONE, 0u},   {SAVE_WORLD_STEP_MANIFEST_DONE, 0u}, {SAVE_WORLD_STEP_REPLACED_GONE, 0u},
    };
    uint64_t saved = 1u;
    save_t *const reader = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(reader));
    for (uint32_t i = 0u; i < sizeof(plans) / sizeof(plans[0]); i++) {
        (void)fflush(stdout);
        const pid_t child = fork();
        REQUIRE(child >= 0);
        if (child == 0) {
            ss_kill_child(t, root, &plans[i]);
        }
        int32_t status = 0;
        REQUIRE(waitpid(child, &status, 0) == child);
        if (!CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)) {
            printf("  plan %" PRIu32 " ended with %" PRId32 "\n", i, status);
            continue;
        }
        /* Before the manifest it did not happen, though its state is on the disk. */
        const bool happened = plans[i].step >= SAVE_WORLD_STEP_MANIFEST_DONE;
        const uint64_t expected = saved + (happened ? 1u : 0u);
        REQUIRE(save_open(reader, root, SS_SEED) == SAVE_WORLD_OK);
        if (!CHECK_EQ_U64(ss_saved_tick(&reader->state), expected) ||
            !CHECK_EQ_U64(reader->world.generation, expected)) {
            printf("  plan %" PRIu32 "\n", i);
        }
        ss_stamps_check(t, reader, expected, expected + 1u);
        CHECK_EQ_U64(ss_state_files(root), 1u);
        save_close(reader);
        saved = expected;
    }
    CHECK_EQ_U64(saved, 3u);
    save_deinit(reader);
    ss_temp_remove(t, root);
}

/* Down the pipe, from a child that saves until it dies. */
typedef struct ss_kill_note_t {
    uint64_t tick; /* 0 */
    uint64_t kind; /* 8: 0 a tick, 1 a save started, as of this tick, 2 one finished, which started at this tick */
} ss_kill_note_t;
static_assert(sizeof(ss_kill_note_t) == 16);

static void ss_kill_note(int32_t pipe_out, uint64_t tick, uint64_t kind) {
    const ss_kill_note_t note = {.tick = tick, .kind = kind};
    if (write(pipe_out, &note, sizeof(note)) != (int64_t)sizeof(note)) {
        _exit(13);
    }
}

[[noreturn]] static void ss_kill_saver(test_context_t *t, const char *root, int32_t pipe_out) {
    ss_rig_t *const rig = test_alloc(t, sizeof(ss_rig_t));
    if (!ss_rig_begin(t, rig, root, true)) {
        _exit(10);
    }
    (void)save_state_apply(&rig->save->state, &rig->parts);
    uint64_t tick = rig->parts.time.world_ticks;
    save_autosave_t *const autosave = &rig->save->autosave;
    save_autosave_start(autosave, rig->job, &rig->save->world, &rig->save->state, tick);
    save_autosave_interval_set(autosave, SS_KILL_INTERVAL);
    autosave->next_tick = tick + SS_KILL_INTERVAL;
    uint64_t started = 0u, finished_seen = 0u;
    for (;;) {
        tick++;
        rig->parts.time.world_ticks = tick;
        ss_stamp(t, rig, tick);
        ss_kill_note(pipe_out, tick, 0u);
        const uint64_t next = autosave->next_tick;
        save_autosave_step(autosave, &rig->w.world->edit, &rig->parts, tick);
        if (autosave->next_tick != next && autosave->outstanding != 0u) {
            started = tick;
            ss_kill_note(pipe_out, tick, 1u);
        }
        if (atomic_load_explicit(&autosave->writing, memory_order_acquire) == 0u &&
            autosave->saves_done != finished_seen) {
            finished_seen = autosave->saves_done;
            ss_kill_note(pipe_out, started, 2u);
        }
        time_sleep_ns(2000000);
    }
}

/* What the notes of one child come to. */
typedef struct ss_kill_notes_t {
    uint64_t last_tick;     /* 0 */
    uint64_t last_started;  /* 8 */
    uint64_t last_finished; /* 16 */
    uint64_t saves;         /* 24 */
} ss_kill_notes_t;
static_assert(sizeof(ss_kill_notes_t) == 32);

static void ss_kill_note_take(test_context_t *t, ss_kill_notes_t *notes, const ss_kill_note_t *note) {
    if (note->kind == 0u) {
        notes->last_tick = note->tick;
    } else if (note->kind == 1u) {
        /* Each one is an interval after the last, or as soon as a slow one had finished. */
        if (notes->last_started != 0u && note->tick - notes->last_started != SS_KILL_INTERVAL) {
            CHECK(note->tick - notes->last_started > SS_KILL_INTERVAL);
        }
        notes->last_started = note->tick;
        notes->saves++;
    } else {
        notes->last_finished = note->tick;
    }
}

TEST(killed_at_any_moment_it_loses_at_most_an_interval_and_the_write_under_way) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    uint64_t rng = 0x1234567u;
    uint64_t saves = 0u, longest_lost = 0u, confirmed = 0u, rounds_midwrite = 0u;
    save_t *const reader = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(reader));
    for (uint32_t round = 0u; round < SS_KILL_ROUNDS; round++) {
        int32_t pipe_ends[2];
        REQUIRE(pipe(pipe_ends) == 0);
        (void)fflush(stdout);
        const pid_t child = fork();
        REQUIRE(child >= 0);
        if (child == 0) {
            (void)close(pipe_ends[0]);
            ss_kill_saver(t, root, pipe_ends[1]);
        }
        (void)close(pipe_ends[1]);
        ss_kill_notes_t notes = {.last_finished = confirmed};
        ss_kill_note_t note;
        /* Past its start and among its saves, however long that took. A quiet pipe: 16 bytes a tick of 2 ms, for well under what it holds. */
        while (notes.last_started == 0u && read(pipe_ends[0], &note, sizeof(note)) == (int64_t)sizeof(note)) {
            ss_kill_note_take(t, &notes, &note);
        }
        rng = rng * 6364136223846793005ull + 1442695040888963407ull;
        const struct timespec wait = {.tv_sec = 0, .tv_nsec = (int64_t)(50000000u + (rng >> 33u) % 250000000u)};
        (void)nanosleep(&wait, nullptr);
        REQUIRE(kill(child, SIGKILL) == 0);
        int32_t status = 0;
        REQUIRE(waitpid(child, &status, 0) == child);
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
        while (read(pipe_ends[0], &note, sizeof(note)) == (int64_t)sizeof(note)) {
            ss_kill_note_take(t, &notes, &note);
        }
        (void)close(pipe_ends[0]);
        REQUIRE(notes.last_started != 0u);
        saves += notes.saves;

        REQUIRE(save_open(reader, root, SS_SEED) == SAVE_WORLD_OK);
        const uint64_t tick = ss_saved_tick(&reader->state);
        /* What it said had finished is there, and what is there had started. So it lost at most what was under way and the ticks since. */
        CHECK(tick >= notes.last_finished);
        CHECK(tick <= notes.last_started);
        const uint64_t lost = notes.last_tick - tick;
        longest_lost = lost > longest_lost ? lost : longest_lost;
        rounds_midwrite += tick != notes.last_started ? 1u : 0u;
        ss_stamps_check(t, reader, tick, notes.last_tick);
        CHECK_EQ_U64(ss_state_files(root), 1u);
        save_close(reader);
        confirmed = tick;
    }
    printf("  %" PRIu32 " kills across %" PRIu64 " saves, %" PRIu64
           " of them with a later save under way; the most ticks lost %" PRIu64 ", at a save every %" PRIu32 "\n",
           SS_KILL_ROUNDS, saves, rounds_midwrite, longest_lost, SS_KILL_INTERVAL);
    save_deinit(reader);
    ss_temp_remove(t, root);
}

/* ---- What it costs --------------------------------------------------------------------------------------------------- */

/* The sim's part of one save, over a few tries, with this many sections changed. */
static int64_t ss_sim_cost(test_context_t *t, ss_rig_t *rig, uint32_t sections, uint64_t *tick) {
    save_autosave_t *const autosave = &rig->save->autosave;
    world_edit_t *const edit = &rig->w.world->edit;
    int64_t most_ns = 0;
    for (uint32_t round = 0u; round < 5u; round++) {
        for (uint32_t section = 0u; section < sections; section++) {
            /* One voxel in each of many sections, of the sixteen columns nearest the middle. */
            const int32_t x = (int32_t)(section % 4u) * 16 - 32;
            const int32_t y = (int32_t)(section / 4u % 4u) * 16 - 32;
            const int32_t z = 16 * (int32_t)(section / 16u) - 200;
            uint16_t block = BLOCK_ID_AIR;
            CHECK_EQ_U64(world_map_block_get(rig->w.map, x, y, z, &block), WORLD_MAP_STATUS_OK);
            CHECK(world_edit_block_set(edit, x, y, z, block == BLOCK_ID_STONE ? BLOCK_ID_DIRT : BLOCK_ID_STONE) ==
                  WORLD_EDIT_STATUS_CHANGED);
        }
        ss_step_until_written(autosave, edit, &rig->parts, *tick);
        *tick += 1u;
        autosave->next_tick = 0u;
        const int64_t started_ns = time_now_ns();
        save_autosave_step(autosave, edit, &rig->parts, *tick);
        const int64_t took_ns = time_now_ns() - started_ns;
        most_ns = took_ns > most_ns ? took_ns : most_ns;
        ss_step_until_written(autosave, edit, &rig->parts, *tick);
    }
    return most_ns;
}

TEST(a_save_costs_the_sim_a_fraction_of_a_tick_and_no_heap) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    ss_rig_t *const rig = test_alloc(t, sizeof(ss_rig_t));
    REQUIRE(ss_rig_begin(t, rig, root, true));
    save_t *const save = rig->save;
    ss_fill(rig);
    save_autosave_start(&save->autosave, rig->job, &save->world, &save->state, 0u);
    uint64_t tick = 1u;
    heap_counters_t before;
    heap_counters_read(&before);
    const int64_t few_ns = ss_sim_cost(t, rig, 8u, &tick);
    const uint64_t few_bytes = save->state.image_bytes;
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.reserve_calls, before.reserve_calls);
    CHECK_EQ_U64(after.release_calls, before.release_calls);

    /* Every store full: the largest image there can be. */
    item_t *const item = rig->item;
    while (item->chests.count < CONTAINER_CHESTS) {
        const uint32_t i = item->chests.count;
        (void)container_open(&item->chests, 100 + (int32_t)i, 0, 1);
    }
    while (item->furnaces.count < SMELTING_FURNACES) {
        const uint32_t i = item->furnaces.count;
        (void)smelting_open(&item->furnaces, 100 + (int32_t)i, 9, 1);
    }
    entity_t *const entity = rig->entity;
    const float64_t at[3] = {8.0f64, 8.0f64, 50.0f64};
    while (entity->item_drops.count < ITEM_DROP_CAPACITY) {
        item_drop_spawn(&entity->item_drops, 1u, 1u, at);
    }
    while (entity->projectiles.count < PROJECTILE_CAPACITY) {
        const float64_t along[3] = {1.0f64, 0.0f64, 0.0f64};
        projectile_fire(&entity->projectiles, ENTITY_HANDLE_NONE, at, along, 1.0f64, 0.0f64);
    }
    while (entity->store.count < ENTITY_STORE_CAPACITY) {
        (void)mob_spawn_body(rig->spawn, &entity->store, MOB_ID_ZOMBIE, at, 0.0f32);
    }
    const int64_t full_ns = ss_sim_cost(t, rig, 8u, &tick);
    const uint64_t full_bytes = save->state.image_bytes;
    CHECK(full_bytes <= SAVE_STATE_IMAGE_BYTES);
    CHECK(full_bytes > SAVE_STATE_IMAGE_BYTES - 16384u);
    const int64_t many_ns = ss_sim_cost(t, rig, 64u, &tick);
    const int64_t most_ns = ss_sim_cost(t, rig, 512u, &tick);
    printf("  the sim's part of a save: %.3f ms with few things (%" PRIu64 " bytes); with every store full (%" PRIu64
           " bytes) %.3f ms, %.3f ms with 64 sections changed, %.3f ms with 512. A tick is 50 ms.\n",
           (libc_double_t)time_ns_to_ms(few_ns), few_bytes, full_bytes, (libc_double_t)time_ns_to_ms(full_ns),
           (libc_double_t)time_ns_to_ms(many_ns), (libc_double_t)time_ns_to_ms(most_ns));
    /* Loose, as this is a debug build on a machine doing other things. */
    CHECK(time_ns_to_ms(few_ns) < 25.0f64);
    CHECK(time_ns_to_ms(full_ns) < 25.0f64);
    /* A full image still reloads. */
    REQUIRE(save_autosave_finish(&save->autosave, &rig->w.world->edit, &rig->parts) == SAVE_WORLD_OK);
    uint64_t bytes = 0u;
    const uint8_t *const file = ss_slurp(t, ss_state_path(t, root, save->world.generation), &bytes);
    CHECK_EQ_U64(save_state_check(file, bytes, SS_SEED, save->world.generation), SAVE_WORLD_OK);
    printf("  %" PRIu64 " saves, the longest on a worker %.3f ms\n", save->autosave.saves_done,
           (libc_double_t)time_ns_to_ms(save->autosave.write_ns_most));
    ss_rig_end(rig);
    ss_temp_remove(t, root);
}

/* ---- Flowing fluid: a voxel of water with no level is a source, so blocks without levels flood ---------------------------- */

enum : int32_t {
    SS_WATER_FROM = -16,
    SS_WATER_TO = 31,
    SS_WATER_Z = 15, /* the top of a section, with the next above it */
};
enum : uint32_t {
    SS_WATER_SIDE = (uint32_t)(SS_WATER_TO - SS_WATER_FROM) + 1u,
    SS_WATER_CELLS = SS_WATER_SIDE * SS_WATER_SIDE * 2u,
};

/* Every block and level of the two layers water is in. */
static void ss_water_look(const test_world_t *w, uint16_t *blocks, uint8_t *levels) {
    uint32_t at = 0u;
    for (int32_t z = SS_WATER_Z; z <= SS_WATER_Z + 1; z++) {
        for (int32_t y = SS_WATER_FROM; y <= SS_WATER_TO; y++) {
            for (int32_t x = SS_WATER_FROM; x <= SS_WATER_TO; x++) {
                blocks[at] = BLOCK_ID_AIR;
                (void)world_map_block_get(w->map, x, y, z, &blocks[at]);
                levels[at] = world_tick_fluid_level(&w->world->tick, x, y, z);
                at++;
            }
        }
    }
}

static void ss_water_begin(test_context_t *t, test_world_t *w) {
    if (test_world_begin(t, w)) {
        test_world_fill(t, w, SS_WATER_FROM, SS_WATER_FROM, SS_WATER_Z - 1, SS_WATER_TO, SS_WATER_TO, SS_WATER_Z - 1,
                        BLOCK_ID_STONE);
    }
}

TEST(a_record_can_end_in_levels_and_levels_no_fluid_has_are_refused) {
    uint16_t *const states = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint16_t *const back = test_alloc(t, SECTION_VOXELS * sizeof(uint16_t));
    uint16_t *const palette_slots = test_alloc(t, 65536u * sizeof(uint16_t));
    uint8_t *const record = test_alloc(t, SAVE_WORLD_ENTRY_BYTES_MAX);
    for (uint32_t i = 0u; i < SECTION_VOXELS; i++) {
        states[i] = BLOCK_ID_WATER;
    }
    const uint32_t bytes = save_world_record_encode(states, palette_slots, record);
    CHECK(save_world_record_levels(record, bytes) == nullptr);
    /* By hand: the top bit of the first number, and half a byte per voxel after what there was. */
    record[1] |= 0x80u;
    memset(record + bytes, 0x81, SAVE_WORLD_RECORD_LEVELS_BYTES);
    const uint32_t with = bytes + SAVE_WORLD_RECORD_LEVELS_BYTES;
    REQUIRE(save_world_record_decode(record, with, back));
    CHECK_MEM_EQ(back, states, SECTION_VOXELS * sizeof(uint16_t));
    CHECK(save_world_record_levels(record, with) == record + bytes);
    CHECK(!save_world_record_decode(record, bytes, back));
    CHECK(!save_world_record_decode(record, with - 1u, back));
    record[with - 1u] = 0x09u;
    CHECK(!save_world_record_decode(record, with, back));
    record[with - 1u] = 0x90u;
    CHECK(!save_world_record_decode(record, with, back));
}

TEST(flowing_water_comes_back_with_its_levels_goes_on_and_does_not_flood) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    test_world_t *const w = test_alloc(t, sizeof(test_world_t));
    ss_water_begin(t, w);
    save_t *const save = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    world_tick_t *tick = &w->world->tick;
    world_edit_t *edit = &w->world->edit;
    save_world_levels_from(&save->world, tick);
    REQUIRE(world_edit_block_set(edit, 8, 8, SS_WATER_Z, BLOCK_ID_WATER) == WORLD_EDIT_STATUS_CHANGED);
    for (uint32_t i = 0u; i < 100u; i++) {
        world_tick_step(tick);
    }
    CHECK_EQ_U64(world_tick_fluid_level(tick, 8, 8, SS_WATER_Z), WORLD_TICK_LEVEL_SOURCE);
    CHECK_EQ_U64(world_tick_fluid_level(tick, 9, 8, SS_WATER_Z), 1u);
    CHECK_EQ_U64(world_tick_fluid_level(tick, 15, 8, SS_WATER_Z), 7u);
    REQUIRE(save_world_flush(&save->world, edit) == SAVE_WORLD_OK);

    /* Water let in above: the layer under it starts falling, with no block in its section changing. So only the level can say that section is dirty. */
    uint32_t slot = 0u;
    REQUIRE(world_map_slot_of(w->map, 0, 0, &slot));
    CHECK_EQ_U64(world_edit_dirty_peek(edit, slot, WORLD_EDIT_CONSUMER_SAVE), 0u);
    REQUIRE(world_edit_block_set(edit, 12, 8, SS_WATER_Z + 1, BLOCK_ID_WATER) == WORLD_EDIT_STATUS_CHANGED);
    for (uint32_t i = 0u; i < 12u; i++) {
        world_tick_step(tick);
    }
    CHECK_EQ_U64(world_tick_fluid_level(tick, 12, 8, SS_WATER_Z), WORLD_TICK_LEVEL_FALLING);
    CHECK((world_edit_dirty_peek(edit, slot, WORLD_EDIT_CONSUMER_SAVE) & (1ull << column_section_of(SS_WATER_Z))) !=
          0u);
    for (uint32_t i = 0u; i < 100u; i++) {
        world_tick_step(tick);
    }
    uint16_t *const blocks = test_alloc(t, SS_WATER_CELLS * sizeof(uint16_t));
    uint8_t *const levels = test_alloc(t, SS_WATER_CELLS);
    ss_water_look(w, blocks, levels);
    REQUIRE(save_world_flush(&save->world, edit) == SAVE_WORLD_OK);
    save_close(save);
    test_world_end(w);

    /* A new world, and what the game does as each column comes in. */
    ss_water_begin(t, w);
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    tick = &w->world->tick;
    edit = &w->world->edit;
    save_world_levels_from(&save->world, tick);
    uint16_t *const states = test_alloc(t, SS_COLUMN_VOXELS * sizeof(uint16_t));
    uint8_t *const scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);
    uint8_t *const pages = test_alloc(t, SAVE_WORLD_OVERLAY_LEVELS_BYTES);
    uint64_t pages_found = 0u;
    for (int32_t chunk_y = -1; chunk_y <= 1; chunk_y++) {
        for (int32_t chunk_x = -1; chunk_x <= 1; chunk_x++) {
            uint64_t levels_mask = 0u;
            const uint64_t mask =
                save_world_column_overlay_levels(&save->world, chunk_x, chunk_y, states, pages, &levels_mask, scratch);
            CHECK_EQ_U64(levels_mask & ~mask, 0u);
            column_t *const column = world_map_column_at(w->map, chunk_x, chunk_y);
            REQUIRE(column != nullptr);
            for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
                if ((mask & (1ull << section)) != 0u) {
                    CHECK(section_blocks_import(&column->sections[section], &w->world->section_pool,
                                                states + (uint64_t)section * SECTION_VOXELS));
                }
                if ((levels_mask & (1ull << section)) != 0u) {
                    world_tick_section_levels_restore(tick, chunk_x, chunk_y, section,
                                                      pages + (uint64_t)section * SAVE_WORLD_RECORD_LEVELS_BYTES);
                    pages_found++;
                }
            }
        }
    }
    CHECK(pages_found >= 2u);
    uint16_t *const blocks_back = test_alloc(t, SS_WATER_CELLS * sizeof(uint16_t));
    uint8_t *const levels_back = test_alloc(t, SS_WATER_CELLS);
    ss_water_look(w, blocks_back, levels_back);
    CHECK_MEM_EQ(blocks_back, blocks, SS_WATER_CELLS * sizeof(uint16_t));
    CHECK_MEM_EQ(levels_back, levels, SS_WATER_CELLS);
    /* What was flowing has been woken. */
    CHECK(world_tick_pending(tick) > 0u);

    /* A later save of something else in that region keeps them: the old file's record goes over as it was. */
    REQUIRE(world_edit_block_set(edit, 1, 1, 100, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(save_world_flush(&save->world, edit) == SAVE_WORLD_OK);
    uint64_t levels_mask = 0u;
    (void)save_world_column_overlay_levels(&save->world, 0, 0, states, pages, &levels_mask, scratch);
    CHECK((levels_mask & (1ull << column_section_of(SS_WATER_Z))) != 0u);

    /* It settled before the save, so left alone it stays as it was. */
    for (uint32_t i = 0u; i < 40u; i++) {
        world_tick_step(tick);
    }
    ss_water_look(w, blocks_back, levels_back);
    CHECK_MEM_EQ(blocks_back, blocks, SS_WATER_CELLS * sizeof(uint16_t));
    CHECK_MEM_EQ(levels_back, levels, SS_WATER_CELLS);

    /* And with its two sources gone it all runs dry: had it come back as sources, every voxel of it would stay, and feed more. */
    REQUIRE(world_edit_block_set(edit, 8, 8, SS_WATER_Z, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(edit, 12, 8, SS_WATER_Z + 1, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);
    for (uint32_t i = 0u; i < 800u; i++) {
        world_tick_step(tick);
    }
    ss_water_look(w, blocks_back, levels_back);
    uint32_t water_left = 0u;
    for (uint32_t i = 0u; i < SS_WATER_CELLS; i++) {
        water_left += blocks_back[i] == BLOCK_ID_WATER ? 1u : 0u;
    }
    CHECK_EQ_U64(water_left, 0u);
    save_deinit(save);
    test_world_end(w);
    ss_temp_remove(t, root);
}

TEST(the_owner_holds_one_region_and_a_second_arena_for_the_state) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    heap_counters_t before;
    heap_counters_read(&before);
    save_t *const save = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));
    heap_counters_t held;
    heap_counters_read(&held);
    CHECK_EQ_U64(held.live_regions, before.live_regions + 1u);
    CHECK(save->owned_region.bytes >= SAVE_WORLD_ARENA_BYTES + SAVE_STATE_ARENA_BYTES);
    for (uint32_t i = 0u; i < 3u; i++) {
        REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
        CHECK(save->arena_state.used <= SAVE_STATE_ARENA_BYTES);
        CHECK_EQ_U64(save->arena_state.failed_count, 0u);
        save_close(save);
        CHECK_EQ_U64(save->arena_state.used, 0u);
        CHECK_EQ_U64(save->state.open, 0u);
    }
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    save_deinit(save);
    heap_counters_t after;
    heap_counters_read(&after);
    CHECK_EQ_U64(after.live_regions, before.live_regions);
    ss_temp_remove(t, root);
}

TEST_MAIN()
