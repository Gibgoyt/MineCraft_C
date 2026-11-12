/* ctest render_feature_bridge. See README.md. */
#include "harness/index.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "log/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    BR_WIDTH = 640u,
    BR_HEIGHT = 360u,
    /* Frames drawn for each picture, so that TAA has settled. */
    BR_FRAMES = 12u,
    /* Planks in hand: the bridge's 20 and some over, so that the preview still shows at the end. */
    BR_BRIDGE = 20u,
    BR_HELD = 24u,
};

enum : int32_t {
    /* The bridge's level: the platform's top block and every block of the bridge are at this z, in water this deep below it. */
    BR_LEVEL = -1,
    BR_WATER_FLOOR = -6,
    BR_PLATFORM_WEST = -4,
    BR_WATER_WEST = -12,
    BR_WATER_EAST = 40,
    BR_WATER_HALF = 14,
};

#define BR_FOG_END 400.0f32
/* A person bridging looks a little down, so that the aim goes past the bridge's end into the water. */
#define BR_PITCH (-0.5f32)

#include "cuda/sky/fixtures/rig.h"

static bool br_put(test_context_t *t, sk_rig_t *rig, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1,
                   int32_t z1, uint16_t voxel) {
    for (int32_t z = z0; z <= z1; z++) {
        for (int32_t y = y0; y <= y1; y++) {
            for (int32_t x = x0; x <= x1; x++) {
                const world_edit_status_t status = world_edit_block_set(&rig->world->edit, x, y, z, voxel);
                if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* A lake on a stone floor, and a stone platform one block wide out into it from the west, its top at the water's. */
static bool br_begin(test_context_t *t, sk_rig_t *rig) {
    if (!sk_rig_begin(t, rig) || !sk_hand_world_open(t, rig, 3u, BR_WIDTH, BR_HEIGHT, BR_FOG_END) ||
        !br_put(t, rig, BR_WATER_WEST, -BR_WATER_HALF, BR_WATER_FLOOR - 1, BR_WATER_EAST, BR_WATER_HALF,
                BR_WATER_FLOOR - 1, BLOCK_ID_STONE) ||
        !br_put(t, rig, BR_WATER_WEST, -BR_WATER_HALF, BR_WATER_FLOOR, BR_WATER_EAST, BR_WATER_HALF, BR_LEVEL,
                BLOCK_ID_WATER) ||
        !br_put(t, rig, BR_PLATFORM_WEST, 0, BR_WATER_FLOOR, 0, 0, BR_LEVEL, BLOCK_ID_STONE) ||
        !sk_hand_world_sync(t, rig)) {
        return false;
    }
    render_frame_features_set(&rig->render->frame, SHADE_FEATURES_PLAY);
    render_frame_time_set(&rig->render->frame, 0.4f32);
    return true;
}

typedef struct br_walker_t {
    player_t *player;   /* 0 */
    physics_t *physics; /* 8 */
    int64_t now_ns;     /* 16 */
} br_walker_t;
static_assert(sizeof(br_walker_t) == 24);

/* One tick of a person at the controls, as the game runs it: the hands, then the body and its aim. */
static void br_tick(sk_rig_t *rig, br_walker_t *walker, int8_t forward, bool place) {
    const player_intent_t intent = {.yaw = 0.0f32, .forward = forward, ._pad0 = {0}};
    walker->now_ns += PLAYER_TICK_NS;
    player_send(walker->player, &intent, walker->now_ns);
    player_look_send(walker->player, 0.0f32, BR_PITCH);
    player_hands_send(walker->player, false, place);
    player_interact_tick(walker->player, &rig->world->edit, walker->now_ns);
    player_tick(walker->player, &rig->world->map, walker->now_ns);
}

/* What the game would send the card for this tick: the preview the next click would place, as game_preview_publish does. */
static player_place_status_t br_preview_send(sk_rig_t *rig, const br_walker_t *walker, bool show) {
    render_entities_t *const entities = rig->render->borrowed_entities;
    render_entities_begin(entities);
    player_place_plan_t plan;
    const player_place_status_t status = player_place_preview(walker->player, &rig->world->map, &plan);
    if (show && status != PLAYER_PLACE_NONE) {
        render_entities_preview_t preview = {.count = plan.count,
                                             .blocked = plan.status == PLAYER_PLACE_BLOCKED ? 1u : 0u};
        for (uint32_t c = 0u; c < plan.count && c < PLAYER_PLACE_CELLS; c++) {
            memcpy(preview.cells[c], plan.cells[c], sizeof(preview.cells[c]));
            preview.voxels[c] = plan.voxels[c];
        }
        render_entities_preview_set(entities, &preview);
    }
    render_entities_publish(entities);
    return status;
}

static uint8_t *br_picture(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera) {
    const uint64_t bytes = post_xrgb_bytes(BR_WIDTH, BR_HEIGHT);
    uint8_t *const copy = test_alloc(t, bytes);
    const uint8_t *picture = nullptr;
    for (uint32_t i = 0u; i < BR_FRAMES; i++) {
        picture = sk_draw(t, rig, camera);
    }
    if (picture != nullptr) {
        memcpy(copy, picture, bytes);
    }
    return copy;
}

/* Into MC_BRIDGE_SHOTS, if it names a directory, for a person to look at. */
static void br_shot(test_context_t *t, const uint8_t *picture, const char *name) {
    const char *const directory = getenv("MC_BRIDGE_SHOTS");
    if (directory == nullptr) {
        return;
    }
    const uint64_t capacity = display_png_bytes_needed(BR_WIDTH, BR_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    uint64_t written = 0u;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    REQUIRE(display_png_encode(picture, BR_WIDTH * 4u, BR_WIDTH, BR_HEIGHT, file, capacity, &written));
    REQUIRE(file_write_atomic(path, file, written) == FILE_OK);
    printf("  wrote %s\n", path);
}

/* The eye, as the player sees: where the body's eyes are, looking where it looks. */
static void br_eye(const br_walker_t *walker, render_camera_t *out) {
    const player_movement_t *const body = &walker->player->movement;
    render_camera_start(out, body->position[0], body->position[1], body->position[2] + body->eye_height, 0.0f32,
                        BR_PITCH);
}

/* How many pixels the preview turned blue: the picture with it against the same without. */
static uint64_t br_blue_pixels(const uint8_t *with, const uint8_t *without) {
    uint64_t blue = 0u;
    for (uint32_t y = 0u; y < BR_HEIGHT; y++) {
        for (uint32_t x = 0u; x < BR_WIDTH; x++) {
            const uint32_t a = sk_pixel(with, BR_WIDTH, x, y), b = sk_pixel(without, BR_WIDTH, x, y);
            for (uint32_t face = 0u; face < 6u && a != b; face++) {
                if (entity_rays_preview_blend(b, ENTITY_RAYS_PREVIEW_OK_XRGB, face) == a) {
                    blue++;
                    break;
                }
            }
        }
    }
    return blue;
}

/* The shots along the way, first person, with the preview and without, and how blue it is. */
static uint64_t br_shots(test_context_t *t, sk_rig_t *rig, const br_walker_t *walker, const char *stem) {
    if (!CHECK(sk_hand_world_sync(t, rig))) {
        return 0u;
    }
    render_camera_t eye;
    br_eye(walker, &eye);
    char name[64];
    (void)br_preview_send(rig, walker, false);
    uint8_t *const bare = br_picture(t, rig, &eye);
    CHECK_EQ_U64(br_preview_send(rig, walker, true), PLAYER_PLACE_OK);
    uint8_t *const shown = br_picture(t, rig, &eye);
    snprintf(name, sizeof(name), "%s.png", stem);
    br_shot(t, shown, name);
    snprintf(name, sizeof(name), "%s_bare.png", stem);
    br_shot(t, bare, name);
    const uint64_t blue = br_blue_pixels(shown, bare);
    printf("  %s: feet at %.2f %.2f %.2f, %" PRIu64 " pixels blue\n", stem,
           (libc_double_t)walker->player->movement.position[0], (libc_double_t)walker->player->movement.position[1],
           (libc_double_t)walker->player->movement.position[2], blue);
    return blue;
}

/* Walks east off the platform's end looking ahead, with the right button held, and stops when 20 are down. Shots after 5, 10 and 20,
 * and one from above and to the south of the whole bridge. */
TEST(a_bridge_of_twenty_goes_out_over_water_placing_ahead_with_the_preview_blue_first) {
    sk_rig_t rig;
    REQUIRE(br_begin(t, &rig));
    br_walker_t walker = {.physics = sk_aligned(t, sizeof(physics_t)), .now_ns = 1000000000};
    REQUIRE(physics_init(walker.physics));
    walker.player = sk_aligned(t, sizeof(player_t));
    player_init(walker.player, walker.physics);
    player_spawn(walker.player, 0.3f64, 0.5f64, (float64_t)(BR_LEVEL + 1));
    player_interaction_hand_set(&walker.player->interaction, BLOCK_ID_OAK_PLANKS, BR_HELD);
    /* A tick to stand and aim. */
    br_tick(&rig, &walker, 0, false);
    br_tick(&rig, &walker, 0, false);
    (void)br_shots(t, &rig, &walker, "bridge_00");
    float64_t lowest = 1.0e9f64;
    uint32_t placed = 0u, shot = 0u;
    for (uint32_t tick = 0u; tick < 1000u && placed < BR_BRIDGE; tick++) {
        player_place_plan_t plan;
        const player_place_status_t status = player_place_preview(walker.player, &rig.world->map, &plan);
        /* After 5 and 10, the first tick the next one shows. */
        if (status == PLAYER_PLACE_OK && (placed == 5u || placed == 10u) && shot != placed) {
            char stem[32];
            snprintf(stem, sizeof(stem), "bridge_%02u", placed);
            (void)br_shots(t, &rig, &walker, stem);
            shot = placed;
        }
        const uint32_t before = walker.player->interaction.hand_count;
        br_tick(&rig, &walker, 1, true);
        lowest = walker.player->movement.position[2] < lowest ? walker.player->movement.position[2] : lowest;
        if (walker.player->interaction.hand_count == before) {
            continue;
        }
        placed++;
        /* Shown blue the tick before, where it went. */
        CHECK_EQ_U64(status, PLAYER_PLACE_OK);
        CHECK_EQ_I64(plan.cells[0][0], (int64_t)placed);
        CHECK_EQ_I64(plan.cells[0][2], BR_LEVEL);
    }
    /* On to the last block, without placing, so that the cell ahead is the water past it; then let go and stand. */
    for (uint32_t i = 0u; i < 100u && walker.player->movement.position[0] < (float64_t)BR_BRIDGE + 0.2f64; i++) {
        br_tick(&rig, &walker, 1, false);
        lowest = walker.player->movement.position[2] < lowest ? walker.player->movement.position[2] : lowest;
    }
    for (uint32_t i = 0u; i < 10u; i++) {
        br_tick(&rig, &walker, 0, false);
        lowest = walker.player->movement.position[2] < lowest ? walker.player->movement.position[2] : lowest;
    }
    CHECK_EQ_U64(placed, BR_BRIDGE);
    CHECK(lowest > (float64_t)(BR_LEVEL + 1) - 1.0e-6f64);
    for (int32_t x = 1; x <= (int32_t)BR_BRIDGE; x++) {
        uint16_t block = BLOCK_ID_AIR;
        (void)world_map_block_get(&rig.world->map, x, 0, BR_LEVEL, &block);
        if (!CHECK_EQ_U64(block, BLOCK_ID_OAK_PLANKS)) {
            printf("  gap at x = %d\n", x);
        }
    }
    const uint64_t blue = br_shots(t, &rig, &walker, "bridge_20");
    CHECK(blue > 200u);
    /* The whole of it from above and to the south, the body standing on its end and the next cell blue. */
    render_camera_t side;
    render_camera_start(&side, 8.0f64, -14.0f64, 9.0f64, 1.5708f32, -0.5f32);
    (void)br_preview_send(&rig, &walker, true);
    const uint8_t *const whole = br_picture(t, &rig, &side);
    br_shot(t, whole, "bridge_side.png");
    /* The rig's, and not needed here: a picture to compare is for a person. */
    (void)sk_bytes_near;
    player_deinit(walker.player);
    physics_deinit(walker.physics);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* Plan 008 phase 01: the card's clock speed, from the one-warp clock64() kernel, is one the card could have: above what any card idles at under
 * load, and not past its stated peak. Capped, it reads below the peak: on Ahmed's card at 50 W, about 600 of 2100 MHz. */
TEST(the_clock_kernel_reads_a_plausible_clock) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    frame_marks_t marks;
    gpu_error_t error = {0};
    REQUIRE(frame_marks_init(&marks, &error) == GPU_STATUS_OK);
    float32_t rated = 0.0f32;
    REQUIRE(frame_gpu_clock_rated(&rated, &error) == GPU_STATUS_OK);
    float32_t most = 0.0f32;
    /* Enough of them, back to back, that an idle card has come up to speed: half a second or so. */
    for (uint32_t i = 0u; i < 600u; i++) {
        REQUIRE(frame_gpu_clock_mhz(&marks, &error) == GPU_STATUS_OK);
        most = marks.card_mhz > most ? marks.card_mhz : most;
    }
    printf("  the clock kernel read %.0f MHz at most of 600, the last over %.3f ms for %" PRIu64
           " cycles; the card's peak is %.0f MHz\n",
           (libc_double_t)most, (libc_double_t)marks.clock_ms, marks.clock_cycles, (libc_double_t)rated);
    CHECK(rated > 0.0f32);
    CHECK(marks.clock_cycles >= (uint64_t)FRAME_CLOCK_CYCLES);
    CHECK(most >= 300.0f32);
    CHECK(most <= rated * 1.1f32);
    CHECK(frame_marks_deinit(&marks, &error) == GPU_STATUS_OK);
    sk_rig_end(t, &rig);
}

TEST_MAIN()
