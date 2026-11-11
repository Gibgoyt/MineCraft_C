/* ctest render_feature_preview. See README.md. */
#include "harness/index.h"

#include "png/index.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "log/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/bench/index.h"
#include "world/block_state/index.h"
#include "world/index.h"
#include "world/wall_torch/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum : uint32_t {
    PV_WIDTH = 320u,
    PV_HEIGHT = 180u,
    PV_PIXELS = PV_WIDTH * PV_HEIGHT,
    PV_GOLDEN_TOLERANCE = 2u,
    PV_GOLDEN_FRAMES = 40u,
    PV_COST_FRAMES = 30u,
    PV_COST_WIDTH = 1920u,
    PV_COST_HEIGHT = 1080u,
    PV_COST_RENDER_DISTANCE = 10u,
};

#define PV_FOG_END 400.0f32
/* The light is then each face's one brightness and nothing else, and each frame stands alone: a picture is the same bytes each time. */
#define PV_PLAIN                                                                                                       \
    (SHADE_FEATURES_ALL &                                                                                              \
     ~(uint32_t)(SHADE_FEATURE_SKY | SHADE_FEATURE_FOG | SHADE_FEATURE_SHADOW | SHADE_FEATURE_LIGHT))

#include "cuda/sky/fixtures/rig.h"

/* ---- A world, and what goes in it ------------------------------------------------------------------------------------------- */

