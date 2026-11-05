/* ctest run_save_state_names. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "file/index.h"
#include "log/index.h"
#include "save/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "tables/mobs/index.h"
#include "tables/recipes/index.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint64_t {
    NS_PATH_BYTES = 512u,
};
static const uint64_t NS_SEED = 909u;

/* A world's worth of state, and where it is. */
typedef struct ns_rig_t {
    test_world_t w;           /* 0 */
    player_t *player;         /* 24 */
    item_t *item;             /* 32 */
    entity_t *entity;         /* 40 */
    mob_t *mob;               /* 48 */
    mob_spawn_t *spawn;       /* 56 */
    item_stack_t *held;       /* 64 */
    save_t *save;             /* 72 */
    const char *root;         /* 80 */
    save_state_parts_t parts; /* 88 */
} ns_rig_t;
static_assert(sizeof(ns_rig_t) == 184);

/* An older build's table, this one's with one name left out, or with a name no build has in its place. */
typedef struct ns_table_t {
    const char **names; /* 0: what each of its numbers means */
    uint32_t count;     /* 8 */
    uint32_t skip;      /* 12: the id of this build's it has no number for, when dropped */
    uint8_t dropped;    /* 16: that one has no place at all, so those after it are one less */
    uint8_t _pad0[7];   /* 17 */
} ns_table_t;
static_assert(sizeof(ns_table_t) == 24);

