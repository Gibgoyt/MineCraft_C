/* ctest run_mob_animal_proof. See README.md. */
#include "harness/index.h"

#include "display/headless/index.h"
#include "display/headless/png/index.h"
#include "entity/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "gpu_bridge/post/index.h"
#include "item/index.h"
#include "job/index.h"
#include "mob/index.h"
#include "mob/spawn/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "world/index.h"

#include <errno.h>
#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum : uint32_t {
    AP_WIDTH = 640u,
    AP_HEIGHT = 360u,
    AP_HERD = 4u, /* two males and two females, in turn */
    AP_PAD = 16u, /* blocks of grass each way about the body */
};
/* A day well past the first, so that herds do not come every tick, at noon. */
#define AP_DAY 10u
#define AP_PI 3.14159265358979323846f64

typedef struct ap_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
    physics_t *physics;          /* 64 */
    player_t *player;            /* 72 */
    entity_t *entity;            /* 80 */
    item_t *item;                /* 88 */
    mob_t *mob;                  /* 96 */
    uint8_t _pad0[24];           /* 104 */
} ap_rig_t;
static_assert(sizeof(ap_rig_t) == 128);

static void *ap_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool ap_begin(test_context_t *t, ap_rig_t *rig) {
    memset(rig, 0, sizeof(*rig));
    rig->bridge = ap_aligned(t, sizeof(gpu_bridge_t));
    rig->job = ap_aligned(t, sizeof(job_system_t));
    rig->world = ap_aligned(t, sizeof(world_t));
    rig->terrain = ap_aligned(t, sizeof(terrain_t));
    rig->render = ap_aligned(t, sizeof(render_t));
    rig->texture = ap_aligned(t, sizeof(texture_t));
    rig->game = ap_aligned(t, sizeof(game_t));
    rig->physics = ap_aligned(t, sizeof(physics_t));
    rig->player = ap_aligned(t, sizeof(player_t));
    rig->entity = ap_aligned(t, sizeof(entity_t));
    rig->item = ap_aligned(t, sizeof(item_t));
    rig->mob = ap_aligned(t, sizeof(mob_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) || !CHECK(physics_init(rig->physics)) ||
        !CHECK(entity_init(rig->entity, rig->physics)) || !CHECK(item_init(rig->item)) ||
        !CHECK(mob_init(rig->mob, rig->job)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    player_init(rig->player, rig->physics);
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    game_player_set(rig->game, rig->player);
    game_entity_set(rig->game, rig->entity);
    game_mob_set(rig->game, rig->mob);
    game_item_set(rig->game, rig->item);
    return true;
}

static void ap_end(test_context_t *t, ap_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    mob_deinit(rig->mob);
    item_deinit(rig->item);
    entity_deinit(rig->entity);
    player_deinit(rig->player);
    physics_deinit(rig->physics);
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

/* The headless scene's ticks, and its buttons. */
typedef struct ap_script_t {
    display_headless_scene_t scene; /* 0 */
    display_script_state_t state;   /* 24 */
    uint32_t tick;                  /* 176 */
    uint8_t _pad0[4];               /* 180 */
} ap_script_t;
static_assert(sizeof(ap_script_t) == 184);

static bool ap_ticks(ap_script_t *script, uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        script->tick++;
        const display_headless_tick_t tick = {
            .events = nullptr, .state = &script->state, .event_count = 0u, .tick = script->tick};
        if (!script->scene.tick(script->scene.borrowed_context, &tick)) {
            return false;
        }
    }
    return true;
}

static void ap_button(ap_script_t *script, uint32_t code, bool down) {
    const uint64_t bit = 1ull << (code % 64u);
    script->state.down[code / 64u] =
        down ? script->state.down[code / 64u] | bit : script->state.down[code / 64u] & ~bit;
}

/* A press and a release of the right button, a tick each, and a tick after. */
static bool ap_use(ap_script_t *script) {
    ap_button(script, BTN_RIGHT, true);
    const bool down = ap_ticks(script, 1u);
    ap_button(script, BTN_RIGHT, false);
    return down && ap_ticks(script, 2u);
}

/* Where the pictures go: MC_ANIMAL_PROOF_DIR, or a directory of its own under /tmp, kept for a person to look at. */
static const char *ap_directory(void) {
    const char *const asked = getenv("MC_ANIMAL_PROOF_DIR");
    const char *const directory = asked != nullptr && asked[0] != '\0' ? asked : "/tmp/mc_animal_proof";
    if (mkdir(directory, 0755) != 0 && errno != EEXIST) {
        return nullptr;
    }
    return directory;
}

/* The picture as it now is, written to name in the proof's directory. */
static bool ap_screenshot(test_context_t *t, ap_script_t *script, const char *name) {
    const uint64_t bytes = post_xrgb_bytes(AP_WIDTH, AP_HEIGHT);
    display_buffer_t target = {.owned_map = ap_aligned(t, bytes),
                               .map_bytes = bytes,
                               .pitch = AP_WIDTH * 4u,
                               .width = AP_WIDTH,
                               .height = AP_HEIGHT};
    const display_headless_tick_t tick = {.events = nullptr, .state = &script->state, .event_count = 0u, .tick = 0u};
    if (!CHECK(script->scene.draw(script->scene.borrowed_context, &tick, &target))) {
        return false;
    }
    const uint64_t capacity = display_png_bytes_needed(AP_WIDTH, AP_HEIGHT);
    uint8_t *const png = test_alloc(t, capacity);
    uint64_t written = 0u;
    if (!CHECK(display_png_encode(target.owned_map, target.pitch, AP_WIDTH, AP_HEIGHT, png, capacity, &written))) {
        return false;
    }
    const char *const directory = ap_directory();
    if (!CHECK(directory != nullptr)) {
        return false;
    }
    char path[512];
    (void)snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE *const file = fopen(path, "wb");
    if (!CHECK(file != nullptr)) {
        return false;
    }
    const bool whole = (uint64_t)fwrite(png, 1u, written, file) == written;
    const bool closed = fclose(file) == 0;
    printf("  wrote %s\n", path);
    return CHECK(whole && closed);
}

/* A field of grass about the body, AP_PAD each way, open to the sky, with nothing else about: no other body, nothing lying. */
static bool ap_field(test_context_t *t, ap_rig_t *rig, ap_script_t *script) {
    const int32_t pad_x = (int32_t)floor(rig->player->movement.position[0]);
    const int32_t pad_y = (int32_t)floor(rig->player->movement.position[1]);
    const int32_t pad_z = (int32_t)floor(rig->player->movement.position[2]);
    const int32_t pad = (int32_t)AP_PAD;
    for (int32_t x = pad_x - pad; x <= pad_x + pad; x++) {
        for (int32_t y = pad_y - pad; y <= pad_y + pad; y++) {
            for (int32_t z = pad_z - 2; z <= pad_z + 16; z++) {
                const uint16_t block = z == pad_z - 1 ? BLOCK_ID_GRASS_BLOCK : z < pad_z ? BLOCK_ID_DIRT : BLOCK_ID_AIR;
                const world_edit_status_t status = world_edit_block_set(&rig->world->edit, x, y, z, block);
                if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
                    return false;
                }
            }
        }
    }
    return CHECK(game_settle(rig->game)) && CHECK(ap_ticks(script, 20u));
}

static void ap_clear(ap_rig_t *rig) {
    entity_store_t *const store = &rig->entity->store;
    while (store->count > 0u) {
        (void)entity_store_remove(store, store->borrowed_handle[store->count - 1u]);
    }
    rig->entity->item_drops.count = 0u;
}

/* A body of this mob and sex standing `ahead` blocks in front of the eye and `across` to its left, turned side on to it. */
static entity_handle_t ap_put(test_context_t *t, ap_rig_t *rig, mob_id_t mob, uint8_t sex, float64_t ahead,
                              float64_t across) {
    const float64_t *const feet = rig->player->movement.position;
    const float64_t yaw = (float64_t)rig->game->camera.yaw;
    const float64_t place[3] = {feet[0] + ahead * cos(yaw) - across * sin(yaw),
                                feet[1] + ahead * sin(yaw) + across * cos(yaw), feet[2]};
    const entity_handle_t handle = mob_spawn_body_as(&rig->game->mob_spawn, &rig->entity->store, mob, place,
                                                     (float32_t)(yaw + AP_PI / 2.0f64), sex);
    CHECK(handle != ENTITY_HANDLE_NONE);
    return handle;
}

static uint32_t ap_index(test_context_t *t, const ap_rig_t *rig, entity_handle_t handle) {
    uint32_t index = 0u;
    CHECK(entity_store_index_of(&rig->entity->store, handle, &index));
    return index;
}

/* How many of an item lie on the ground. */
static uint32_t ap_lying(const ap_rig_t *rig, uint16_t item) {
    uint32_t count = 0u;
    const item_drop_store_t *const drops = &rig->entity->item_drops;
    for (uint32_t i = 0u; i < drops->count; i++) {
        count += block_drop_items[drops->borrowed_items[i].item].item == item ? drops->borrowed_items[i].count : 0u;
    }
    return count;
}

static void ap_hold(ap_rig_t *rig, uint16_t item, uint32_t count) {
    inventory_t *const inventory = &rig->item->inventory;
    *inventory_slot(inventory, inventory->selected) = item_stack_make(item, count);
    inventory->added_count++;
}

TEST(a_herd_of_each_a_sheep_shorn_a_cow_milked_and_a_bed_of_wool_slept_in) {
    ap_rig_t rig;
    REQUIRE(ap_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = 4u,
                                  .width = AP_WIDTH,
                                  .height = AP_HEIGHT,
                                  .features_off = SHADE_FEATURE_TAA | SHADE_FEATURE_DAY_CYCLE,
                                  .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    ap_script_t *const script = test_alloc(t, sizeof(ap_script_t));
    memset(script, 0, sizeof(*script));
    game_headless_scene(game, &script->scene);
    game_clock_set(game, AP_DAY, 0.5f64);
    REQUIRE(ap_field(t, &rig, script));
    game->camera.pitch = -0.12f32;
    entity_store_t *const store = &rig.entity->store;

    /* A herd of each, from the side: males and females in turn, the males bigger, with their horns, tusks, beards and combs. */
    static const mob_id_t ANIMALS[] = {MOB_ID_CATTLE, MOB_ID_SHEEP,         MOB_ID_PIG,
                                       MOB_ID_GOAT,   MOB_ID_MOUNTAIN_GOAT, MOB_ID_CHICKEN};
    for (uint32_t a = 0u; a < 6u; a++) {
        ap_clear(&rig);
        const bool small = ANIMALS[a] == MOB_ID_CHICKEN;
        for (uint32_t i = 0u; i < AP_HERD; i++) {
            const uint8_t sex = (i & 1u) == 0u ? ENTITY_STORE_SEX_MALE : ENTITY_STORE_SEX_FEMALE;
            const float64_t spacing = small ? 1.0f64 : 1.9f64;
            (void)ap_put(t, &rig, ANIMALS[a], sex, small ? 3.5f64 : 6.0f64, spacing * (1.5f64 - (float64_t)i));
        }
        REQUIRE(ap_ticks(script, 2u));
        CHECK_EQ_U64(store->count, AP_HERD);
        char name[64];
        (void)snprintf(name, sizeof(name), "herd_%s.png", mob_defs[ANIMALS[a]].name);
        REQUIRE(ap_screenshot(t, script, name));
    }

    /* A sheep shorn: shears on the ewe in front bring out 1 to 3 wool and wear the shears, and she shows her skin beside a ram that
     * still has his. */
    ap_clear(&rig);
    game->camera.pitch = -0.35f32;
    const entity_handle_t ewe = ap_put(t, &rig, MOB_ID_SHEEP, ENTITY_STORE_SEX_FEMALE, 2.2f64, 0.0f64);
    (void)ap_put(t, &rig, MOB_ID_SHEEP, ENTITY_STORE_SEX_MALE, 4.0f64, 2.2f64);
    ap_hold(&rig, ITEM_ID_SHEARS, 1u);
    REQUIRE(ap_ticks(script, 2u));
    REQUIRE(ap_use(script));
    const uint32_t wool =
        ap_lying(&rig, ITEM_ID_WHITE_WOOL) + inventory_count(&rig.item->inventory, ITEM_ID_WHITE_WOOL);
    printf("  the ewe gave %u wool\n", wool);
    CHECK(wool >= 1u && wool <= 3u);
    CHECK_EQ_U64(inventory_selected(&rig.item->inventory)->wear, 1u);
    CHECK(!mob_passive_fleece_shown(&rig.mob->passive, store, ap_index(t, &rig, ewe)));
    /* Again at once: nothing more, and the shears are not worn for it. */
    REQUIRE(ap_use(script));
    CHECK_EQ_U64(ap_lying(&rig, ITEM_ID_WHITE_WOOL) + inventory_count(&rig.item->inventory, ITEM_ID_WHITE_WOOL), wool);
    CHECK_EQ_U64(inventory_selected(&rig.item->inventory)->wear, 1u);
    REQUIRE(ap_screenshot(t, script, "sheep_shorn.png"));

    /* A cow milked: the bucket in hand comes back full, and drinking it does not start while the button is still down. A bull gives
     * nothing. */
    ap_clear(&rig);
    const entity_handle_t bull = ap_put(t, &rig, MOB_ID_CATTLE, ENTITY_STORE_SEX_MALE, 2.4f64, 0.0f64);
    ap_hold(&rig, ITEM_ID_BUCKET, 1u);
    REQUIRE(ap_ticks(script, 2u));
    REQUIRE(ap_use(script));
    CHECK_EQ_U64(inventory_selected(&rig.item->inventory)->item, ITEM_ID_BUCKET);
    REQUIRE(entity_store_remove(store, bull));
    (void)ap_put(t, &rig, MOB_ID_CATTLE, ENTITY_STORE_SEX_FEMALE, 2.4f64, 0.0f64);
    REQUIRE(ap_ticks(script, 2u));
    ap_button(script, BTN_RIGHT, true);
    REQUIRE(ap_ticks(script, 10u));
    CHECK_EQ_U64(inventory_selected(&rig.item->inventory)->item, ITEM_ID_MILK_BUCKET);
    CHECK_EQ_U64(rig.player->survival.eaten_count, 0u);
    ap_button(script, BTN_RIGHT, false);
    REQUIRE(ap_ticks(script, 2u));
    REQUIRE(ap_screenshot(t, script, "cow_milked.png"));

    /* A bed of the wool: three wool over three planks at a woodworking bench (plan 005 phase 6), put down and slept in at night. */
    ap_clear(&rig);
    inventory_t *const inventory = &rig.item->inventory;
    crafting_grid_t *const table = rig.item->borrowed_crafting_bench;
    table->station = RECIPE_STATION_WOODWORKING;
    for (uint32_t c = 0u; c < 3u; c++) {
        *crafting_grid_cell(table, c) = item_stack_make(ITEM_ID_WHITE_WOOL, 1u);
        *crafting_grid_cell(table, 3u + c) = item_stack_make(ITEM_ID_OAK_PLANKS, 1u);
    }
    crafting_grid_changed(table);
    const uint32_t beds_before = inventory_count(inventory, ITEM_ID_BED);
    CHECK_EQ_U64(crafting_grid_take_all(table, inventory), 1u);
    CHECK_EQ_U64(inventory_count(inventory, ITEM_ID_BED), beds_before + 1u);
    /* Wheat no longer makes one. */
    for (uint32_t c = 0u; c < 3u; c++) {
        *crafting_grid_cell(table, c) = item_stack_make(ITEM_ID_WHEAT, 1u);
        *crafting_grid_cell(table, 3u + c) = item_stack_make(ITEM_ID_OAK_PLANKS, 1u);
    }
    crafting_grid_changed(table);
    CHECK_EQ_U64(crafting_grid_take_all(table, inventory), 0u);
    (void)crafting_grid_give_back(table, inventory);
    ap_hold(&rig, ITEM_ID_BED, 1u);
    game->camera.pitch = -0.8f32;
    REQUIRE(ap_ticks(script, 3u));
    REQUIRE(rig.player->target.hit.result == RAYCAST_RESULT_HIT);
    REQUIRE(ap_use(script));
    REQUIRE(ap_ticks(script, 3u));
    REQUIRE(rig.player->target.hit.block == BLOCK_ID_BED);
    REQUIRE(ap_screenshot(t, script, "bed_of_wool.png"));
    game_clock_set(game, AP_DAY, 0.85f64);
    REQUIRE(ap_ticks(script, 2u));
    REQUIRE(ap_use(script));
    CHECK_EQ_U64(rig.player->survival.bed_answer, PLAYER_SURVIVAL_BED_ASLEEP);
    CHECK(rig.player->survival.state == PLAYER_SURVIVAL_SLEEPING);
    REQUIRE(ap_screenshot(t, script, "bed_slept_in.png"));
    game_close(game);
    ap_end(t, &rig);
}

TEST_MAIN()