static bool pv_put(test_context_t *t, sk_rig_t *rig, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1,
                   int32_t z1, uint16_t voxel) {
    for (int32_t z = z0; z < z1; z++) {
        for (int32_t y = y0; y < y1; y++) {
            for (int32_t x = x0; x < x1; x++) {
                const world_edit_status_t status = world_edit_block_set(&rig->world->edit, x, y, z, voxel);
                if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* A stone floor with its top at z = -1, and a wall across at x = 20. */
static bool pv_begin(test_context_t *t, sk_rig_t *rig, uint32_t features) {
    if (!sk_rig_begin(t, rig) || !sk_hand_world_open(t, rig, 3u, PV_WIDTH, PV_HEIGHT, PV_FOG_END) ||
        !pv_put(t, rig, -2, -16, -2, 24, 32, -1, BLOCK_ID_STONE) ||
        !pv_put(t, rig, 20, -16, -1, 21, 32, 10, BLOCK_ID_STONE) || !sk_hand_world_sync(t, rig)) {
        return false;
    }
    render_frame_features_set(&rig->render->frame, features);
    render_frame_time_set(&rig->render->frame, 0.31f32);
    return true;
}

static void pv_end(test_context_t *t, sk_rig_t *rig) {
    sk_hand_world_close(rig);
    sk_rig_end(t, rig);
}

/* A preview of up to four cells, as the sim sends one. */
typedef struct pv_layout_t {
    const char *name;    /* 0 */
    int32_t cells[4][3]; /* 8 */
    uint16_t voxels[4];  /* 56 */
    uint32_t count;      /* 64 */
    int32_t wall[3];     /* 68: a block the preview leans on, put there first; z of INT32_MIN for none */
    uint8_t blocked;     /* 80 */
    uint8_t _pad0[7];    /* 81 */
} pv_layout_t;
static_assert(sizeof(pv_layout_t) == 88);

static void pv_send(sk_rig_t *rig, const pv_layout_t *layout) {
    render_entities_t *const entities = rig->render->borrowed_entities;
    render_entities_begin(entities);
    if (layout != nullptr) {
        render_entities_preview_t preview = {.count = (uint8_t)layout->count, .blocked = layout->blocked};
        memcpy(preview.cells, layout->cells, sizeof(preview.cells));
        memcpy(preview.voxels, layout->voxels, sizeof(preview.voxels));
        render_entities_preview_set(entities, &preview);
    }
    render_entities_publish(entities);
}

static uint8_t *pv_picture(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera, uint32_t frames) {
    const uint64_t bytes = post_xrgb_bytes(PV_WIDTH, PV_HEIGHT);
    uint8_t *const copy = test_alloc(t, bytes);
    const uint8_t *picture = nullptr;
    for (uint32_t i = 0u; i < frames; i++) {
        picture = sk_draw(t, rig, camera);
    }
    if (picture != nullptr) {
        memcpy(copy, picture, bytes);
    }
    return copy;
}

/* The bench lying across a view to the north: its west half where aimed, and its other half where bench_half_other says. */
static pv_layout_t pv_bench(int32_t x, int32_t y, int32_t z) {
    pv_layout_t layout = {.name = "bench", .count = 2u, .wall = {0, 0, INT32_MIN}};
    const uint16_t west = BLOCK_ID_WOODWORKING_BENCH_WEST;
    int32_t step_x = 0, step_y = 0;
    uint16_t other = BLOCK_ID_AIR;
    (void)bench_half_other(west, &step_x, &step_y, &other);
    layout.cells[0][0] = x;
    layout.cells[0][1] = y;
    layout.cells[0][2] = z;
    layout.cells[1][0] = x + step_x;
    layout.cells[1][1] = y + step_y;
    layout.cells[1][2] = z;
    layout.voxels[0] = west;
    layout.voxels[1] = other;
    return layout;
}

/* The four of the goldens, and stairs, in the middle of the picture from PV_EYE. */
static pv_layout_t pv_layout(uint32_t which) {
    pv_layout_t layout = {.count = 1u, .cells = {{12, 6, -1}}, .wall = {0, 0, INT32_MIN}};
    switch (which) {
    case 0u:
        layout.name = "cube";
        layout.voxels[0] =
            BLOCK_ID_OAK_PLANKS; /* not stone, which the floor is, so that the block placed shows everywhere */
        break;
    case 1u:
        /* Hanging from the west face of a wall the eye looks at: leaning out to -x. */
        layout.name = "wall_torch";
        layout.cells[0][0] = 11;
        layout.cells[0][2] = 0;
        layout.voxels[0] = wall_torch_of(-1, 0);
        layout.wall[0] = 12;
        layout.wall[1] = 6;
        layout.wall[2] = 0;
        break;
    case 2u:
        layout.name = "slab";
        layout.voxels[0] = BLOCK_ID_OAK_SLAB;
        break;
    case 3u:
        layout = pv_bench(12, 6, -1);
        break;
    default:
        layout.name = "stairs";
        layout.voxels[0] = block_state_placed(BLOCK_ID_OAK_STAIRS, BLOCK_FACING_EAST);
        break;
    }
    return layout;
}

enum : uint32_t { PV_LAYOUTS = 5u };

static void pv_eye(render_camera_t *eye) {
    render_camera_start(eye, 9.2f64, 3.4f64, 1.4f64, 0.62f32, -0.5f32);
}

/* What leans on the wall, if anything: the wall to the floor, so it stands. */
static bool pv_wall(test_context_t *t, sk_rig_t *rig, const pv_layout_t *layout, uint16_t voxel) {
    if (layout->wall[2] == INT32_MIN) {
        return true;
    }
    return pv_put(t, rig, layout->wall[0], layout->wall[1], -1, layout->wall[0] + 1, layout->wall[1] + 1,
                  layout->wall[2] + 1, voxel);
}

/* ---- The picture ---------------------------------------------------------------------------------------------------------- */

/* How many pixels differ, and whether every one of them is the picture without it under the preview's colour, on one of the six faces. */
static uint64_t pv_tinted(test_context_t *t, const uint8_t *with, const uint8_t *without, uint32_t xrgb) {
    uint64_t changed = 0u, wrong = 0u;
    for (uint32_t y = 0u; y < PV_HEIGHT; y++) {
        for (uint32_t x = 0u; x < PV_WIDTH; x++) {
            const uint32_t a = sk_pixel(with, PV_WIDTH, x, y), b = sk_pixel(without, PV_WIDTH, x, y);
            if (a == b) {
                continue;
            }
            changed++;
            bool any = false;
            for (uint32_t face = 0u; face < 6u; face++) {
                any |= entity_rays_preview_blend(b, xrgb, face) == a;
            }
            wrong += any ? 0u : 1u;
        }
    }
    CHECK_EQ_U64(wrong, 0u);
    return changed;
}

TEST(the_blend_is_half_the_colour_lit_by_the_face_over_the_picture) {
    /* Black under blue, on the top: half of each channel. */
    CHECK_EQ_U64(entity_rays_preview_blend(0x000000u, 0x3C8CFFu, TRAVERSE_FACE_POS_Z), 0x1E467Fu);
    /* White under it: half of white and half of the colour. */
    CHECK_EQ_U64(entity_rays_preview_blend(0xFFFFFFu, 0x3C8CFFu, TRAVERSE_FACE_POS_Z), 0x9DC5FFu);
    /* The bottom is the darkest face and the top the brightest, so a shape shows. */
    CHECK(entity_rays_preview_face_light(TRAVERSE_FACE_NEG_Z) < entity_rays_preview_face_light(TRAVERSE_FACE_NEG_Y));
    CHECK(entity_rays_preview_face_light(TRAVERSE_FACE_NEG_Y) < entity_rays_preview_face_light(TRAVERSE_FACE_NEG_X));
    CHECK(entity_rays_preview_face_light(TRAVERSE_FACE_NEG_X) < entity_rays_preview_face_light(TRAVERSE_FACE_POS_Z));
    CHECK_EQ_U64(entity_rays_preview_face_light(TRAVERSE_FACE_NEG_X),
                 entity_rays_preview_face_light(TRAVERSE_FACE_POS_X));
    /* Red is red: the blocked colour leaves the most red. */
    const uint32_t red = entity_rays_preview_blend(0x808080u, ENTITY_RAYS_PREVIEW_BLOCKED_XRGB, TRAVERSE_FACE_POS_Z);
    const uint32_t blue = entity_rays_preview_blend(0x808080u, ENTITY_RAYS_PREVIEW_OK_XRGB, TRAVERSE_FACE_POS_Z);
    CHECK((red >> 16) > (red & 0xFFu));
    CHECK((blue & 0xFFu) > (blue >> 16));
}

TEST(a_preview_tints_what_it_covers_blue_or_red_and_nothing_else_and_the_setting_hides_it) {
    sk_rig_t rig;
    REQUIRE(pv_begin(t, &rig, PV_PLAIN));
    render_camera_t eye;
    pv_eye(&eye);
    render_entities_t *const entities = rig.render->borrowed_entities;
    for (uint32_t which = 0u; which < PV_LAYOUTS; which++) {
        pv_layout_t layout = pv_layout(which);
        REQUIRE(pv_wall(t, &rig, &layout, BLOCK_ID_OAK_PLANKS));
        REQUIRE(sk_hand_world_sync(t, &rig));
        pv_send(&rig, nullptr);
        uint8_t *const bare = pv_picture(t, &rig, &eye, 1u);
        pv_send(&rig, &layout);
        uint8_t *const blue = pv_picture(t, &rig, &eye, 1u);
        const uint64_t blue_pixels = pv_tinted(t, blue, bare, ENTITY_RAYS_PREVIEW_OK_XRGB);
        layout.blocked = 1u;
        pv_send(&rig, &layout);
        uint8_t *const red = pv_picture(t, &rig, &eye, 1u);
        const uint64_t red_pixels = pv_tinted(t, red, bare, ENTITY_RAYS_PREVIEW_BLOCKED_XRGB);
        printf("  %s: %" PRIu64 " pixels blue, %" PRIu64 " red\n", layout.name, blue_pixels, red_pixels);
        CHECK(blue_pixels > 20u);
        CHECK(blue_pixels < PV_PIXELS / 4u);
        CHECK_EQ_U64(red_pixels, blue_pixels);
        /* The setting off: the same bytes as with nothing to show, and back on, shown again. */
        render_entities_preview_show(entities, false);
        uint8_t *const hidden = pv_picture(t, &rig, &eye, 1u);
        CHECK_MEM_EQ(hidden, bare, post_xrgb_bytes(PV_WIDTH, PV_HEIGHT));
        render_entities_preview_show(entities, true);
        uint8_t *const again = pv_picture(t, &rig, &eye, 1u);
        CHECK_MEM_EQ(again, red, post_xrgb_bytes(PV_WIDTH, PV_HEIGHT));
        REQUIRE(pv_wall(t, &rig, &layout, BLOCK_ID_AIR));
    }
    pv_end(t, &rig);
}

/* The pixels where two pictures differ from a third. */
static uint64_t pv_silhouette_gap(const uint8_t *a, const uint8_t *b, const uint8_t *bare, uint64_t *out_a) {
    uint64_t gap = 0u, in_a = 0u;
    for (uint32_t y = 0u; y < PV_HEIGHT; y++) {
        for (uint32_t x = 0u; x < PV_WIDTH; x++) {
            const uint32_t under = sk_pixel(bare, PV_WIDTH, x, y);
            const bool on_a = sk_pixel(a, PV_WIDTH, x, y) != under, on_b = sk_pixel(b, PV_WIDTH, x, y) != under;
            in_a += on_a ? 1u : 0u;
            gap += on_a != on_b ? 1u : 0u;
        }
    }
    *out_a = in_a;
    return gap;
}

/* The preview covers the pixels the block itself covers once placed: the same shape, turned the same way, because both are the card's
 * table through the world's shapes. Apart, at most, from a pixel's worth along its edges, where the jitter-free rays of the preview and
 * the frame's own rays fall on either side. */
TEST(each_preview_is_the_shape_the_block_is_drawn_in_turned_by_its_facing) {
    sk_rig_t rig;
    REQUIRE(pv_begin(t, &rig, PV_PLAIN));
    render_camera_t eye;
    pv_eye(&eye);
    for (uint32_t which = 0u; which < PV_LAYOUTS; which++) {
        const pv_layout_t layout = pv_layout(which);
        REQUIRE(pv_wall(t, &rig, &layout, BLOCK_ID_OAK_PLANKS));
        REQUIRE(sk_hand_world_sync(t, &rig));
        pv_send(&rig, nullptr);
        uint8_t *const bare = pv_picture(t, &rig, &eye, 1u);
        pv_send(&rig, &layout);
        uint8_t *const preview = pv_picture(t, &rig, &eye, 1u);
        pv_send(&rig, nullptr);
        for (uint32_t c = 0u; c < layout.count; c++) {
            REQUIRE(pv_put(t, &rig, layout.cells[c][0], layout.cells[c][1], layout.cells[c][2], layout.cells[c][0] + 1,
                           layout.cells[c][1] + 1, layout.cells[c][2] + 1, layout.voxels[c]));
        }
        REQUIRE(sk_hand_world_sync(t, &rig));
        uint8_t *const placed = pv_picture(t, &rig, &eye, 1u);
        uint64_t covered = 0u;
        const uint64_t gap = pv_silhouette_gap(placed, preview, bare, &covered);
        printf("  %s: the block covers %" PRIu64 " pixels, and %" PRIu64 " differ in what the preview covers\n",
               layout.name, covered, gap);
        CHECK(covered > 20u);
        CHECK(gap * 20u <= covered);
        for (uint32_t c = 0u; c < layout.count; c++) {
            REQUIRE(pv_put(t, &rig, layout.cells[c][0], layout.cells[c][1], layout.cells[c][2], layout.cells[c][0] + 1,
                           layout.cells[c][1] + 1, layout.cells[c][2] + 1, BLOCK_ID_AIR));
        }
        REQUIRE(pv_wall(t, &rig, &layout, BLOCK_ID_AIR));
        REQUIRE(sk_hand_world_sync(t, &rig));
    }
    pv_end(t, &rig);
}

TEST(a_wall_in_front_hides_it_and_its_cells_beyond_four_are_refused) {
    sk_rig_t rig;
    REQUIRE(pv_begin(t, &rig, PV_PLAIN));
    render_camera_t eye;
    pv_eye(&eye);
    /* Behind the wall at x = 20, which the eye cannot see through. */
    pv_layout_t layout = pv_layout(0u);
    layout.cells[0][0] = 22;
    pv_send(&rig, nullptr);
    uint8_t *const bare = pv_picture(t, &rig, &eye, 1u);
    pv_send(&rig, &layout);
    uint8_t *const behind = pv_picture(t, &rig, &eye, 1u);
    CHECK_MEM_EQ(behind, bare, post_xrgb_bytes(PV_WIDTH, PV_HEIGHT));
    /* Under the floor. */
    layout.cells[0][0] = 12;
    layout.cells[0][2] = -3;
    pv_send(&rig, &layout);
    uint8_t *const under = pv_picture(t, &rig, &eye, 1u);
    CHECK_MEM_EQ(under, bare, post_xrgb_bytes(PV_WIDTH, PV_HEIGHT));
    /* No frame takes from the heap for it. */
    layout = pv_bench(12, 6, -1);
    heap_counters_t before, after;
    pv_send(&rig, &layout);
    REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 8u; i++) {
        pv_send(&rig, &layout);
        REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    }
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));
    /* More cells than it holds is refused before anything starts. */
    traverse_camera_t view;
    render_camera_to_traverse(&eye, PV_WIDTH, PV_HEIGHT, &view);
    entity_rays_scene_t scene = {0};
    scene.preview.count = ENTITY_RAYS_PREVIEW_CELLS + 1u;
    scene.preview.pixel_end[0] = 1u;
    scene.preview.pixel_end[1] = 1u;
    const float32_t jitter[2] = {0.0f32, 0.0f32};
    const traverse_world_t world = {.borrowed_table = rig.render->buffers.borrowed_table,
                                    .borrowed_pool = rig.render->buffers.borrowed_pool,
                                    .window = rig.render->buffers.window};
    gpu_error_t error;
    memset(&error, 0, sizeof(error));
    CHECK_EQ_U64(entity_rays_overlay(&error, &scene, &world, &view, jitter, BLOCK_ID_WATER, &rig.render->owned_shaded,
                                     &rig.render->owned_xrgb),
                 GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(error.borrowed_what, "preview");
    /* A sim that says more is cut to what the card holds. */
    render_entities_preview_t many = {.count = 9u};
    render_entities_t *const entities = rig.render->borrowed_entities;
    render_entities_begin(entities);
    render_entities_preview_set(entities, &many);
    CHECK_EQ_U64(entities->slots[entities->exchange.back].preview.count, ENTITY_RAYS_PREVIEW_CELLS);
    render_entities_publish(entities);
    pv_end(t, &rig);
}

/* ---- Golden pictures ------------------------------------------------------------------------------------------------------- */

static void pv_golden_compare(test_context_t *t, const char *path, const uint8_t *picture) {
    const uint64_t capacity = display_png_bytes_needed(PV_WIDTH, PV_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        uint64_t written = 0u;
        REQUIRE(display_png_encode(picture, PV_WIDTH * 4u, PV_WIDTH, PV_HEIGHT, file, capacity, &written));
        REQUIRE(file_write_atomic(path, file, written) == FILE_OK);
        printf("  wrote %s\n", path);
        return;
    }
    uint64_t length = 0u;
    if (!CHECK_EQ_U64(file_read_whole(path, file, capacity, &length), FILE_OK)) {
        printf("  cannot read %s: run with MC_GOLDEN_WRITE=1, then look at what it wrote\n", path);
        return;
    }
    png_info_t info;
    png_error_t error;
    REQUIRE(png_probe(file, length, &info, &error) == PNG_OK);
    REQUIRE(info.width == PV_WIDTH && info.height == PV_HEIGHT);
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    REQUIRE(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK);
    uint64_t wrong = 0u, exact = 0u;
    for (uint32_t y = 0u; y < PV_HEIGHT; y++) {
        for (uint32_t x = 0u; x < PV_WIDTH; x++) {
            const uint64_t i = (uint64_t)y * PV_WIDTH + x;
            const uint32_t ours = sk_pixel(picture, PV_WIDTH, x, y);
            const uint32_t golden = post_xrgb(rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u]);
            wrong += sk_bytes_near(ours, golden, PV_GOLDEN_TOLERANCE) ? 0u : 1u;
            exact += ours == golden ? 1u : 0u;
        }
    }
    printf("  %s: %" PRIu64 " of %" PRIu64 " pixels exact, %" PRIu64 " past the tolerance\n", path, exact,
           (uint64_t)PV_PIXELS, wrong);
    CHECK_EQ_U64(wrong, 0u);
}

/* Each of the four as a person playing sees it, and the cube red. */
TEST(golden_pictures_match) {
    sk_rig_t rig;
    REQUIRE(pv_begin(t, &rig, SHADE_FEATURES_PLAY));
    render_camera_t eye;
    pv_eye(&eye);
    static const char *const paths[] = {"tests/cuda/preview/fixtures/cube.png",
                                        "tests/cuda/preview/fixtures/wall_torch.png",
                                        "tests/cuda/preview/fixtures/slab.png", "tests/cuda/preview/fixtures/bench.png",
                                        "tests/cuda/preview/fixtures/blocked.png"};
    for (uint32_t g = 0u; g < sizeof(paths) / sizeof(paths[0]); g++) {
        pv_layout_t layout = pv_layout(g < 4u ? g : 0u);
        layout.blocked = g == 4u ? 1u : 0u;
        REQUIRE(pv_wall(t, &rig, &layout, BLOCK_ID_OAK_PLANKS));
        REQUIRE(sk_hand_world_sync(t, &rig));
        pv_send(&rig, &layout);
        pv_golden_compare(t, paths[g], pv_picture(t, &rig, &eye, PV_GOLDEN_FRAMES));
        REQUIRE(pv_wall(t, &rig, &layout, BLOCK_ID_AIR));
    }
    pv_end(t, &rig);
}

/* ---- The cost ----------------------------------------------------------------------------------------------------------------- */

static void pv_cost_row(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera, const char *name,
                        const pv_layout_t *layout) {
    render_frame_t *const frame = &rig->render->frame;
    pv_send(rig, layout);
    for (uint32_t i = 0u; i < 4u; i++) {
        REQUIRE(sk_draw(t, rig, camera) != nullptr);
    }
    const render_frame_stats_t before = frame->stats;
    for (uint32_t i = 0u; i < PV_COST_FRAMES; i++) {
        REQUIRE(sk_draw(t, rig, camera) != nullptr);
    }
    const float64_t frames = (float64_t)PV_COST_FRAMES;
    const float64_t post_ms = (frame->stats.sum_ms[FRAME_PASS_POST] - before.sum_ms[FRAME_PASS_POST]) / frames;
    const float64_t total_ms = (frame->stats.sum_total_ms - before.sum_total_ms) / frames;
    printf("  cost, %-40s: post with the overlay %7.3f ms, frame %7.3f ms\n", name, (libc_double_t)post_ms,
           (libc_double_t)total_ms);
    CHECK(total_ms > 0.0f64);
}

/* A preview near the eye on real terrain, at 1080p, on the card's own clock, beside none. Nothing here is a limit: the numbers go in the
 * phase file. */
TEST(the_cost_is_measured_on_real_terrain) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = PV_COST_RENDER_DISTANCE,
                                  .width = PV_COST_WIDTH,
                                  .height = PV_COST_HEIGHT,
                                  .features_off = 0u,
                                  .god_rays = 0u,
                                  .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    render_camera_t camera = game->camera;
    camera.pitch = -0.6f32;
    render_frame_time_set(&rig.render->frame, (float32_t)GAME_DAY_START);
    /* Right ahead and below, where a hand reaches: the largest a preview gets on the picture but for one at the eye. */
    const float64_t ahead_x = camera.position[0] + 2.0f64 * cos((float64_t)camera.yaw);
    const float64_t ahead_y = camera.position[1] + 2.0f64 * sin((float64_t)camera.yaw);
    const pv_layout_t near =
        pv_bench((int32_t)floor(ahead_x), (int32_t)floor(ahead_y), (int32_t)floor(camera.position[2]) - 2);
    pv_cost_row(t, &rig, &camera, "no preview", nullptr);
    pv_cost_row(t, &rig, &camera, "a bench's preview two blocks ahead", &near);
    pv_cost_row(t, &rig, &camera, "no preview again", nullptr);
    pv_cost_row(t, &rig, &camera, "the bench again", &near);
    game_close(game);
    sk_rig_end(t, &rig);
}

TEST_MAIN()