static uint64_t ns_fnv(const void *data, uint64_t bytes) {
    const uint8_t *const cursor = data;
    uint64_t hash = 14695981039346656037ull;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash ^= cursor[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static char *ns_join(test_context_t *t, const char *left, const char *right) {
    char *const path = test_alloc(t, NS_PATH_BYTES);
    snprintf(path, NS_PATH_BYTES, "%s/%s", left, right);
    return path;
}

static char *ns_temp_directory(test_context_t *t) {
    char *const directory = test_alloc(t, NS_PATH_BYTES);
    snprintf(directory, NS_PATH_BYTES, "/tmp/mc_save_state_names_XXXXXX");
    return mkdtemp(directory);
}

static void ns_temp_remove(test_context_t *t, const char *directory) {
    if (directory == nullptr || strncmp(directory, "/tmp/mc_save_state_names_", 25u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", (char *)directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static uint8_t *ns_slurp(test_context_t *t, const char *path, uint64_t *out_bytes) {
    uint8_t *const bytes = test_alloc(t, SAVE_STATE_IMAGE_BYTES);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, SAVE_STATE_IMAGE_BYTES, out_bytes), FILE_OK)) {
        *out_bytes = 0u;
    }
    return bytes;
}

static bool ns_rig_begin(test_context_t *t, ns_rig_t *rig, const char *root, save_world_status_t expected) {
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
    memset(rig->held, 0, sizeof(item_stack_t));
    rig->save = test_world_aligned(t, sizeof(save_t));
    test_world_fill(t, &rig->w, -16, -16, 0, 31, 31, 0, BLOCK_ID_STONE);
    player_init(rig->player, rig->w.physics);
    if (!CHECK(item_init(rig->item)) || !CHECK(entity_init(rig->entity, rig->w.physics)) ||
        !CHECK(mob_init(rig->mob, nullptr)) || !CHECK(save_init(rig->save))) {
        return false;
    }
    if (!CHECK_EQ_U64(save_open(rig->save, root, NS_SEED), expected)) {
        return false;
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

static void ns_rig_end(ns_rig_t *rig) {
    mob_close(rig->mob);
    mob_deinit(rig->mob);
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    item_close(rig->item);
    item_deinit(rig->item);
    player_deinit(rig->player);
    save_deinit(rig->save);
    test_world_end(&rig->w);
}

/* ---- An older build's tables, and a file of its ------------------------------------------------------------------------ */

static uint16_t ns_old_of(const ns_table_t *table, uint32_t live) {
    return (uint16_t)(table->dropped != 0u && live > table->skip ? live - 1u : live);
}

/* This build's table without one name. */
static void ns_table_without(test_context_t *t, ns_table_t *out, save_names_kind_t kind, uint32_t skip) {
    const uint32_t live = save_names_live_count(kind);
    out->names = test_alloc(t, live * sizeof(const char *));
    out->count = 0u;
    out->skip = skip;
    out->dropped = 1u;
    for (uint32_t id = 0u; id < live; id++) {
        if (id != skip) {
            out->names[out->count] = save_names_live_name(kind, id);
            out->count++;
        }
    }
}

/* This build's table with another name in the place of one. */
static void ns_table_renamed(test_context_t *t, ns_table_t *out, save_names_kind_t kind, uint32_t skip,
                             const char *name) {
    const uint32_t live = save_names_live_count(kind);
    out->names = test_alloc(t, live * sizeof(const char *));
    out->count = live;
    out->skip = skip;
    out->dropped = 0u;
    for (uint32_t id = 0u; id < live; id++) {
        out->names[id] = id == skip ? name : save_names_live_name(kind, id);
    }
}

static void ns_table_same(test_context_t *t, ns_table_t *out, save_names_kind_t kind) {
    ns_table_renamed(t, out, kind, UINT32_MAX, nullptr);
}

/* One list as it goes on the disk. Returns its bytes, and adds its names to a hash. */
static uint64_t ns_list_write(const ns_table_t *table, uint8_t *out, uint64_t *hash) {
    uint64_t at = sizeof(save_names_head_t);
    for (uint32_t i = 0u; i < table->count; i++) {
        const uint64_t bytes = strlen(table->names[i]) + 1u;
        memcpy(out + at, table->names[i], bytes);
        for (uint64_t c = 0u; c < bytes; c++) {
            *hash = (*hash ^ out[at + c]) * 1099511628211ull;
        }
        at += bytes;
    }
    const save_names_head_t head = {.count = table->count, .bytes = (uint32_t)(at - sizeof(save_names_head_t))};
    memcpy(out, &head, sizeof(head));
    while ((at & 7u) != 0u) {
        out[at] = 0u;
        at++;
    }
    return at;
}

/* The state file at this path with these tables for its own, and each hash made to agree: what that older build would have written. */
static void ns_file_tables_set(test_context_t *t, const char *path, const ns_table_t tables[SAVE_NAMES_KIND_COUNT]) {
    uint64_t bytes = 0u;
    uint8_t *const file = ns_slurp(t, path, &bytes);
    uint64_t names_bytes = 0u;
    const uint8_t *const names = save_state_section_find(file, bytes, SAVE_STATE_SECTION_NAMES, &names_bytes);
    REQUIRE(names != nullptr);
    /* Last in the file, and everything before it stays. */
    const uint64_t names_at = (uint64_t)(names - file);
    uint64_t hash = 14695981039346656037ull;
    uint64_t at = names_at;
    for (uint32_t kind = 0u; kind < SAVE_NAMES_KIND_COUNT; kind++) {
        at += ns_list_write(&tables[kind], file + at, &hash);
    }
    save_state_section_t section;
    memcpy(&section, file + names_at - sizeof(section), sizeof(section));
    section.bytes = at - names_at;
    memcpy(file + names_at - sizeof(section), &section, sizeof(section));
    save_state_header_t header;
    memcpy(&header, file, sizeof(header));
    header.table_hash = hash;
    header.sections_bytes = at - sizeof(header);
    header.sections_hash = ns_fnv(file + sizeof(header), header.sections_bytes);
    header.checksum = ns_fnv(&header, offsetof(save_state_header_t, checksum));
    memcpy(file, &header, sizeof(header));
    CHECK_EQ_U64(file_write_atomic(path, file, at), FILE_OK);
}

/* What a body of the three is. A build from before a mob cannot have one of it, so a zombie stands in. */
static uint16_t ns_body_kind(const ns_table_t *mobs, uint32_t body) {
    static const uint16_t kinds[3] = {MOB_ID_ZOMBIE, MOB_ID_SKELETON, MOB_ID_CREEPER};
    return mobs->dropped != 0u && kinds[body] == mobs->skip ? (uint16_t)MOB_ID_ZOMBIE : kinds[body];
}

/* A stack of this item, in an older build's number. */
static item_stack_t ns_stack(const ns_table_t *items, uint16_t item, uint32_t count) {
    item_stack_t stack = item_stack_make(item, count);
    stack.item = ns_old_of(items, item);
    return stack;
}

/* Something of each kind that holds a number, in these tables' numbers, as the older build would have saved it. Returns the handles of the bodies. */
static void ns_old_fill(ns_rig_t *rig, const ns_table_t *blocks, const ns_table_t *drops, const ns_table_t *items,
                        const ns_table_t *mobs, entity_handle_t out_bodies[3]) {
    player_t *const player = rig->player;
    item_t *const item = rig->item;
    *inventory_slot(&item->inventory, 0u) = ns_stack(items, ITEM_ID_DIRT, 40u);
    *inventory_slot(&item->inventory, 1u) = ns_stack(items, ITEM_ID_COAL, 5u);
    item_stack_t sword = ns_stack(items, ITEM_ID_DIAMOND_SWORD, 1u);
    sword.wear = 9u;
    *inventory_slot(&item->inventory, 20u) = sword;
    item_stack_t helmet = ns_stack(items, ITEM_ID_IRON_HELMET, 1u);
    helmet.wear = 3u;
    item->equipment.armor[0] = helmet;
    item->crafting_hands.cells[0] = ns_stack(items, ITEM_ID_OAK_LOG, 1u);
    item->crafting_table.cells[8] = ns_stack(items, ITEM_ID_RAW_IRON, 3u);
    item->borrowed_crafting_bench->cells[4] = ns_stack(items, ITEM_ID_STICK, 2u);
    *rig->held = ns_stack(items, ITEM_ID_STONE, 3u);
    container_chest_t *const chest = container_open(&item->chests, 7, 8, 1);
    *container_slot(chest, 26u) = ns_stack(items, ITEM_ID_STONE, 12u);
    *container_slot(chest, 2u) = ns_stack(items, ITEM_ID_COAL, 1u);
    smelting_furnace_t *const furnace = smelting_open(&item->furnaces, 9, 9, 1);
    furnace->slots[SMELTING_SLOT_INPUT] = ns_stack(items, ITEM_ID_RAW_IRON, 2u);
    furnace->slots[SMELTING_SLOT_FUEL] = ns_stack(items, ITEM_ID_COAL, 2u);
    furnace->cook_ticks = 5u;
    furnace->cooking = ns_old_of(items, ITEM_ID_RAW_IRON);
    smelting_furnace_t *const second = smelting_open(&item->furnaces, 10, 9, 1);
    second->cook_ticks = 7u;
    second->cooking = ns_old_of(items, ITEM_ID_COAL);

    player_interaction_t *const hands = &player->interaction;
    hands->hand_block = ns_old_of(blocks, BLOCK_ID_STONE);
    hands->hand_count = 9u;
    hands->mining = 1u;
    hands->mining_block = ns_old_of(blocks, BLOCK_ID_DIRT);
    hands->progress = 123u;
    hands->last_mined = ns_old_of(blocks, BLOCK_ID_DIRT);
    hands->drop_head = 30u;
    hands->drop_count = 3u;
    const uint16_t ring_rows[3] = {item_stack_drop_row(ITEM_ID_DIRT), item_stack_drop_row(ITEM_ID_COAL),
                                   item_stack_drop_row(ITEM_ID_STONE)};
    for (uint32_t i = 0u; i < 3u; i++) {
        player_drop_t *const drop = &hands->drops[(hands->drop_head + i) % PLAYER_INTERACTION_DROPS];
        memset(drop, 0, sizeof(*drop));
        drop->item = ns_old_of(drops, ring_rows[i]);
        drop->count = (uint8_t)(i + 1u);
    }
    player->survival.held_item = ns_old_of(items, ITEM_ID_STONE);
    player->combat.held_item = ns_old_of(items, ITEM_ID_DIAMOND_SWORD);

    entity_t *const entity = rig->entity;
    const float64_t drop_at[3] = {6.5f64, 6.5f64, 3.0f64};
    item_drop_spawn(&entity->item_drops, ns_old_of(drops, item_stack_drop_row(ITEM_ID_COAL)), 7u, drop_at);
    const float64_t second_at[3] = {90.5f64, 6.5f64, 3.0f64};
    item_drop_spawn(&entity->item_drops, ns_old_of(drops, item_stack_drop_row(ITEM_ID_STONE)), 8u, second_at);

    const float64_t feet[3][3] = {{10.5f64, 12.5f64, 1.0f64}, {12.5f64, 12.5f64, 3.0f64}, {14.5f64, 10.5f64, 1.0f64}};
    for (uint32_t i = 0u; i < 3u; i++) {
        const uint16_t kind = ns_body_kind(mobs, i);
        out_bodies[i] = mob_spawn_body(rig->spawn, &entity->store, kind, feet[i], 0.5f32);
        entity->store.borrowed_kind[i] = ns_old_of(mobs, kind);
    }
    rig->parts.time = (save_state_time_t){
        .world_ticks = 1234u, .day_units = 0x12345678u, .yaw = 1.25f32, .pitch = -0.5f32, ._pad0 = {0}};
}

/* The rig's state as a file, of an older build. */
static void ns_old_write(test_context_t *t, ns_rig_t *rig, const ns_table_t tables[SAVE_NAMES_KIND_COUNT]) {
    save_state_capture(&rig->save->state, &rig->parts);
    REQUIRE(save_state_write(&rig->save->state, 0u) == SAVE_WORLD_OK);
    ns_file_tables_set(t, ns_join(t, rig->root, "state.0"), tables);
}

/* The game's next save of it, and that it believes what it wrote. */
static void ns_saved_again_is_believed(test_context_t *t, ns_rig_t *rig) {
    save_state_capture(&rig->save->state, &rig->parts);
    CHECK_EQ_U64(save_state_check(rig->save->state.borrowed_image, rig->save->state.image_bytes, NS_SEED, 0u),
                 SAVE_WORLD_ERROR_DAMAGED); /* the image has no hashes until it is written */
    REQUIRE(save_state_write(&rig->save->state, 0u) == SAVE_WORLD_OK);
    uint64_t bytes = 0u;
    const uint8_t *const file = ns_slurp(t, ns_join(t, rig->root, "state.0"), &bytes);
    CHECK_EQ_U64(save_state_check(file, bytes, NS_SEED, 0u), SAVE_WORLD_OK);
}

/* ---- Tables that moved ------------------------------------------------------------------------------------------------- */

static void ns_moved_test(test_context_t *t, uint32_t skip_block, uint32_t skip_item, uint32_t skip_mob) {
    char *const root = ns_temp_directory(t);
    REQUIRE(root != nullptr);
    ns_table_t tables[SAVE_NAMES_KIND_COUNT];
    ns_table_without(t, &tables[SAVE_NAMES_KIND_BLOCKS], SAVE_NAMES_KIND_BLOCKS, skip_block);
    ns_table_without(t, &tables[SAVE_NAMES_KIND_DROPS], SAVE_NAMES_KIND_DROPS,
                     item_stack_drop_row((uint16_t)skip_item));
    ns_table_without(t, &tables[SAVE_NAMES_KIND_ITEMS], SAVE_NAMES_KIND_ITEMS, skip_item);
    ns_table_without(t, &tables[SAVE_NAMES_KIND_MOBS], SAVE_NAMES_KIND_MOBS, skip_mob);

    ns_rig_t *const first = test_alloc(t, sizeof(ns_rig_t));
    REQUIRE(ns_rig_begin(t, first, root, SAVE_WORLD_OK));
    entity_handle_t bodies[3];
    ns_old_fill(first, &tables[SAVE_NAMES_KIND_BLOCKS], &tables[SAVE_NAMES_KIND_DROPS], &tables[SAVE_NAMES_KIND_ITEMS],
                &tables[SAVE_NAMES_KIND_MOBS], bodies);
    ns_old_write(t, first, tables);
    ns_rig_end(first);

    /* What is on the disk says it is from other tables: the check alone will not have it, and the way in will. */
    uint64_t on_disk_bytes = 0u;
    const uint8_t *const on_disk = ns_slurp(t, ns_join(t, root, "state.0"), &on_disk_bytes);
    CHECK_EQ_U64(save_state_check(on_disk, on_disk_bytes, NS_SEED, 0u), SAVE_WORLD_ERROR_TABLES);

    ns_rig_t *const rig = test_alloc(t, sizeof(ns_rig_t));
    REQUIRE(ns_rig_begin(t, rig, root, SAVE_WORLD_OK));
    REQUIRE(save_state_apply(&rig->save->state, &rig->parts));

    item_t *const item = rig->item;
    const item_stack_t *const slot_0 = inventory_slot(&item->inventory, 0u);
    CHECK_EQ_U64(slot_0->item, ITEM_ID_DIRT);
    CHECK_EQ_U64(slot_0->count, 40u);
    CHECK_EQ_U64(inventory_slot(&item->inventory, 1u)->item, ITEM_ID_COAL);
    const item_stack_t *const slot_20 = inventory_slot(&item->inventory, 20u);
    CHECK_EQ_U64(slot_20->item, ITEM_ID_DIAMOND_SWORD);
    CHECK_EQ_U64(slot_20->wear, 9u);
    CHECK_EQ_U64(item->equipment.armor[0].item, ITEM_ID_IRON_HELMET);
    CHECK_EQ_U64(item->equipment.armor[0].wear, 3u);
    CHECK_EQ_U64(item->crafting_hands.cells[0].item, ITEM_ID_OAK_LOG);
    CHECK_EQ_U64(item->crafting_table.cells[8].item, ITEM_ID_RAW_IRON);
    CHECK_EQ_U64(item->borrowed_crafting_bench->cells[4].item, ITEM_ID_STICK);
    CHECK_EQ_U64(item->borrowed_crafting_bench->cells[4].count, 2u);
    CHECK_EQ_U64(rig->held->item, ITEM_ID_STONE);
    container_chest_t *const chest = container_at(&item->chests, 7, 8, 1);
    REQUIRE(chest != nullptr);
    CHECK_EQ_U64(container_slot(chest, 26u)->item, ITEM_ID_STONE);
    CHECK_EQ_U64(container_slot(chest, 26u)->count, 12u);
    CHECK_EQ_U64(container_slot(chest, 2u)->item, ITEM_ID_COAL);
    const smelting_furnace_t *const furnace = smelting_at(&item->furnaces, 9, 9, 1);
    REQUIRE(furnace != nullptr);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_INPUT].item, ITEM_ID_RAW_IRON);
    CHECK_EQ_U64(furnace->slots[SMELTING_SLOT_FUEL].item, ITEM_ID_COAL);
    CHECK_EQ_U64(furnace->cooking, ITEM_ID_RAW_IRON);
    CHECK_EQ_U64(furnace->cook_ticks, 5u);
    /* What a log in a grid makes, by this build's recipes. */
    CHECK_EQ_U64(item->crafting_hands.recipe,
                 crafting_match(item->crafting_hands.cells, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE));
    CHECK(item->crafting_hands.recipe != CRAFTING_NO_RECIPE);
    CHECK(!item_stack_is_empty(&item->crafting_hands.output));

    const player_interaction_t *const hands = &rig->player->interaction;
    CHECK_EQ_U64(hands->hand_block, BLOCK_ID_STONE);
    CHECK_EQ_U64(hands->hand_count, 9u);
    CHECK_EQ_U64(hands->mining_block, BLOCK_ID_DIRT);
    CHECK_EQ_U64(hands->mining, 1u);
    CHECK_EQ_U64(hands->last_mined, BLOCK_ID_DIRT);
    CHECK_EQ_U64(hands->drop_count, 3u);
    const uint16_t ring_rows[3] = {item_stack_drop_row(ITEM_ID_DIRT), item_stack_drop_row(ITEM_ID_COAL),
                                   item_stack_drop_row(ITEM_ID_STONE)};
    for (uint32_t i = 0u; i < 3u; i++) {
        CHECK_EQ_U64(hands->drops[(hands->drop_head + i) % PLAYER_INTERACTION_DROPS].item, ring_rows[i]);
    }
    CHECK_EQ_U64(rig->player->survival.held_item, ITEM_ID_STONE);
    CHECK_EQ_U64(rig->player->combat.held_item, ITEM_ID_DIAMOND_SWORD);

    const item_drop_store_t *const drops = &rig->entity->item_drops;
    REQUIRE(drops->count == 2u);
    CHECK_EQ_U64(drops->borrowed_items[0].item, item_stack_drop_row(ITEM_ID_COAL));
    CHECK_EQ_U64(drops->borrowed_items[0].count, 7u);
    CHECK_EQ_U64(drops->borrowed_items[1].item, item_stack_drop_row(ITEM_ID_STONE));

    const entity_store_t *const store = &rig->entity->store;
    REQUIRE(store->count == 3u);
    for (uint32_t i = 0u; i < 3u; i++) {
        CHECK_EQ_U64(store->borrowed_kind[i], ns_body_kind(&tables[SAVE_NAMES_KIND_MOBS], i));
        uint32_t index = 0u;
        CHECK(entity_store_index_of(store, bodies[i], &index));
        CHECK_EQ_U64(index, i);
    }
    CHECK_EQ_U64(rig->parts.time.world_ticks, 1234u);
    ns_saved_again_is_believed(t, rig);
    ns_rig_end(rig);
    ns_temp_remove(t, root);
}

TEST(tables_that_moved_at_the_start_keep_every_stack_drop_block_and_body) {
    /* What comes first in each moves all that follow. Id 0 of each is always air or none. */
    ns_moved_test(t, 1u, 1u, 1u);
}

TEST(tables_that_moved_in_the_middle_keep_every_stack_drop_block_and_body) {
    ns_moved_test(t, BLOCK_COUNT / 2u, ITEM_COUNT / 2u, MOB_COUNT / 2u);
}

/* ---- Tables that lost something ------------------------------------------------------------------------------------------ */

TEST(what_a_build_does_not_have_goes_and_the_log_says_what_and_the_world_opens) {
    char *const root = ns_temp_directory(t);
    REQUIRE(root != nullptr);
    /* Coal and the creeper and stone, under names no build has, at their own numbers, so that all else means what it did. */
    ns_table_t tables[SAVE_NAMES_KIND_COUNT];
    ns_table_renamed(t, &tables[SAVE_NAMES_KIND_BLOCKS], SAVE_NAMES_KIND_BLOCKS, BLOCK_ID_STONE, "gone_block");
    ns_table_renamed(t, &tables[SAVE_NAMES_KIND_DROPS], SAVE_NAMES_KIND_DROPS, item_stack_drop_row(ITEM_ID_COAL),
                     "gone_item");
    ns_table_renamed(t, &tables[SAVE_NAMES_KIND_ITEMS], SAVE_NAMES_KIND_ITEMS, ITEM_ID_COAL, "gone_item");
    ns_table_renamed(t, &tables[SAVE_NAMES_KIND_MOBS], SAVE_NAMES_KIND_MOBS, MOB_ID_SKELETON, "gone_mob");

    ns_rig_t *const first = test_alloc(t, sizeof(ns_rig_t));
    REQUIRE(ns_rig_begin(t, first, root, SAVE_WORLD_OK));
    entity_handle_t bodies[3];
    ns_old_fill(first, &tables[SAVE_NAMES_KIND_BLOCKS], &tables[SAVE_NAMES_KIND_DROPS], &tables[SAVE_NAMES_KIND_ITEMS],
                &tables[SAVE_NAMES_KIND_MOBS], bodies);
    const uint64_t lost_before = first->entity->item_drops.lost_count;
    const uint64_t removed_before = first->entity->store.removed_count;
    ns_old_write(t, first, tables);
    ns_rig_end(first);

    const char *const log_path = ns_join(t, root, "log");
    const int32_t log_fd = open(log_path, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    REQUIRE(log_fd >= 0);
    const int32_t previous = log_set_fd(log_fd);
    ns_rig_t *const rig = test_alloc(t, sizeof(ns_rig_t));
    const bool opened = ns_rig_begin(t, rig, root, SAVE_WORLD_OK);
    (void)log_set_fd(previous);
    (void)close(log_fd);
    REQUIRE(opened);
    REQUIRE(save_state_apply(&rig->save->state, &rig->parts));
    uint64_t said_bytes = 0u;
    char *const said = (char *)ns_slurp(t, log_path, &said_bytes);
    said[said_bytes] = '\0';
    CHECK(strstr(said, "gone_block") != nullptr);
    CHECK(strstr(said, "gone_item") != nullptr);
    CHECK(strstr(said, "gone_mob") != nullptr);
    CHECK(strstr(said, "are gone") != nullptr);

    item_t *const item = rig->item;
    /* Coal is gone from wherever it was, and every slot is the one empty form. */
    CHECK(item_stack_is_empty(inventory_slot(&item->inventory, 1u)));
    CHECK(item_stack_is_valid(inventory_slot(&item->inventory, 1u)));
    CHECK_EQ_U64(inventory_slot(&item->inventory, 0u)->item, ITEM_ID_DIRT);
    container_chest_t *const chest = container_at(&item->chests, 7, 8, 1);
    REQUIRE(chest != nullptr);
    CHECK(item_stack_is_empty(container_slot(chest, 2u)));
    CHECK_EQ_U64(container_slot(chest, 26u)->item, ITEM_ID_STONE);
    const smelting_furnace_t *const furnace = smelting_at(&item->furnaces, 9, 9, 1);
    REQUIRE(furnace != nullptr);
    CHECK(item_stack_is_empty(&furnace->slots[SMELTING_SLOT_FUEL]));
    CHECK_EQ_U64(furnace->cooking, ITEM_ID_RAW_IRON);
    /* What was cooking has gone, so nothing is. */
    const smelting_furnace_t *const second = smelting_at(&item->furnaces, 10, 9, 1);
    REQUIRE(second != nullptr);
    CHECK_EQ_U64(second->cooking, ITEM_ID_NONE);
    CHECK_EQ_U64(second->cook_ticks, 0u);

    /* The stone in hand has gone, and so has that it was being placed; the dirt being mined has not. */
    const player_interaction_t *const hands = &rig->player->interaction;
    CHECK_EQ_U64(hands->hand_count, 0u);
    CHECK_EQ_U64(hands->hand_block, BLOCK_ID_AIR);
    CHECK_EQ_U64(hands->mining, 1u);
    CHECK_EQ_U64(hands->mining_block, BLOCK_ID_DIRT);
    /* Coal in the ring: what was after it closes up, in order. */
    CHECK_EQ_U64(hands->drop_count, 2u);
    CHECK_EQ_U64(hands->drops[hands->drop_head].item, item_stack_drop_row(ITEM_ID_DIRT));
    CHECK_EQ_U64(hands->drops[(hands->drop_head + 1u) % PLAYER_INTERACTION_DROPS].item,
                 item_stack_drop_row(ITEM_ID_STONE));

    /* Coal on the ground: the stone is left, and the store's books add up. */
    const item_drop_store_t *const drops = &rig->entity->item_drops;
    REQUIRE(drops->count == 1u);
    CHECK_EQ_U64(drops->borrowed_items[0].item, item_stack_drop_row(ITEM_ID_STONE));
    CHECK_EQ_U64(drops->borrowed_items[0].count, 8u);
    CHECK_EQ_U64(drops->lost_count, lost_before + 7u);

    /* The skeleton: the other two are as they were, under their own handles, and its slot is free to be taken again. */
    entity_store_t *const store = &rig->entity->store;
    REQUIRE(store->count == 2u);
    CHECK_EQ_U64(store->removed_count, removed_before + 1u);
    uint32_t index = 0u;
    CHECK(!entity_store_index_of(store, bodies[1], &index));
    REQUIRE(entity_store_index_of(store, bodies[0], &index));
    CHECK_EQ_U64(store->borrowed_kind[index], MOB_ID_ZOMBIE);
    REQUIRE(entity_store_index_of(store, bodies[2], &index));
    CHECK_EQ_U64(store->borrowed_kind[index], MOB_ID_CREEPER);
    CHECK_EQ_I64((int64_t)store->borrowed_position[0][index], 14);
    const float64_t feet[3] = {4.5f64, 4.5f64, 1.0f64};
    for (uint32_t i = 0u; i < ENTITY_STORE_CAPACITY - 2u; i++) {
        REQUIRE(mob_spawn_body(rig->spawn, store, MOB_ID_ZOMBIE, feet, 0.5f32) != ENTITY_HANDLE_NONE);
    }
    CHECK_EQ_U64(store->count, ENTITY_STORE_CAPACITY);
    CHECK(entity_store_index_of(store, bodies[0], &index));
    CHECK(entity_store_index_of(store, bodies[2], &index));
    ns_saved_again_is_believed(t, rig);
    ns_rig_end(rig);
    ns_temp_remove(t, root);
}

TEST(a_block_being_mined_that_has_gone_stops_the_mining_and_a_stack_keeps_to_what_its_item_is_today) {
    char *const root = ns_temp_directory(t);
    REQUIRE(root != nullptr);
    /* Dirt, under a name no build has, at its own number, and items that moved, so that the file is from other tables. */
    ns_table_t tables[SAVE_NAMES_KIND_COUNT];
    ns_table_renamed(t, &tables[SAVE_NAMES_KIND_BLOCKS], SAVE_NAMES_KIND_BLOCKS, BLOCK_ID_DIRT, "gone_block");
    ns_table_without(t, &tables[SAVE_NAMES_KIND_DROPS], SAVE_NAMES_KIND_DROPS, item_stack_drop_row(1u));
    ns_table_without(t, &tables[SAVE_NAMES_KIND_ITEMS], SAVE_NAMES_KIND_ITEMS, 1u);
    ns_table_without(t, &tables[SAVE_NAMES_KIND_MOBS], SAVE_NAMES_KIND_MOBS, 1u);

    ns_rig_t *const first = test_alloc(t, sizeof(ns_rig_t));
    REQUIRE(ns_rig_begin(t, first, root, SAVE_WORLD_OK));
    entity_handle_t bodies[3];
    ns_old_fill(first, &tables[SAVE_NAMES_KIND_BLOCKS], &tables[SAVE_NAMES_KIND_DROPS], &tables[SAVE_NAMES_KIND_ITEMS],
                &tables[SAVE_NAMES_KIND_MOBS], bodies);
    /* What an older build's limits allowed: more of it in a slot than today, worn further than it lasts today, and worn where nothing wears today. */
    const uint32_t lasts = item_defs[ITEM_ID_DIAMOND_SWORD].durability;
    REQUIRE(lasts > 1u);
    REQUIRE(item_stack_max(ITEM_ID_DIAMOND_SWORD) < 5u);
    item_stack_t *const swords = inventory_slot(&first->item->inventory, 2u);
    *swords = ns_stack(&tables[SAVE_NAMES_KIND_ITEMS], ITEM_ID_DIAMOND_SWORD, 1u);
    swords->count = 5u;
    swords->wear = (uint16_t)(lasts + 40u);
    item_stack_t *const worn_stone = inventory_slot(&first->item->inventory, 3u);
    *worn_stone = ns_stack(&tables[SAVE_NAMES_KIND_ITEMS], ITEM_ID_STONE, 2u);
    worn_stone->wear = 7u;
    ns_old_write(t, first, tables);
    ns_rig_end(first);

    ns_rig_t *const rig = test_alloc(t, sizeof(ns_rig_t));
    REQUIRE(ns_rig_begin(t, rig, root, SAVE_WORLD_OK));
    REQUIRE(save_state_apply(&rig->save->state, &rig->parts));
    const player_interaction_t *const hands = &rig->player->interaction;
    CHECK_EQ_U64(hands->mining, 0u);
    CHECK_EQ_U64(hands->progress, 0u);
    CHECK_EQ_U64(hands->mining_block, BLOCK_ID_AIR);
    CHECK_EQ_U64(hands->last_mined, BLOCK_ID_AIR);
    /* The stone in hand was never what went. */
    CHECK_EQ_U64(hands->hand_block, BLOCK_ID_STONE);
    CHECK_EQ_U64(hands->hand_count, 9u);

    const item_stack_t *const swords_now = inventory_slot(&rig->item->inventory, 2u);
    CHECK_EQ_U64(swords_now->item, ITEM_ID_DIAMOND_SWORD);
    CHECK_EQ_U64(swords_now->count, item_stack_max(ITEM_ID_DIAMOND_SWORD));
    CHECK_EQ_U64(swords_now->wear, lasts - 1u);
    CHECK(item_stack_is_valid(swords_now));
    const item_stack_t *const stone_now = inventory_slot(&rig->item->inventory, 3u);
    CHECK_EQ_U64(stone_now->item, ITEM_ID_STONE);
    CHECK_EQ_U64(stone_now->count, 2u);
    CHECK_EQ_U64(stone_now->wear, 0u);
    ns_saved_again_is_believed(t, rig);
    ns_rig_end(rig);
    ns_temp_remove(t, root);
}

/* ---- Recipes, which have no names ---------------------------------------------------------------------------------------- */

TEST(what_a_grid_makes_comes_from_this_builds_recipes_and_no_file_can_stop_a_world_with_it) {
    char *const root = ns_temp_directory(t);
    REQUIRE(root != nullptr);
    ns_rig_t *const first = test_alloc(t, sizeof(ns_rig_t));
    REQUIRE(ns_rig_begin(t, first, root, SAVE_WORLD_OK));
    /* A build with more recipes than this, with what it made then. The tables are the same, so this goes in through the check alone. */
    first->item->crafting_hands.cells[0] = item_stack_make(ITEM_ID_OAK_LOG, 1u);
    first->item->crafting_hands.recipe = RECIPE_COUNT + 100u;
    first->item->crafting_hands.output = item_stack_make(ITEM_ID_DIAMOND_SWORD, 1u);
    first->item->crafting_table.recipe = 0u;
    first->item->crafting_table.output = item_stack_make(ITEM_ID_DIAMOND_SWORD, 1u);
    save_state_capture(&first->save->state, &first->parts);
    REQUIRE(save_state_write(&first->save->state, 0u) == SAVE_WORLD_OK);
    ns_rig_end(first);
    ns_rig_t *const rig = test_alloc(t, sizeof(ns_rig_t));
    REQUIRE(ns_rig_begin(t, rig, root, SAVE_WORLD_OK));
    REQUIRE(save_state_apply(&rig->save->state, &rig->parts));
    const crafting_grid_t *const hands = &rig->item->crafting_hands;
    CHECK_EQ_U64(hands->recipe, crafting_match(hands->cells, CRAFTING_SIDE_HANDS, RECIPE_STATION_TABLE));
    CHECK(hands->recipe < RECIPE_COUNT);
    CHECK_EQ_U64(hands->output.item, recipe_defs[hands->recipe].gives);
    CHECK_EQ_U64(rig->item->crafting_table.recipe, CRAFTING_NO_RECIPE);
    CHECK(item_stack_is_empty(&rig->item->crafting_table.output));
    ns_rig_end(rig);
    ns_temp_remove(t, root);
}

/* ---- What is refused ------------------------------------------------------------------------------------------------------ */

TEST(a_state_in_the_old_format_is_refused_says_why_and_is_left_as_it_was) {
    char *const root = ns_temp_directory(t);
    REQUIRE(root != nullptr);
    ns_rig_t *const first = test_alloc(t, sizeof(ns_rig_t));
    REQUIRE(ns_rig_begin(t, first, root, SAVE_WORLD_OK));
    save_state_capture(&first->save->state, &first->parts);
    REQUIRE(save_state_write(&first->save->state, 0u) == SAVE_WORLD_OK);
    ns_rig_end(first);

    const char *const path = ns_join(t, root, "state.0");
    uint64_t bytes = 0u;
    uint8_t *const file = ns_slurp(t, path, &bytes);
    save_state_header_t header;
    memcpy(&header, file, sizeof(header));
    header.version = SAVE_STATE_VERSION_FIRST_WITH_NAMES - 1u;
    header.checksum = ns_fnv(&header, offsetof(save_state_header_t, checksum));
    memcpy(file, &header, sizeof(header));
    REQUIRE(file_write_atomic(path, file, bytes) == FILE_OK);

    const char *const log_path = ns_join(t, root, "log");
    const int32_t log_fd = open(log_path, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    REQUIRE(log_fd >= 0);
    const int32_t previous = log_set_fd(log_fd);
    save_t *const save = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));
    CHECK_EQ_U64(save_open(save, root, NS_SEED), SAVE_WORLD_ERROR_VERSION);
    (void)log_set_fd(previous);
    (void)close(log_fd);
    save_deinit(save);
    uint64_t said_bytes = 0u;
    char *const said = (char *)ns_slurp(t, log_path, &said_bytes);
    said[said_bytes] = '\0';
    CHECK(strstr(said, "before a state kept the names of its tables") != nullptr);
    uint64_t after_bytes = 0u;
    const uint8_t *const after = ns_slurp(t, path, &after_bytes);
    REQUIRE(after_bytes == bytes);
    CHECK_MEM_EQ(after, file, bytes);
    ns_temp_remove(t, root);
}

TEST(a_number_a_list_does_not_have_stops_the_world_and_changes_nothing) {
    char *const root = ns_temp_directory(t);
    REQUIRE(root != nullptr);
    ns_table_t tables[SAVE_NAMES_KIND_COUNT];
    ns_table_same(t, &tables[SAVE_NAMES_KIND_BLOCKS], SAVE_NAMES_KIND_BLOCKS);
    ns_table_same(t, &tables[SAVE_NAMES_KIND_DROPS], SAVE_NAMES_KIND_DROPS);
    /* One short, so that the hash is another's, and the last item is a number it does not have. */
    ns_table_without(t, &tables[SAVE_NAMES_KIND_ITEMS], SAVE_NAMES_KIND_ITEMS, ITEM_COUNT - 1u);
    ns_table_same(t, &tables[SAVE_NAMES_KIND_MOBS], SAVE_NAMES_KIND_MOBS);
    ns_rig_t *const first = test_alloc(t, sizeof(ns_rig_t));
    REQUIRE(ns_rig_begin(t, first, root, SAVE_WORLD_OK));
    item_stack_t past = item_stack_make(ITEM_ID_DIRT, 1u);
    past.item = (uint16_t)(ITEM_COUNT - 1u);
    *inventory_slot(&first->item->inventory, 3u) = past;
    ns_old_write(t, first, tables);
    ns_rig_end(first);

    const char *const path = ns_join(t, root, "state.0");
    uint64_t before_bytes = 0u;
    const uint8_t *const before = ns_slurp(t, path, &before_bytes);
    const int32_t quiet = log_set_fd(open("/dev/null", O_WRONLY | O_CLOEXEC));
    save_t *const save = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));
    CHECK_EQ_U64(save_open(save, root, NS_SEED), SAVE_WORLD_ERROR_DAMAGED);
    save_deinit(save);
    const int32_t was_quiet = log_set_fd(quiet);
    (void)close(was_quiet);
    uint64_t after_bytes = 0u;
    const uint8_t *const after = ns_slurp(t, path, &after_bytes);
    REQUIRE(after_bytes == before_bytes);
    CHECK_MEM_EQ(after, before, before_bytes);
    ns_temp_remove(t, root);
}

