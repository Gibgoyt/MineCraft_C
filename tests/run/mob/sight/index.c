/* ctest run_mob_sight. See README.md. */
#include "harness/index.h"

#include "display/headless/png/index.h"
#include "display/index.h"
#include "entity/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "item/index.h"
#include "job/index.h"
#include "mob/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "tables/mobs/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "world/index.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    SIGHT_WIDTH = 640u,
    SIGHT_HEIGHT = 360u,
    /* The floor this far round the body, so that what stands behind the mobs is sky and floor and not the land past it, whose
     * grass and leaves (a jungle since plan 006 phase 03) are the very colours the test counts. */
    SIGHT_PAD_REACH = 40u,
    /* Air this high over the floor: the land round the spawn may be a forest (plan 006 phase 03), whose canopy would shade
     * the mobs and dull their colours. */
    SIGHT_PAD_ROOM = 48u,
    SIGHT_SETTLE_TICKS = 10u,
    SIGHT_STEPPED_TICKS = 2u,
    SIGHT_MOBS = 3u,
    SIGHT_PATH_BYTES = 512u,
};

static void *sight_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

typedef struct sight_rig_t {
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
} sight_rig_t;
static_assert(sizeof(sight_rig_t) == 104);

static bool sight_begin(test_context_t *t, sight_rig_t *rig) {
    rig->bridge = sight_aligned(t, sizeof(gpu_bridge_t));
    rig->job = sight_aligned(t, sizeof(job_system_t));
    rig->world = sight_aligned(t, sizeof(world_t));
    rig->terrain = sight_aligned(t, sizeof(terrain_t));
    rig->render = sight_aligned(t, sizeof(render_t));
    rig->texture = sight_aligned(t, sizeof(texture_t));
    rig->game = sight_aligned(t, sizeof(game_t));
    rig->physics = sight_aligned(t, sizeof(physics_t));
    rig->player = sight_aligned(t, sizeof(player_t));
    rig->entity = sight_aligned(t, sizeof(entity_t));
    rig->item = sight_aligned(t, sizeof(item_t));
    rig->mob = sight_aligned(t, sizeof(mob_t));
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

static void sight_end(test_context_t *t, sight_rig_t *rig) {
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

static bool sight_ticks(display_headless_scene_t *scene, const display_script_state_t *state, uint32_t *tick,
                        uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; i++) {
        (*tick)++;
        const display_headless_tick_t now = {.events = nullptr, .state = state, .event_count = 0u, .tick = *tick};
        if (!scene->tick(scene->borrowed_context, &now)) {
            return false;
        }
    }
    return true;
}

static bool sight_draw(display_headless_scene_t *scene, const display_script_state_t *state, uint32_t tick,
                       display_buffer_t *target) {
    const display_headless_tick_t now = {.events = nullptr, .state = state, .event_count = 0u, .tick = tick};
    return scene->draw(scene->borrowed_context, &now, target);
}

/* What this picture holds of a mob, within one column of pixels' worth of the screen: pixels changed from the empty picture, and of those how
 * many are green, blue or grey. */
typedef struct sight_count_t {
    uint32_t changed; /* 0 */
    uint32_t green;   /* 4 */
    uint32_t blue;    /* 8 */
    uint32_t grey;    /* 12 */
} sight_count_t;
static_assert(sizeof(sight_count_t) == 16);

static bool sight_is_grey(uint32_t pixel) {
    const uint32_t red = (pixel >> 16u) & 0xFFu, green = (pixel >> 8u) & 0xFFu, blue = pixel & 0xFFu;
    const uint32_t most = red > green ? (red > blue ? red : blue) : (green > blue ? green : blue);
    const uint32_t least = red < green ? (red < blue ? red : blue) : (green < blue ? green : blue);
    return most - least < 24u;
}

static sight_count_t sight_count(const uint32_t *before, const uint32_t *after, uint32_t from_x, uint32_t to_x) {
    sight_count_t count = {0u, 0u, 0u, 0u};
    for (uint32_t y = 0u; y < SIGHT_HEIGHT; y++) {
        for (uint32_t x = from_x; x < to_x; x++) {
            const uint32_t pixel = after[(uint64_t)y * SIGHT_WIDTH + x];
            if ((pixel & 0xFFFFFFu) == (before[(uint64_t)y * SIGHT_WIDTH + x] & 0xFFFFFFu)) {
                continue;
            }
            const uint32_t red = (pixel >> 16u) & 0xFFu, green = (pixel >> 8u) & 0xFFu, blue = pixel & 0xFFu;
            count.changed++;
            count.green += green > red + 30u && green > blue + 30u ? 1u : 0u;
            count.blue += red < 70u && blue > red + 25u ? 1u : 0u; /* a shirt or trousers, and not the sky */
            /* Bone against the sky: the stone it stands on is as grey. */
            const uint32_t behind = before[(uint64_t)y * SIGHT_WIDTH + x];
            count.grey += sight_is_grey(pixel) && !sight_is_grey(behind) ? 1u : 0u;
        }
    }
    return count;
}

/* With MC_SIGHT_DIR set, the picture as a file, for a person to look at. */
static void sight_save(test_context_t *t, const display_buffer_t *target, const char *name) {
    const char *const directory = getenv("MC_SIGHT_DIR");
    if (directory == nullptr) {
        return;
    }
    const uint64_t capacity = display_png_bytes_needed(SIGHT_WIDTH, SIGHT_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    uint64_t bytes = 0u;
    REQUIRE(display_png_encode(target->owned_map, target->pitch, SIGHT_WIDTH, SIGHT_HEIGHT, file, capacity, &bytes));
    char path[SIGHT_PATH_BYTES];
    (void)snprintf(path, sizeof(path), "%s/%s", directory, name);
    CHECK_EQ_U64(file_write_atomic(path, file, bytes), FILE_OK);
}

/* All of it through run/src/game/: what spawning makes goes through the snapshot of what is drawn, and comes out on the card's picture, each in
 * its own skin. Every other test of these three looks at their numbers, or at boxes it made itself. */
TEST(in_a_real_game_each_mob_is_on_the_picture_in_its_own_skin) {
    sight_rig_t rig;
    REQUIRE(sight_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = 5u, .width = SIGHT_WIDTH, .height = SIGHT_HEIGHT, .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    display_script_state_t *const state = test_alloc(t, sizeof(*state));
    memset(state, 0, sizeof(*state));
    uint32_t tick = 0u;

    /* A floor of its own, with the sky over it: sand, which is none of the colours counted (green, blue, bone grey), as a floor
     * this wide is most of what stands behind the mobs. */
    const int32_t reach = (int32_t)SIGHT_PAD_REACH;
    const int32_t pad_x = (int32_t)floor(rig.player->movement.position[0]);
    const int32_t pad_y = (int32_t)floor(rig.player->movement.position[1]);
    const int32_t pad_z = (int32_t)floor(rig.player->movement.position[2]);
    for (int32_t x = pad_x - reach; x <= pad_x + reach; x++) {
        for (int32_t y = pad_y - reach; y <= pad_y + reach; y++) {
            for (int32_t z = pad_z - 1; z <= pad_z + (int32_t)SIGHT_PAD_ROOM; z++) {
                const world_edit_status_t status =
                    world_edit_block_set(&rig.world->edit, x, y, z, z < pad_z ? BLOCK_ID_SAND : BLOCK_ID_AIR);
                REQUIRE(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED);
            }
        }
    }
    REQUIRE(sight_ticks(&scene, state, &tick, SIGHT_SETTLE_TICKS));

    const uint64_t screen_bytes = (uint64_t)SIGHT_WIDTH * SIGHT_HEIGHT * 4u;
    display_buffer_t before = {.owned_map = sight_aligned(t, screen_bytes),
                               .map_bytes = screen_bytes,
                               .pitch = SIGHT_WIDTH * 4u,
                               .width = SIGHT_WIDTH,
                               .height = SIGHT_HEIGHT};
    display_buffer_t after = before;
    after.owned_map = sight_aligned(t, screen_bytes);
    REQUIRE(sight_draw(&scene, state, tick, &before));
    sight_save(t, &before, "empty.png");

    /* Seven blocks ahead of where it looks, standing on the floor, from left to right on the screen. */
    const mob_id_t mobs[SIGHT_MOBS] = {MOB_ID_ZOMBIE, MOB_ID_SKELETON, MOB_ID_CREEPER};
    const float64_t *const feet = rig.player->movement.position;
    float64_t look[COLLISION_AXES];
    player_camera_direction(game->camera.yaw, 0.0f32, look);
    for (uint32_t i = 0u; i < SIGHT_MOBS; i++) {
        const mob_def_t *const def = &mob_defs[mobs[i]];
        const float64_t across = 3.0f64 - 3.0f64 * (float64_t)i;
        const entity_store_spawn_t body = {
            .position = {feet[0] + 7.0f64 * look[0] - across * look[1], feet[1] + 7.0f64 * look[1] + across * look[0],
                         (float64_t)pad_z},
            .half_width = def->half_width,
            .height = def->height,
            .health = def->health,
            .kind = (uint16_t)mobs[i],
        };
        REQUIRE(entity_store_spawn(&rig.entity->store, &body) != ENTITY_HANDLE_NONE);
    }
    REQUIRE(sight_ticks(&scene, state, &tick, SIGHT_STEPPED_TICKS));
    REQUIRE(sight_draw(&scene, state, tick, &after));
    sight_save(t, &after, "mobs.png");

    const uint32_t *const shown_before = (const uint32_t *)(const void *)before.owned_map;
    const uint32_t *const shown_after = (const uint32_t *)(const void *)after.owned_map;
    const uint32_t third = SIGHT_WIDTH / 3u;
    const sight_count_t zombie = sight_count(shown_before, shown_after, 0u, third);
    const sight_count_t skeleton = sight_count(shown_before, shown_after, third, 2u * third);
    const sight_count_t creeper = sight_count(shown_before, shown_after, 2u * third, SIGHT_WIDTH);
    printf("  changed: zombie %u (green %u, blue %u), skeleton %u (grey %u), creeper %u (green %u, blue %u)\n",
           zombie.changed, zombie.green, zombie.blue, skeleton.changed, skeleton.grey, creeper.changed, creeper.green,
           creeper.blue);
    /* A zombie is green above a blue shirt, a skeleton the colour of bone, and a creeper green with no shirt. The day and the picture's jitter
     * change most of what is behind them, so what counts is the colours. */
    /* Plan 007 phase 04 with plan 006 phase 04's trees: a tree's crown stands behind the zombie's head, green on green, so 88 of its
     * green pixels count as changed, against 100 before; its 705 of shirt say it is there. */
    CHECK(zombie.green > 50u);
    CHECK(zombie.blue > 100u);
    CHECK(skeleton.grey > 100u);
    CHECK(creeper.green > 300u);
    CHECK(creeper.blue < creeper.green / 4u);

    game_close(game);
    sight_end(t, &rig);
}

TEST_MAIN()