TEST(a_kind_of_body_this_build_has_not_got_is_not_believed_from_a_file_of_its_own_tables) {
    char *const root = ns_temp_directory(t);
    REQUIRE(root != nullptr);
    ns_rig_t *const first = test_alloc(t, sizeof(ns_rig_t));
    REQUIRE(ns_rig_begin(t, first, root, SAVE_WORLD_OK));
    const float64_t feet[3] = {10.5f64, 12.5f64, 1.0f64};
    REQUIRE(mob_spawn_body(first->spawn, &first->entity->store, MOB_ID_ZOMBIE, feet, 0.5f32) != ENTITY_HANDLE_NONE);
    first->entity->store.borrowed_kind[0] = MOB_COUNT;
    save_state_capture(&first->save->state, &first->parts);
    REQUIRE(save_state_write(&first->save->state, 0u) == SAVE_WORLD_OK);
    ns_rig_end(first);
    uint64_t bytes = 0u;
    const uint8_t *const file = ns_slurp(t, ns_join(t, root, "state.0"), &bytes);
    const int32_t quiet = log_set_fd(open("/dev/null", O_WRONLY | O_CLOEXEC));
    CHECK_EQ_U64(save_state_check(file, bytes, NS_SEED, 0u), SAVE_WORLD_ERROR_DAMAGED);
    const int32_t was_quiet = log_set_fd(quiet);
    (void)close(was_quiet);
    ns_temp_remove(t, root);
}

TEST_MAIN()
