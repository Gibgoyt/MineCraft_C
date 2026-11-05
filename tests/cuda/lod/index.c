/* ctest render_feature_lod. See README.md. */
#include "harness/index.h"

#include "png/index.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "log/index.h"
#include "main/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    LD_WIDTH = 320u,
    LD_HEIGHT = 180u,
    LD_PIXELS = LD_WIDTH * LD_HEIGHT,
    LD_GOLDEN_TOLERANCE = 2u,
    LD_GOLDEN_FRAMES = 24u,
    LD_PROGRAM_TIMEOUT_MS = 120000u,
    LD_COST_FRAMES = 30u,
    LD_COST_WIDTH = 1920u,
    LD_COST_HEIGHT = 1080u,
    LD_RANDOM_RAYS = 3000u,
    LD_HAND_RADIUS = 2u,
    LD_SEA_STEP = 12u,
};

#define LD_FOG_FAR 20000.0f32
/* The light is then each face's one brightness and nothing else, and each frame stands alone. */
#define LD_PLAIN                                                                                                       \
    (SHADE_FEATURES_ALL &                                                                                              \
     ~(uint32_t)(SHADE_FEATURE_SKY | SHADE_FEATURE_FOG | SHADE_FEATURE_SHADOW | SHADE_FEATURE_LIGHT))

#include "cuda/sky/fixtures/rig.h"

/* ---- Ground a test can work out for itself ------------------------------------------------------------------------------------- */

typedef enum ld_ground_t : uint32_t {
    LD_GROUND_FLAT = 0,  /* grass at z = 10, on stone */
    LD_GROUND_HILLS = 1, /* grass on rolling hills */
    LD_GROUND_WALL = 2,  /* grass at z = 0, and a wall of stone 200 high from x = 1024 to 1056 */
    LD_GROUND_LAKE = 3,  /* water down to z = -20 */
} ld_ground_t;
static_assert(sizeof(ld_ground_t) == 4);

typedef struct ld_source_t {
    uint32_t ground;  /* 0 */
    uint8_t _pad0[4]; /* 4 */
} ld_source_t;
static_assert(sizeof(ld_source_t) == 8);

/* One above the highest block of ground, as a storage height. */
static int32_t ld_ground_top(uint32_t ground, int32_t x, int32_t y) {
    switch ((ld_ground_t)ground) {
    case LD_GROUND_FLAT:
        return 256 + 11;
    case LD_GROUND_HILLS:
        return 256 + 60 + (int32_t)(40.0f64 * sin((float64_t)x / 150.0f64) + 35.0f64 * cos((float64_t)y / 110.0f64));
    case LD_GROUND_WALL:
        return x >= 1024 && x < 1056 ? 256 + 201 : 256 + 1;
    case LD_GROUND_LAKE:
        return 256 - 20;
    }
    return 0;
}

static void ld_sample(const void *borrowed_context, int32_t x, int32_t y, uint32_t shift, uint16_t *out) {
    (void)shift;
    const uint32_t ground = ((const ld_source_t *)borrowed_context)->ground;
    const int32_t top = ld_ground_top(ground, x, y);
    for (int32_t z = 0; z < (int32_t)RENDER_LOD_HEIGHT_BLOCKS; z++) {
        uint16_t block = BLOCK_ID_AIR;
        if (z < top - 1) {
            block = BLOCK_ID_STONE;
        } else if (z == top - 1) {
            block = BLOCK_ID_GRASS_BLOCK;
        } else if (ground == LD_GROUND_LAKE && z < 256) {
            block = BLOCK_ID_WATER;
        }
        out[z] = block;
    }
}

/* What a coarse voxel of this ground must be, by the same rule as the workers' and from the same samples: which is what
 * a_coarse_voxel_is_as_on_paper holds to numbers worked out by hand. */
static uint16_t ld_voxel(const ld_source_t *source, uint32_t shift, int32_t x, int32_t y, int32_t z) {
    if (z < 0 || z >= (int32_t)(RENDER_LOD_HEIGHT_BLOCKS >> shift)) {
        return BLOCK_ID_AIR;
    }
    uint16_t column[RENDER_LOD_HEIGHT_BLOCKS];
    uint16_t cells[RENDER_LOD_HEIGHT_BLOCKS];
    const int32_t size = 1 << shift;
    ld_sample(source, x * size + size / 2, y * size + size / 2, shift, column);
    (void)render_lod_cells_from_column(column, shift, 1u, cells);
    return cells[z];
}

/* ---- A world with a far field ------------------------------------------------------------------------------------------------- */

/* Pumps until every column wanted is on the card. */
static bool ld_settle(test_context_t *t, sk_rig_t *rig, const float64_t position[3]) {
    render_lod_t *const lod = rig->render->borrowed_lod;
    for (uint64_t pass = 0u; pass < 100000000ull; pass++) {
        if (!CHECK_EQ_U64(render_lod_frame(lod, position), GPU_STATUS_OK)) {
            return false;
        }
        if (render_lod_idle(lod)) {
            /* The last column's table entry and what is shown are worked out by the frame that sent it. */
            return CHECK_EQ_U64(render_lod_frame(lod, position), GPU_STATUS_OK);
        }
        time_sleep_ns(20000);
    }
    return CHECK(false);
}

static bool ld_begin(test_context_t *t, sk_rig_t *rig, const ld_source_t *source, uint32_t full, uint32_t far,
                     float32_t fog_end, uint32_t features, const float64_t position[3]) {
    if (!sk_rig_begin(t, rig) || !sk_hand_world_open(t, rig, full, LD_WIDTH, LD_HEIGHT, fog_end) ||
        !sk_hand_world_sync(t, rig)) {
        return false;
    }
    render_frame_features_set(&rig->render->frame, features);
    const render_lod_plan_t plan = render_lod_plan(full, far);
    if (!CHECK(plan.level_count > 0u) ||
        !CHECK_EQ_U64(render_open_lod(rig->render, &plan, ld_sample, source), GPU_STATUS_OK)) {
        return false;
    }
    return ld_settle(t, rig, position);
}

static void ld_end(test_context_t *t, sk_rig_t *rig) {
    sk_hand_world_close(rig);
    sk_rig_end(t, rig);
}

static bool ld_put(test_context_t *t, sk_rig_t *rig, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1,
                   int32_t z1, uint32_t state) {
    for (int32_t z = z0; z < z1; z++) {
        for (int32_t y = y0; y < y1; y++) {
            for (int32_t x = x0; x < x1; x++) {
                const world_edit_status_t status = world_edit_block_set(&rig->world->edit, x, y, z, (uint16_t)state);
                if (!CHECK(status == WORLD_EDIT_STATUS_CHANGED || status == WORLD_EDIT_STATUS_UNCHANGED)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static uint8_t *ld_picture(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera) {
    const uint64_t bytes = post_xrgb_bytes(LD_WIDTH, LD_HEIGHT);
    uint8_t *const copy = test_alloc(t, bytes);
    const uint8_t *const picture = sk_draw(t, rig, camera);
    if (picture != nullptr) {
        memcpy(copy, picture, bytes);
    }
    return copy;
}

static shade_pixel_t *ld_shaded(test_context_t *t, sk_rig_t *rig) {
    const uint64_t bytes = shade_pixels_bytes(LD_WIDTH, LD_HEIGHT);
    shade_pixel_t *const shaded = test_alloc(t, bytes);
    CHECK_EQ_U64(device_memory_read(&rig->bridge->device_memory, &rig->render->owned_shaded, 0u, shaded, bytes),
                 GPU_STATUS_OK);
    return shaded;
}

static lod_hit_t *ld_hits(test_context_t *t, sk_rig_t *rig) {
    const uint64_t bytes = lod_hits_bytes(LD_WIDTH, LD_HEIGHT);
    lod_hit_t *const hits = test_alloc(t, bytes);
    CHECK_EQ_U64(device_memory_read(&rig->bridge->device_memory, &rig->render->owned_lod_hits, 0u, hits, bytes),
                 GPU_STATUS_OK);
    return hits;
}

/* ---- A march, in float64_t, to hold the card's rays to ----------------------------------------------------------------------- */

typedef struct ld_marched_t {
    float64_t t;       /* 0: in blocks */
    uint32_t material; /* 8: LOD_NO_MATERIAL for nothing */
    uint32_t face;     /* 12 */
} ld_marched_t;
static_assert(sizeof(ld_marched_t) == 16);

/* Whether full detail has a column at this cell. */
static bool ld_fine_ready(const world_buffers_t *fine, int32_t x, int32_t y) {
    const int32_t column_x = x >> 4, column_y = y >> 4;
    uint32_t slot = 0u;
    if (!world_buffers_slot_of(&fine->window, column_x, column_y, &slot)) {
        return false;
    }
    const world_buffers_column_t *const column = &fine->borrowed_shadow[slot];
    return column->ready != 0u && column->chunk_x == column_x && column->chunk_y == column_y;
}

/* One level, as the far field's own rule has it: from t_from to t_exit, one voxel at a time, taking only what the ray enters after t_from.
 * A ray going on past a column full detail holds starts again from where it leaves that column. */
static bool ld_march_level(const ld_source_t *source, const lod_level_t *level, const world_buffers_t *fine,
                           const float64_t eye[3], const float64_t direction[3], float64_t *t_from, float64_t t_exit,
                           ld_marched_t *out) {
    const float64_t size = (float64_t)(1u << level->shift);
    while (*t_from < t_exit) {
        int32_t voxel[3];
        float64_t step_t[3], delta[3];
        int32_t step[3];
        for (uint32_t i = 0u; i < 3u; i++) {
            const float64_t at = (eye[i] + direction[i] * *t_from) / size;
            voxel[i] = (int32_t)floor(at + (direction[i] > 0.0f64 ? 1e-9f64 : -1e-9f64));
            step[i] = direction[i] > 0.0f64 ? 1 : -1;
            const float64_t plane = (float64_t)(direction[i] > 0.0f64 ? voxel[i] + 1 : voxel[i]) * size;
            step_t[i] = (plane - eye[i]) / direction[i];
            delta[i] = size / fabs(direction[i]);
        }
        bool went_on = false;
        for (;;) {
            uint32_t axis = 0u;
            axis = step_t[1] < step_t[axis] ? 1u : axis;
            axis = step_t[2] < step_t[axis] ? 2u : axis;
            const float64_t t = step_t[axis];
            if (t >= t_exit) {
                return false;
            }
            voxel[axis] += step[axis];
            step_t[axis] += delta[axis];
            const uint16_t block = ld_voxel(source, level->shift, voxel[0], voxel[1], voxel[2]);
            if (block == BLOCK_ID_AIR) {
                continue;
            }
            const int32_t cell_x = (int32_t)floor(eye[0] + direction[0] * (t + 1e-3f64));
            const int32_t cell_y = (int32_t)floor(eye[1] + direction[1] * (t + 1e-3f64));
            if (fine != nullptr && ld_fine_ready(fine, cell_x, cell_y)) {
                const float64_t plane_x = (float64_t)((cell_x & ~15) + (direction[0] > 0.0f64 ? 16 : 0));
                const float64_t plane_y = (float64_t)((cell_y & ~15) + (direction[1] > 0.0f64 ? 16 : 0));
                const float64_t past = fmin((plane_x - eye[0]) / direction[0], (plane_y - eye[1]) / direction[1]);
                *t_from = fmax(past, t + 1e-3f64);
                went_on = true;
                break;
            }
            out->t = t;
            out->material = block;
            out->face = axis * 2u + (direction[axis] > 0.0f64 ? 0u : 1u);
            return true;
        }
        if (!went_on) {
            break;
        }
    }
    return false;
}

/* eye has a storage height on z. */
static void ld_march(const ld_source_t *source, const lod_scene_t *scene, const world_buffers_t *fine,
                     const float64_t eye[3], const float64_t direction[3], float64_t t_start, ld_marched_t *out) {
    out->t = (float64_t)TRAVERSE_COARSE_SKY;
    out->material = LOD_NO_MATERIAL;
    out->face = TRAVERSE_FACE_MISS;
    float64_t t_from = t_start;
    const float64_t t_up = direction[2] > 0.0f64 ? ((float64_t)scene->top_storage - eye[2]) / direction[2] : 1e30f64;
    for (uint32_t i = 0u; i < scene->level_count; i++) {
        const lod_level_t *const level = &scene->levels[i];
        if (level->shown_side == 0u || t_from >= t_up) {
            continue;
        }
        const float64_t scale = (float64_t)(16u << level->shift);
        float64_t t_exit = 1e30f64;
        const float64_t low[2] = {(float64_t)level->shown_min_x * scale, (float64_t)level->shown_min_y * scale};
        for (uint32_t a = 0u; a < 2u; a++) {
            const float64_t plane = direction[a] > 0.0f64 ? low[a] + (float64_t)level->shown_side * scale : low[a];
            t_exit = fmin(t_exit, (plane - eye[a]) / direction[a]);
        }
        if (t_exit > t_from) {
            if (ld_march_level(source, level, fine, eye, direction, &t_from, t_exit, out)) {
                return;
            }
            t_from = fmax(t_from, t_exit - 0.01f64);
        }
    }
}

/* ---- By hand ---------------------------------------------------------------------------------------------------------------- */

TEST(the_levels_are_as_on_paper) {
    /* Default full detail to the largest distance: 896, 1792, 3584 and 7168 blocks, and then what is left in voxels of 64. */
    const render_lod_plan_t most = render_lod_plan(32u, 512u);
    REQUIRE(CHECK_EQ_U64(most.level_count, 5u));
    const uint32_t shifts[] = {2u, 3u, 4u, 5u, 6u};
    const uint32_t radii[] = {16u, 16u, 16u, 16u, 11u};
    for (uint32_t i = 0u; i < 5u; i++) {
        CHECK_EQ_U64(most.shifts[i], shifts[i]);
        CHECK_EQ_U64(most.radii[i], radii[i]);
    }
    /* Reach and nothing over: the last one's rays go at least the distance, and the one before does not. */
    CHECK(((most.radii[4] - RENDER_LOD_MARGIN_COLUMNS) * 16u << most.shifts[4]) >= 512u * 16u);
    CHECK(((most.radii[3] - RENDER_LOD_MARGIN_COLUMNS) * 16u << most.shifts[3]) < 512u * 16u);
    /* Full detail reaching all of it wants none. */
    CHECK_EQ_U64(render_lod_plan(32u, 32u).level_count, 0u);
    CHECK_EQ_U64(render_lod_plan(32u, 10u).level_count, 0u);
    /* Small distances stop early. */
    const render_lod_plan_t small = render_lod_plan(8u, 64u);
    CHECK_EQ_U64(small.level_count, 3u);
    CHECK_EQ_U64(small.shifts[0], 1u);
    CHECK_EQ_U64(small.shifts[2], 3u);
    CHECK_EQ_U64(small.radii[2], 11u);
    /* The finest of all is one block per 8 cubic metres. */
    CHECK_EQ_U64(render_lod_plan(1u, 512u).level_count, LOD_LEVELS_MAX);
    CHECK_EQ_U64(render_lod_plan(1u, 512u).shifts[0], 1u);
    /* Every distance there can be is reached. */
    for (uint32_t full = 1u; full <= 64u; full++) {
        for (uint32_t far = full + 1u; far <= 512u; far += 7u) {
            const render_lod_plan_t plan = render_lod_plan(full, far);
            REQUIRE(plan.level_count > 0u);
            const uint32_t last = plan.level_count - 1u;
            CHECK(((plan.radii[last] - RENDER_LOD_MARGIN_COLUMNS) * 16u << plan.shifts[last]) >= far * 16u);
        }
    }
}

TEST(a_coarse_voxel_is_as_on_paper) {
    uint16_t column[RENDER_LOD_HEIGHT_BLOCKS];
    uint16_t cells[RENDER_LOD_HEIGHT_BLOCKS * 4u];
    memset(column, 0, sizeof(column));
    /* Stone to 100, grass from 100 to 102, and a flower on top. */
    for (uint32_t z = 0u; z < 100u; z++) {
        column[z] = BLOCK_ID_STONE;
    }
    column[100] = BLOCK_ID_GRASS_BLOCK;
    column[101] = BLOCK_ID_GRASS_BLOCK;
    column[102] = BLOCK_ID_DANDELION;
    /* In voxels of 4: voxel 24 is stone to 96, and 25 is 100 and up, with 2 grass of 4: half is enough, and the top one shows. */
    CHECK_EQ_I64(render_lod_cells_from_column(column, 2u, 1u, cells), 25);
    CHECK_EQ_U64(cells[23], BLOCK_ID_STONE);
    CHECK_EQ_U64(cells[24], BLOCK_ID_STONE);
    CHECK_EQ_U64(cells[25], BLOCK_ID_GRASS_BLOCK);
    CHECK_EQ_U64(cells[26], BLOCK_ID_AIR);
    /* In voxels of 16: voxel 6 is 96 to 112, of which 6 are blocks and a flower does not count: not half, so the ground stops at 96. */
    CHECK_EQ_I64(render_lod_cells_from_column(column, 4u, 1u, cells), 5);
    CHECK_EQ_U64(cells[5], BLOCK_ID_STONE);
    CHECK_EQ_U64(cells[6], BLOCK_ID_AIR);
    /* In voxels of 64: voxel 1 is 64 to 128, with 38 blocks of 64, of which the highest is grass. */
    CHECK_EQ_I64(render_lod_cells_from_column(column, 6u, 1u, cells), 1);
    CHECK_EQ_U64(cells[1], BLOCK_ID_GRASS_BLOCK);
    CHECK_EQ_U64(cells[0], BLOCK_ID_STONE);
    /* A frozen sea: water to 99, ice at 100. In voxels of 2, a voxel of ice and air is half ice. In voxels of 16 the half rule takes it, and it shows on the water it lay on, at the water's height. */
    memset(column, 0, sizeof(column));
    for (uint32_t z = 0u; z < 100u; z++) {
        column[z] = BLOCK_ID_WATER;
    }
    column[100] = BLOCK_ID_ICE;
    CHECK_EQ_I64(render_lod_cells_from_column(column, 1u, 1u, cells), 50);
    CHECK_EQ_U64(cells[50], BLOCK_ID_ICE);
    CHECK_EQ_U64(cells[49], BLOCK_ID_WATER);
    CHECK_EQ_I64(render_lod_cells_from_column(column, 4u, 1u, cells), 5);
    CHECK_EQ_U64(cells[6], BLOCK_ID_AIR);
    CHECK_EQ_U64(cells[5], BLOCK_ID_ICE);
    CHECK_EQ_U64(cells[4], BLOCK_ID_WATER);
    /* Ice over rock stays what the half rule says: only water takes it. */
    for (uint32_t z = 0u; z < 100u; z++) {
        column[z] = BLOCK_ID_STONE;
    }
    CHECK_EQ_I64(render_lod_cells_from_column(column, 4u, 1u, cells), 5);
    CHECK_EQ_U64(cells[5], BLOCK_ID_STONE);
    /* Nothing at all. */
    memset(column, 0, sizeof(column));
    CHECK_EQ_I64(render_lod_cells_from_column(column, 1u, 1u, cells), -1);
    /* A stride puts each where a section wants it. */
    column[3] = BLOCK_ID_DIRT;
    column[2] = BLOCK_ID_DIRT;
    CHECK_EQ_I64(render_lod_cells_from_column(column, 1u, 4u, cells), 1);
    CHECK_EQ_U64(cells[4], BLOCK_ID_DIRT);
    /* Which column a place is in: down, for what is below zero. */
    CHECK_EQ_I64(render_lod_column_of(0.5f64, 1u), 0);
    CHECK_EQ_I64(render_lod_column_of(31.9f64, 1u), 0);
    CHECK_EQ_I64(render_lod_column_of(32.0f64, 1u), 1);
    CHECK_EQ_I64(render_lod_column_of(-0.5f64, 1u), -1);
    CHECK_EQ_I64(render_lod_column_of(-65.0f64, 2u), -2);
}

TEST(far_colour_is_as_on_paper) {
    const shade_sun_t sun = {.direction = {0.0f32, 0.0f32, 1.0f32},
                             .direct = {0.5f32, 0.5f32, 0.5f32},
                             .ambient = {0.25f32, 0.25f32, 0.25f32},
                             .block = {1.0f32, 1.0f32, 1.0f32}};
    const uint8_t mean[4] = {255u, 0u, 188u, 255u};
    const float32_t sky[3] = {0.1f32, 0.2f32, 0.3f32};
    const float32_t down[3] = {0.0f32, 0.0f32, -1.0f32};
    const lod_hit_t top = {100.0f32, BLOCK_ID_STONE, TRAVERSE_FACE_POS_Z, LOD_SUN_SEEN};
    float32_t colour[3];
    shade_far_colour(&sun, mean, false, false, false, &top, down, sky, colour);
    CHECK_NEAR(colour[0], 0.75f32, 1e-4f32);
    CHECK_NEAR(colour[1], 0.0f32, 1e-4f32);
    CHECK_NEAR(colour[2], 0.75f32 * shade_srgb_to_linear(188.0f32 / 255.0f32), 1e-4f32);
    /* The sun's share goes with the sun. */
    const lod_hit_t shaded_top = {100.0f32, BLOCK_ID_STONE, TRAVERSE_FACE_POS_Z, LOD_SUN_HIDDEN};
    shade_far_colour(&sun, mean, false, false, false, &shaded_top, down, sky, colour);
    CHECK_NEAR(colour[0], 0.25f32, 1e-4f32);
    /* A side has its brightness and no sun from straight above. */
    const lod_hit_t side = {100.0f32, BLOCK_ID_STONE, TRAVERSE_FACE_NEG_X, LOD_SUN_SEEN};
    const float32_t east[3] = {1.0f32, 0.0f32, 0.0f32};
    shade_far_colour(&sun, mean, false, false, false, &side, east, sky, colour);
    CHECK_NEAR(colour[0], 0.25f32 * SHADE_BRIGHTNESS_X, 1e-4f32);
    /* Water seen straight on: nearly all its own. Along it: nearly all sky. */
    const float32_t flat[3] = {0.9999f32, 0.0f32, -0.01414f32};
    const uint8_t clear[4] = {255u, 0u, 188u, 0u};
    shade_far_colour(&sun, clear, true, true, false, &top, down, sky, colour);
    CHECK_NEAR(colour[2], SHADE_WATER_DEEP_B * 0.75f32 * (1.0f32 - SHADE_WATER_F0) + 0.3f32 * SHADE_WATER_F0, 1e-3f32);
    shade_far_colour(&sun, clear, true, true, false, &top, flat, sky, colour);
    CHECK(colour[2] > 0.26f32 && colour[2] < 0.3f32);
    shade_far_colour(&sun, clear, true, false, false, &top, flat, sky, colour);
    CHECK_NEAR(colour[2], SHADE_WATER_DEEP_B * 0.75f32, 1e-4f32);
    /* The texture's mean over deep water, as far as it covers: near water's order, with half of red's linear 1 on top. */
    const uint8_t half[4] = {255u, 0u, 0u, 51u};
    shade_far_colour(&sun, half, true, false, false, &top, down, sky, colour);
    CHECK_NEAR(colour[0], (0.2f32 + SHADE_WATER_DEEP_R * 0.8f32) * 0.75f32, 1e-4f32);
    CHECK_NEAR(colour[1], SHADE_WATER_DEEP_G * 0.8f32 * 0.75f32, 1e-4f32);
    /* A side of far water, turned from the sun and asking none, looks as its top does. */
    const lod_hit_t water_side = {100.0f32, BLOCK_ID_WATER, TRAVERSE_FACE_NEG_X, LOD_SUN_HIDDEN};
    float32_t on_top[3];
    shade_far_colour(&sun, half, true, true, false, &top, flat, sky, on_top);
    shade_far_colour(&sun, half, true, true, false, &water_side, flat, sky, colour);
    for (uint32_t i = 0u; i < 3u; i++) {
        CHECK_NEAR(colour[i], on_top[i], 1e-6f32);
    }
    /* From inside water: no mirror and no texture, only what deep water gives back. */
    shade_far_colour(&sun, half, true, true, true, &water_side, flat, sky, colour);
    CHECK_NEAR(colour[0], SHADE_WATER_DEEP_R * 0.75f32, 1e-4f32);
    CHECK_NEAR(colour[2], SHADE_WATER_DEEP_B * 0.75f32, 1e-4f32);
}

/* ---- What workers build ------------------------------------------------------------------------------------------------------ */

/* Every level's columns as a real kernel reads them, at places all over, against the samples they came from. */
static void ld_grids_check(test_context_t *t, sk_rig_t *rig, const ld_source_t *source, uint64_t *out_voxels) {
    device_block_t scratch = {0};
    const uint32_t count = 1u << 14;
    REQUIRE(device_memory_alloc(&rig->bridge->device_memory, DEVICE_POOL_FRAMES, count * WORLD_BUFFERS_PROBE_BYTES,
                                "probe", &scratch) == GPU_STATUS_OK);
    world_buffers_probe_query_t *const queries = test_alloc(t, count * sizeof(world_buffers_probe_query_t));
    world_buffers_probe_result_t *const results = test_alloc(t, count * sizeof(world_buffers_probe_result_t));
    uint16_t *const wanted = test_alloc(t, count * sizeof(uint16_t));
    render_lod_t *const lod = rig->render->borrowed_lod;
    uint64_t state = 0x1234567u, wrong = 0u, voxels = 0u;
    for (uint32_t index = 0u; index < lod->level_count; index++) {
        render_lod_level_t *const level = &lod->levels[index];
        const int32_t across = (int32_t)level->buffers.window.side * 16;
        for (uint32_t i = 0u; i < count; i++) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            const int32_t x = level->buffers.window.min_x * 16 + (int32_t)((state >> 20) % (uint64_t)across);
            const int32_t y = level->buffers.window.min_y * 16 + (int32_t)((state >> 36) % (uint64_t)across);
            /* Near the surface half the time, where it matters. */
            const int32_t voxels_high = (int32_t)(RENDER_LOD_HEIGHT_BLOCKS >> level->shift);
            const int32_t top =
                ld_ground_top(source->ground, x * (1 << level->shift), y * (1 << level->shift)) >> level->shift;
            const int32_t z = (state & 1u) != 0u ? (int32_t)((state >> 50) % (uint64_t)voxels_high)
                                                 : top - 2 + (int32_t)((state >> 50) % 5u);
            queries[i] = (world_buffers_probe_query_t){x, y, z - (int32_t)WORLD_BUFFERS_Z_ORIGIN};
            wanted[i] = ld_voxel(source, level->shift, x, y, z);
        }
        REQUIRE(world_buffers_probe(&level->buffers, &scratch, queries, results, count) == GPU_STATUS_OK);
        for (uint32_t i = 0u; i < count; i++) {
            const bool occupied = results[i].state == WORLD_BUFFERS_VOXEL_OCCUPIED;
            const bool right = wanted[i] == BLOCK_ID_AIR ? results[i].state == WORLD_BUFFERS_VOXEL_EMPTY
                                                         : occupied && results[i].material == wanted[i] &&
                                                               results[i].light == SHADE_LIGHT_OPEN_SKY;
            wrong += right ? 0u : 1u;
            voxels++;
        }
    }
    REQUIRE(device_memory_free(&rig->bridge->device_memory, &scratch) == GPU_STATUS_OK);
    printf("  %" PRIu64 " coarse voxels looked at on the card, %" PRIu64 " wrong\n", voxels, wrong);
    CHECK_EQ_U64(wrong, 0u);
    *out_voxels = voxels;
}

TEST(what_workers_build_is_what_the_samples_say_at_every_level) {
    sk_rig_t rig;
    const ld_source_t source = {LD_GROUND_HILLS, {0}};
    const float64_t eye[3] = {40.5f64, -70.5f64, 200.5f64};
    REQUIRE(ld_begin(t, &rig, &source, LD_HAND_RADIUS, 64u, LD_FOG_FAR, LD_PLAIN, eye));
    const render_lod_t *const lod = rig.render->borrowed_lod;
    CHECK(lod->level_count >= 3u);
    uint64_t columns = 0u;
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        const render_lod_level_t *const level = &lod->levels[i];
        columns += level->column_count;
        /* Everything shown, as far as a level shows, around the eye's column. */
        CHECK_EQ_U64(level->shown_side, 2u * ((uint32_t)level->radius - RENDER_LOD_MARGIN_COLUMNS) + 1u);
        CHECK_EQ_I64(level->center_x, render_lod_column_of(eye[0], level->shift));
        CHECK_EQ_I64(level->center_y, render_lod_column_of(eye[1], level->shift));
    }
    CHECK_EQ_U64(lod->stats.columns_built, columns);
    CHECK_EQ_U64(lod->stats.columns_no_room, 0u);
    uint64_t voxels = 0u;
    ld_grids_check(t, &rig, &source, &voxels);
    CHECK(voxels > 0u);
    ld_end(t, &rig);
}

TEST(a_level_follows_the_eye_and_asks_only_for_what_is_new) {
    sk_rig_t rig;
    const ld_source_t source = {LD_GROUND_HILLS, {0}};
    float64_t eye[3] = {8.5f64, 8.5f64, 300.5f64};
    REQUIRE(ld_begin(t, &rig, &source, LD_HAND_RADIUS, 40u, LD_FOG_FAR, LD_PLAIN, eye));
    render_lod_t *const lod = rig.render->borrowed_lod;
    const render_lod_level_t *const finest = &lod->levels[0];
    const uint32_t shown = finest->shown_side;
    const uint32_t side = finest->buffers.window.side;
    const uint64_t built_before = lod->stats.columns_built;
    /* One column of the finest level on, and no further in the others. */
    const float64_t column = (float64_t)(16u << finest->shift);
    eye[0] += column;
    REQUIRE(render_lod_frame(lod, eye) == GPU_STATUS_OK);
    /* What it steps towards is there already: rays never see less than they did, and on the very frame it moved. */
    CHECK_EQ_U64(finest->shown_side, shown);
    CHECK_EQ_I64(finest->center_x, render_lod_column_of(eye[0], finest->shift));
    CHECK_EQ_I64(finest->shown_min_x, finest->center_x - (int32_t)(shown / 2u));
    REQUIRE(ld_settle(t, &rig, eye));
    CHECK_EQ_U64(lod->stats.columns_built - built_before, side);
    uint64_t voxels = 0u;
    ld_grids_check(t, &rig, &source, &voxels);

    /* Sideways and back: what is held stays the same size however long it goes on. */
    const uint64_t slabs = finest->buffers.slab_bytes;
    for (uint32_t i = 0u; i < 24u; i++) {
        eye[1] += i < 12u ? column : -column;
        REQUIRE(ld_settle(t, &rig, eye));
    }
    CHECK_EQ_U64(finest->buffers.slab_bytes, slabs);
    CHECK_EQ_U64(lod->stats.columns_no_room, 0u);
    ld_grids_check(t, &rig, &source, &voxels);

    /* Far away at once: nothing old may show, and then all of it comes. */
    eye[0] += 1000000.0f64;
    eye[1] -= 700000.0f64;
    REQUIRE(render_lod_frame(lod, eye) == GPU_STATUS_OK);
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        CHECK_EQ_U64(lod->levels[i].shown_side, 0u);
    }
    REQUIRE(ld_settle(t, &rig, eye));
    for (uint32_t i = 0u; i < lod->level_count; i++) {
        CHECK_EQ_U64(lod->levels[i].shown_side,
                     2u * ((uint32_t)lod->levels[i].radius - RENDER_LOD_MARGIN_COLUMNS) + 1u);
    }
    ld_grids_check(t, &rig, &source, &voxels);
    ld_end(t, &rig);
}

/* ---- Rays ------------------------------------------------------------------------------------------------------------------ */

static float64_t ld_random(uint64_t *state) {
    *state = *state * 6364136223846793005ull + 1442695040888963407ull;
    return (float64_t)(*state >> 11) / 9007199254740992.0f64;
}

TEST(rays_agree_with_a_march_through_every_level) {
    sk_rig_t rig;
    const ld_source_t source = {LD_GROUND_HILLS, {0}};
    const float64_t place[3] = {100.25f64, -60.75f64, 240.5f64};
    REQUIRE(ld_begin(t, &rig, &source, LD_HAND_RADIUS, 128u, LD_FOG_FAR, LD_PLAIN, place));
    device_memory_t *const memory = &rig.bridge->device_memory;
    device_block_t ray_block = {0}, hit_block = {0};
    REQUIRE(device_memory_alloc(memory, DEVICE_POOL_FRAMES, LD_RANDOM_RAYS * sizeof(traverse_ray_t), "rays",
                                &ray_block) == GPU_STATUS_OK);
    REQUIRE(device_memory_alloc(memory, DEVICE_POOL_FRAMES, LD_RANDOM_RAYS * sizeof(lod_hit_t), "hits", &hit_block) ==
            GPU_STATUS_OK);
    traverse_ray_t *const rays = test_alloc(t, LD_RANDOM_RAYS * sizeof(traverse_ray_t));
    lod_hit_t *const hits = test_alloc(t, LD_RANDOM_RAYS * sizeof(lod_hit_t));
    uint64_t state = 77u;
    for (uint32_t i = 0u; i < LD_RANDOM_RAYS; i++) {
        const float64_t yaw = ld_random(&state) * 6.283185307f64;
        /* Most near the horizon, where a ray goes through all the levels. */
        const float64_t pitch = -0.25f64 * pow(ld_random(&state), 2.0f64) + 0.01f64;
        const float32_t direction[3] = {(float32_t)(cos(yaw) * cos(pitch)), (float32_t)(sin(yaw) * cos(pitch)),
                                        (float32_t)sin(pitch)};
        memset(&rays[i], 0, sizeof(rays[i]));
        for (uint32_t a = 0u; a < 3u; a++) {
            const float64_t whole = floor(place[a]);
            rays[i].eye_cell[a] = (int32_t)whole;
            rays[i].eye_frac[a] = (float32_t)(place[a] - whole);
            rays[i].direction[a] = direction[a];
        }
    }
    REQUIRE(device_memory_write(memory, &ray_block, 0u, rays, LD_RANDOM_RAYS * sizeof(traverse_ray_t)) ==
            GPU_STATUS_OK);
    lod_scene_t scene;
    render_lod_scene(rig.render->borrowed_lod, &scene);
    gpu_error_t error;
    memset(&error, 0, sizeof(error));
    REQUIRE(lod_rays(&error, &scene, nullptr, nullptr, &ray_block, &hit_block, LD_RANDOM_RAYS) == GPU_STATUS_OK);
    REQUIRE(traverse_wait(&error) == GPU_STATUS_OK);
    REQUIRE(device_memory_read(memory, &hit_block, 0u, hits, LD_RANDOM_RAYS * sizeof(lod_hit_t)) == GPU_STATUS_OK);
    const float64_t eye[3] = {place[0], place[1], place[2] + 256.0f64};
    uint64_t met = 0u, disagree = 0u;
    for (uint32_t i = 0u; i < LD_RANDOM_RAYS; i++) {
        float64_t direction[3], length = 0.0f64;
        for (uint32_t a = 0u; a < 3u; a++) {
            direction[a] = (float64_t)rays[i].direction[a];
            length += direction[a] * direction[a];
        }
        for (uint32_t a = 0u; a < 3u; a++) {
            direction[a] /= sqrt(length);
        }
        ld_marched_t marched;
        ld_march(&source, &scene, nullptr, eye, direction, 0.0f64, &marched);
        met += marched.material != LOD_NO_MATERIAL ? 1u : 0u;
        const bool same = marched.material == hits[i].material && marched.face == hits[i].face &&
                          fabs(marched.t - (float64_t)hits[i].t) < 0.01f64 + marched.t * 1e-5f64;
        if (!same) {
            disagree++;
            if (disagree <= 8u) {
                printf("  ray %" PRIu32 ": march t %.4f material %" PRIu32 " face %" PRIu32
                       ", card t %.4f material %" PRIu32 " face %" PRIu32 "\n",
                       i, (libc_double_t)marched.t, marched.material, marched.face, (libc_double_t)hits[i].t,
                       (uint32_t)hits[i].material, (uint32_t)hits[i].face);
            }
        }
    }
    printf("  %" PRIu32 " rays, %" PRIu64 " met ground, %" PRIu64 " disagree\n", LD_RANDOM_RAYS, met, disagree);
    CHECK(met > LD_RANDOM_RAYS / 3u);
    /* Two float32_t rounding one tie each way can part at a voxel's edge, and little else should. */
    CHECK(disagree * 1000u <= LD_RANDOM_RAYS);
    REQUIRE(device_memory_free(memory, &hit_block) == GPU_STATUS_OK);
    REQUIRE(device_memory_free(memory, &ray_block) == GPU_STATUS_OK);
    ld_end(t, &rig);
}

TEST(the_sun_is_stopped_by_what_stands_in_its_way) {
    sk_rig_t rig;
    const ld_source_t source = {LD_GROUND_WALL, {0}};
    const float64_t place[3] = {8.5f64, 8.5f64, 300.5f64};
    REQUIRE(ld_begin(t, &rig, &source, 1u, 128u, LD_FOG_FAR, LD_PLAIN, place));
    device_memory_t *const memory = &rig.bridge->device_memory;
    enum : uint32_t { RAYS = 256u };
    device_block_t ray_block = {0}, hit_block = {0};
    REQUIRE(device_memory_alloc(memory, DEVICE_POOL_FRAMES, RAYS * sizeof(traverse_ray_t), "rays", &ray_block) ==
            GPU_STATUS_OK);
    REQUIRE(device_memory_alloc(memory, DEVICE_POOL_FRAMES, RAYS * sizeof(lod_hit_t), "hits", &hit_block) ==
            GPU_STATUS_OK);
    traverse_ray_t *const rays = test_alloc(t, RAYS * sizeof(traverse_ray_t));
    lod_hit_t *const hits = test_alloc(t, RAYS * sizeof(lod_hit_t));
    /* Straight down onto the ground, along x from 500 to 1000: from the east at 45 degrees a wall 200 high ends its shadow at x = 824. */
    for (uint32_t i = 0u; i < RAYS; i++) {
        memset(&rays[i], 0, sizeof(rays[i]));
        rays[i].eye_cell[0] = 500 + (int32_t)(i * 500u / RAYS);
        rays[i].eye_cell[1] = 8;
        rays[i].eye_cell[2] = 300;
        rays[i].eye_frac[0] = 0.5f32;
        rays[i].eye_frac[1] = 0.5f32;
        rays[i].direction[0] = 1e-5f32;
        rays[i].direction[1] = 1e-5f32;
        rays[i].direction[2] = -1.0f32;
    }
    REQUIRE(device_memory_write(memory, &ray_block, 0u, rays, RAYS * sizeof(traverse_ray_t)) == GPU_STATUS_OK);
    lod_scene_t scene;
    render_lod_scene(rig.render->borrowed_lod, &scene);
    const shade_scene_t *const shade = &rig.render->frame.scene;
    lod_light_t light = {.borrowed_textures = shade->borrowed_textures,
                         .borrowed_faces = shade->borrowed_faces,
                         .sun_direction = {0.70710678f32, 0.0f32, 0.70710678f32},
                         .material_count = shade->material_count,
                         .shadows = 1u};
    gpu_error_t error;
    memset(&error, 0, sizeof(error));
    REQUIRE(lod_rays(&error, &scene, &light, nullptr, &ray_block, &hit_block, RAYS) == GPU_STATUS_OK);
    REQUIRE(traverse_wait(&error) == GPU_STATUS_OK);
    REQUIRE(device_memory_read(memory, &hit_block, 0u, hits, RAYS * sizeof(lod_hit_t)) == GPU_STATUS_OK);
    uint64_t lit = 0u, shadowed = 0u, wrong = 0u;
    for (uint32_t i = 0u; i < RAYS; i++) {
        const int32_t x = rays[i].eye_cell[0];
        CHECK_EQ_U64(hits[i].face, TRAVERSE_FACE_POS_Z);
        /* The levels' voxels leave the wall's own edge up to a coarse voxel out, so a band each side says nothing. */
        if (x < 824 - 64 && hits[i].sun != LOD_SUN_SEEN) {
            wrong++;
        }
        if (x > 824 + 64 && hits[i].sun != LOD_SUN_HIDDEN) {
            wrong++;
        }
        lit += hits[i].sun == LOD_SUN_SEEN ? 1u : 0u;
        shadowed += hits[i].sun == LOD_SUN_HIDDEN ? 1u : 0u;
    }
    printf("  %" PRIu64 " in the sun, %" PRIu64 " in the wall's shadow, %" PRIu64 " wrong\n", lit, shadowed, wrong);
    CHECK_EQ_U64(wrong, 0u);
    CHECK(lit > 0u);
    CHECK(shadowed > 0u);
    /* The sun behind the ground: a top gets none of it, and no ray has to say so. */
    light.sun_direction[2] = -0.70710678f32;
    REQUIRE(lod_rays(&error, &scene, &light, nullptr, &ray_block, &hit_block, RAYS) == GPU_STATUS_OK);
    REQUIRE(traverse_wait(&error) == GPU_STATUS_OK);
    REQUIRE(device_memory_read(memory, &hit_block, 0u, hits, RAYS * sizeof(lod_hit_t)) == GPU_STATUS_OK);
    for (uint32_t i = 0u; i < RAYS; i++) {
        CHECK_EQ_U64(hits[i].sun, LOD_SUN_SEEN);
    }
    REQUIRE(device_memory_free(memory, &hit_block) == GPU_STATUS_OK);
    REQUIRE(device_memory_free(memory, &ray_block) == GPU_STATUS_OK);
    ld_end(t, &rig);
}

/* ---- The picture ----------------------------------------------------------------------------------------------------------- */

/* A face's mean colour, as render_frame has it for the card. */
static const uint8_t *ld_mean_of(const sk_rig_t *rig, uint32_t material, uint32_t face) {
    uint32_t id = 0u;
    texture_info_t info;
    const uint16_t texture = block_defs[material].faces[face];
    if (texture == BLOCK_NO_TEXTURE || !texture_find(rig->texture, block_texture_names[texture], &id) ||
        !texture_info_get(rig->texture, id, &info)) {
        return nullptr;
    }
    return info.borrowed_entry->mean_rgba;
}

TEST(what_full_detail_misses_goes_on_to_the_far_field_and_shows_its_colour) {
    sk_rig_t rig;
    const ld_source_t source = {LD_GROUND_HILLS, {0}};
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 8.5f64, 220.5f64, 0.3f32, -0.25f32);
    REQUIRE(ld_begin(t, &rig, &source, LD_HAND_RADIUS, 64u, LD_FOG_FAR, LD_PLAIN, eye.position));
    const render_frame_t *const frame = &rig.render->frame;
    const uint8_t *const with = ld_picture(t, &rig, &eye);
    const shade_pixel_t *const shaded = ld_shaded(t, &rig);
    const lod_hit_t *const hits = ld_hits(t, &rig);
    traverse_camera_t view;
    render_camera_to_traverse(&eye, LD_WIDTH, LD_HEIGHT, &view);
    lod_scene_t scene;
    render_lod_scene(rig.render->borrowed_lod, &scene);
    const float64_t origin[3] = {eye.position[0], eye.position[1], eye.position[2] + 256.0f64};
    uint64_t far = 0u, sky = 0u, wrong_colour = 0u, wrong_ray = 0u, in_full_detail = 0u;
    for (uint32_t y = 0u; y < LD_HEIGHT; y++) {
        for (uint32_t x = 0u; x < LD_WIDTH; x++) {
            const uint64_t i = (uint64_t)y * LD_WIDTH + x;
            float64_t way[3], length = 0.0f64;
            const float64_t u = 2.0f64 * ((float64_t)x + 0.5f64) / (float64_t)LD_WIDTH - 1.0f64;
            const float64_t v = 1.0f64 - 2.0f64 * ((float64_t)y + 0.5f64) / (float64_t)LD_HEIGHT;
            for (uint32_t a = 0u; a < 3u; a++) {
                way[a] = (float64_t)view.forward[a] + u * (float64_t)view.right[a] + v * (float64_t)view.up[a];
                length += way[a] * way[a];
            }
            float32_t direction[3];
            for (uint32_t a = 0u; a < 3u; a++) {
                way[a] /= sqrt(length);
                direction[a] = (float32_t)way[a];
            }
            ld_marched_t marched;
            ld_march(&source, &scene, &rig.render->buffers, origin, way, 0.0f64, &marched);
            const bool same = marched.material == hits[i].material &&
                              (marched.material == LOD_NO_MATERIAL ||
                               (marched.face == hits[i].face &&
                                fabs(marched.t - (float64_t)hits[i].t) < 0.05f64 + marched.t * 1e-4f64));
            wrong_ray += same ? 0u : 1u;
            if (hits[i].material == LOD_NO_MATERIAL) {
                CHECK(shaded[i].t >= TRAVERSE_COARSE_SKY);
                sky++;
                continue;
            }
            far++;
            /* Nothing of the far field is drawn in what full detail holds. */
            const float64_t at_x = origin[0] + way[0] * ((float64_t)hits[i].t + 1e-3f64);
            const float64_t at_y = origin[1] + way[1] * ((float64_t)hits[i].t + 1e-3f64);
            in_full_detail += ld_fine_ready(&rig.render->buffers, (int32_t)floor(at_x), (int32_t)floor(at_y)) ? 1u : 0u;
            float32_t sky_colour[3], colour[3];
            post_sky(&frame->post, direction, sky_colour);
            const uint8_t *const mean = ld_mean_of(&rig, hits[i].material, hits[i].face);
            REQUIRE(mean != nullptr);
            shade_far_colour(&frame->scene.sun, mean, false, false, false, &hits[i], direction, sky_colour, colour);
            bool near = fabs((float64_t)shaded[i].t - (float64_t)hits[i].t) < 1e-3f64;
            for (uint32_t a = 0u; a < 3u; a++) {
                near = near && fabsf(shaded[i].rgb[a] - colour[a]) < 1e-4f32;
            }
            wrong_colour += near ? 0u : 1u;
        }
    }
    printf("  %" PRIu64 " pixels on far ground, %" PRIu64 " on sky, %" PRIu64 " against the march, %" PRIu64
           " with the wrong colour\n",
           far, sky, wrong_ray, wrong_colour);
    CHECK(far > LD_PIXELS / 4u);
    CHECK(sky > LD_PIXELS / 10u);
    CHECK(wrong_ray * 200u <= LD_PIXELS);
    CHECK_EQ_U64(wrong_colour, 0u);
    CHECK_EQ_U64(in_full_detail, 0u);

    /* The setting off, and only then, and the world's own pixels as they were. */
    render_frame_features_set(&rig.render->frame, LD_PLAIN & ~(uint32_t)SHADE_FEATURE_LOD);
    const uint8_t *const without = ld_picture(t, &rig, &eye);
    uint64_t differ = 0u;
    for (uint64_t i = 0u; i < LD_PIXELS; i++) {
        uint32_t a = 0u, b = 0u;
        memcpy(&a, with + i * 4u, 4u);
        memcpy(&b, without + i * 4u, 4u);
        if (a != b) {
            differ++;
            CHECK(hits[i].material != LOD_NO_MATERIAL);
        }
    }
    CHECK(differ > LD_PIXELS / 4u);
    /* Off, the picture is as with no far field at all, to the byte. */
    render_close_lod(rig.render);
    render_frame_features_set(&rig.render->frame, LD_PLAIN);
    const uint8_t *const none = ld_picture(t, &rig, &eye);
    CHECK_MEM_EQ(none, without, post_xrgb_bytes(LD_WIDTH, LD_HEIGHT));
    ld_end(t, &rig);
}

/* How many far hits lie in a column full detail holds, or with in_that with that one column, which it may not. */
static uint64_t ld_far_in_columns(test_context_t *t, sk_rig_t *rig, const render_camera_t *eye, const lod_hit_t *hits,
                                  int32_t column_x, int32_t column_y, bool in_that) {
    (void)t;
    traverse_camera_t view;
    render_camera_to_traverse(eye, LD_WIDTH, LD_HEIGHT, &view);
    uint64_t count = 0u;
    for (uint32_t y = 0u; y < LD_HEIGHT; y++) {
        for (uint32_t x = 0u; x < LD_WIDTH; x++) {
            const lod_hit_t *const hit = &hits[(uint64_t)y * LD_WIDTH + x];
            if (hit->material == LOD_NO_MATERIAL) {
                continue;
            }
            const float64_t u = 2.0f64 * ((float64_t)x + 0.5f64) / (float64_t)LD_WIDTH - 1.0f64;
            const float64_t v = 1.0f64 - 2.0f64 * ((float64_t)y + 0.5f64) / (float64_t)LD_HEIGHT;
            float64_t way[3], length = 0.0f64;
            for (uint32_t a = 0u; a < 3u; a++) {
                way[a] = (float64_t)view.forward[a] + u * (float64_t)view.right[a] + v * (float64_t)view.up[a];
                length += way[a] * way[a];
            }
            length = sqrt(length);
            const float64_t t_along = (float64_t)hit->t + 1e-3f64;
            const int32_t cell_x = (int32_t)floor(eye->position[0] + way[0] / length * t_along);
            const int32_t cell_y = (int32_t)floor(eye->position[1] + way[1] / length * t_along);
            if (in_that) {
                count += (cell_x >> 4) == column_x && (cell_y >> 4) == column_y ? 1u : 0u;
            } else {
                count += ld_fine_ready(&rig->render->buffers, cell_x, cell_y) ? 1u : 0u;
            }
        }
    }
    return count;
}

/* Full detail in front of the far field, and a gap in it, and things to see through. */
TEST(full_detail_stands_in_front_and_a_column_missing_from_it_shows_the_far_field) {
    sk_rig_t rig;
    const ld_source_t source = {LD_GROUND_FLAT, {0}};
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 8.5f64, 40.5f64, 0.0f32, -0.25f32);
    REQUIRE(ld_begin(t, &rig, &source, LD_HAND_RADIUS, 40u, LD_FOG_FAR, LD_PLAIN, eye.position));
    render_frame_features_set(&rig.render->frame, LD_PLAIN & ~(uint32_t)SHADE_FEATURE_LOD);
    const uint8_t *const bare = ld_picture(t, &rig, &eye);
    render_frame_features_set(&rig.render->frame, LD_PLAIN);
    /* Looking down at what full detail holds, empty and all there: the ray goes through it, and the coarse ground that lies under it is not
     * looked at, so nothing but what is past the columns can change. */
    const uint8_t *const open = ld_picture(t, &rig, &eye);
    const lod_hit_t *const open_hits = ld_hits(t, &rig);
    uint64_t seen = 0u;
    for (uint64_t i = 0u; i < LD_PIXELS; i++) {
        if (open_hits[i].material != LOD_NO_MATERIAL) {
            seen++;
            CHECK(open_hits[i].t > 8.0f64);
        }
    }
    CHECK(seen > 0u);
    CHECK(memcmp(open, bare, post_xrgb_bytes(LD_WIDTH, LD_HEIGHT)) != 0);
    CHECK_EQ_U64(ld_far_in_columns(t, &rig, &eye, open_hits, 0, 0, false), 0u);

    /* One column gone from full detail, as one not yet streamed in is: the far field shows there, and nowhere else that holds a column. */
    world_buffers_t *const fine = &rig.render->buffers;
    uint32_t gone_slot = 0u;
    REQUIRE(world_buffers_slot_of(&fine->window, 2, 0, &gone_slot));
    world_buffers_column_clear(fine, gone_slot);
    REQUIRE(world_buffers_flush(fine) == GPU_STATUS_OK);
    (void)ld_picture(t, &rig, &eye);
    const lod_hit_t *const gone_hits = ld_hits(t, &rig);
    const uint64_t in_gap = ld_far_in_columns(t, &rig, &eye, gone_hits, 2, 0, true);
    printf("  %" PRIu64 " far pixels in a column gone from full detail\n", in_gap);
    CHECK(in_gap > 0u);
    CHECK_EQ_U64(ld_far_in_columns(t, &rig, &eye, gone_hits, 0, 0, false), 0u);

    /* A wall of real blocks hides it all where it stands, to the byte, and no far ray goes on past what is opaque. */
    REQUIRE(ld_put(t, &rig, 20, -16, -2, 21, 32, 90, BLOCK_ID_STONE));
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_features_set(&rig.render->frame, LD_PLAIN & ~(uint32_t)SHADE_FEATURE_LOD);
    const uint8_t *const wall_bare = ld_picture(t, &rig, &eye);
    render_frame_features_set(&rig.render->frame, LD_PLAIN);
    const uint8_t *const wall = ld_picture(t, &rig, &eye);
    const lod_hit_t *const wall_hits = ld_hits(t, &rig);
    uint64_t behind_wall = 0u;
    for (uint64_t i = 0u; i < LD_PIXELS; i++) {
        uint32_t a = 0u, b = 0u;
        memcpy(&a, wall + i * 4u, 4u);
        memcpy(&b, wall_bare + i * 4u, 4u);
        if (wall_hits[i].material == LOD_NO_MATERIAL) {
            CHECK_EQ_U64(a, b);
            behind_wall++;
        }
    }
    CHECK_EQ_U64(behind_wall, LD_PIXELS);

    /* A pane of glass there: what shows through its holes is the far field. */
    REQUIRE(ld_put(t, &rig, 20, -16, -2, 21, 32, 90, BLOCK_ID_GLASS));
    REQUIRE(sk_hand_world_sync(t, &rig));
    const uint8_t *const glass = ld_picture(t, &rig, &eye);
    const lod_hit_t *const glass_hits = ld_hits(t, &rig);
    render_frame_features_set(&rig.render->frame, LD_PLAIN & ~(uint32_t)SHADE_FEATURE_LOD);
    const uint8_t *const glass_bare = ld_picture(t, &rig, &eye);
    render_frame_features_set(&rig.render->frame, LD_PLAIN);
    uint64_t through = 0u, shown_through = 0u;
    for (uint64_t i = 0u; i < LD_PIXELS; i++) {
        through += glass_hits[i].material != LOD_NO_MATERIAL ? 1u : 0u;
        shown_through += memcmp(glass + i * 4u, glass_bare + i * 4u, 3u) != 0 ? 1u : 0u;
    }
    CHECK(shown_through > seen / 4u);
    /* A wall of water there: what a ray meets behind a surface that has already been drawn is the far field as well. */
    REQUIRE(ld_put(t, &rig, 20, -16, -2, 21, 32, 90, BLOCK_ID_WATER));
    REQUIRE(sk_hand_world_sync(t, &rig));
    const uint8_t *const water = ld_picture(t, &rig, &eye);
    render_frame_features_set(&rig.render->frame, LD_PLAIN & ~(uint32_t)SHADE_FEATURE_LOD);
    const uint8_t *const water_bare = ld_picture(t, &rig, &eye);
    render_frame_features_set(&rig.render->frame, LD_PLAIN);
    uint64_t through_water = 0u;
    for (uint64_t i = 0u; i < LD_PIXELS; i++) {
        through_water += memcmp(water + i * 4u, water_bare + i * 4u, 3u) != 0 ? 1u : 0u;
    }
    printf("  %" PRIu64 " pixels show it through water\n", through_water);
    CHECK(through_water > seen / 4u);
    printf("  far pixels: %" PRIu64 " open, %" PRIu64 " behind a wall of stone would have been, %" PRIu64
           " through glass\n",
           seen, behind_wall, through);
    CHECK(through >= seen / 2u);
    ld_end(t, &rig);
}

/* Fog takes the far field as it takes anything: past its end a pixel is the sky's, to the byte. */
TEST(fog_takes_the_far_field_and_water_shows_the_sky) {
    sk_rig_t rig;
    const ld_source_t source = {LD_GROUND_LAKE, {0}};
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 8.5f64, 40.5f64, 0.0f32, -0.05f32);
    REQUIRE(ld_begin(t, &rig, &source, LD_HAND_RADIUS, 64u, 300.0f32, SHADE_FEATURES_ALL, eye.position));
    render_frame_time_set(&rig.render->frame, 0.4f32);
    const uint8_t *const with = ld_picture(t, &rig, &eye);
    const lod_hit_t *const hits = ld_hits(t, &rig);
    const shade_pixel_t *const shaded = ld_shaded(t, &rig);
    render_frame_features_set(&rig.render->frame, SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_LOD);
    const uint8_t *const without = ld_picture(t, &rig, &eye);
    uint64_t fogged = 0u, water = 0u;
    for (uint64_t i = 0u; i < LD_PIXELS; i++) {
        if (hits[i].material != LOD_NO_MATERIAL && hits[i].t >= 300.0f32) {
            CHECK_MEM_EQ(with + i * 4u, without + i * 4u, 3u);
            fogged++;
        }
        if (hits[i].material == BLOCK_ID_WATER) {
            CHECK_EQ_U64(hits[i].face, TRAVERSE_FACE_POS_Z);
            water += shaded[i].t == hits[i].t ? 1u : 0u;
        }
    }
    printf("  %" PRIu64 " pixels of water, %" PRIu64 " all fog\n", water, fogged);
    CHECK(fogged > 0u);
    CHECK(water > 0u);
    ld_end(t, &rig);
}

/* Far water's sides in a picture, against what is three rows either way: how many there are, and how many channels are darker than both
 * by more than a texel's own change. From the air, or from under near water. */
static void ld_sea_sides(const uint8_t *picture, const lod_hit_t *hits, const shade_pixel_t *shaded, bool from_air,
                         uint64_t *out_sides, uint64_t *out_dark) {
    *out_sides = 0u;
    *out_dark = 0u;
    for (uint32_t y = 3u; y + 3u < LD_HEIGHT; y++) {
        for (uint32_t x = 0u; x < LD_WIDTH; x++) {
            const uint64_t i = (uint64_t)y * LD_WIDTH + x;
            if (hits[i].material != BLOCK_ID_WATER || hits[i].face == TRAVERSE_FACE_POS_Z ||
                (shaded[i].t == hits[i].t) != from_air) {
                continue;
            }
            *out_sides += 1u;
            const uint8_t *const bytes = picture + i * 4u;
            const uint8_t *const above = bytes - 3u * (uint64_t)LD_WIDTH * 4u;
            const uint8_t *const below = bytes + 3u * (uint64_t)LD_WIDTH * 4u;
            for (uint32_t c = 0u; c < 3u; c++) {
                const uint32_t low = above[c] < below[c] ? above[c] : below[c];
                *out_dark += (uint32_t)bytes[c] + LD_SEA_STEP < low ? 1u : 0u;
            }
        }
    }
}

/* Where a sea crosses from full detail to the far field there is no edge to see. Found by looking at a real run, which showed a dashed dark
 * line there and a sea of another colour past it. A far surface is what a near one that deep comes to, and a coarse voxel's side, from the
 * air or from under near water, is only more water. */
TEST(a_sea_goes_on_past_full_detail_with_no_edge) {
    sk_rig_t rig;
    const ld_source_t source = {LD_GROUND_LAKE, {0}};
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 8.5f64, 40.5f64, 0.0f32, -0.3f32);
    REQUIRE(ld_begin(t, &rig, &source, LD_HAND_RADIUS, 40u, LD_FOG_FAR, SHADE_FEATURES_ALL, eye.position));
    render_frame_time_set(&rig.render->frame, 0.4f32);
    for (int32_t cy = -(int32_t)LD_HAND_RADIUS; cy <= (int32_t)LD_HAND_RADIUS; cy++) {
        for (int32_t cx = -(int32_t)LD_HAND_RADIUS; cx <= (int32_t)LD_HAND_RADIUS; cx++) {
            if (world_map_column_at(&rig.world->map, cx, cy) != nullptr) {
                /* Deeper than SHADE_WATER_SEEN_BLOCKS, so that no ray of it sees a bottom, and the far surface has none. */
                REQUIRE(ld_put(t, &rig, cx * 16, cy * 16, -60, cx * 16 + 16, cy * 16 + 16, 0, BLOCK_ID_WATER));
            }
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    const uint8_t *const picture = ld_picture(t, &rig, &eye);
    const lod_hit_t *const hits = ld_hits(t, &rig);
    const shade_pixel_t *const shaded = ld_shaded(t, &rig);

    /* Row by row, the far surface against the near one, on the rows that have both. */
    uint64_t rows = 0u, worst = 0u;
    for (uint32_t y = 0u; y < LD_HEIGHT; y++) {
        uint64_t near_sum[3] = {0u, 0u, 0u}, far_sum[3] = {0u, 0u, 0u}, near_count = 0u, far_count = 0u;
        for (uint32_t x = 0u; x < LD_WIDTH; x++) {
            const uint64_t i = (uint64_t)y * LD_WIDTH + x;
            const bool is_far = hits[i].material == BLOCK_ID_WATER && shaded[i].t == hits[i].t;
            const bool is_near = shaded[i].t < TRAVERSE_COARSE_SKY && shaded[i].t < hits[i].t;
            for (uint32_t c = 0u; c < 3u; c++) {
                far_sum[c] += is_far ? picture[i * 4u + c] : 0u;
                near_sum[c] += is_near ? picture[i * 4u + c] : 0u;
            }
            far_count += is_far ? 1u : 0u;
            near_count += is_near ? 1u : 0u;
        }
        if (near_count >= 8u && far_count >= 8u) {
            rows++;
            for (uint32_t c = 0u; c < 3u; c++) {
                const uint64_t a = near_sum[c] / near_count, b = far_sum[c] / far_count;
                const uint64_t apart = a > b ? a - b : b - a;
                worst = apart > worst ? apart : worst;
            }
        }
    }
    uint64_t under_sides = 0u, under_dark = 0u;
    ld_sea_sides(picture, hits, shaded, false, &under_sides, &under_dark);

    /* Near water a block lower than the far level rounds it, as real sea is: the far voxels' sides now stand in the air. */
    for (int32_t cy = -(int32_t)LD_HAND_RADIUS; cy <= (int32_t)LD_HAND_RADIUS; cy++) {
        for (int32_t cx = -(int32_t)LD_HAND_RADIUS; cx <= (int32_t)LD_HAND_RADIUS; cx++) {
            if (world_map_column_at(&rig.world->map, cx, cy) != nullptr) {
                REQUIRE(ld_put(t, &rig, cx * 16, cy * 16, -1, cx * 16 + 16, cy * 16 + 16, 0, BLOCK_ID_AIR));
            }
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    const uint8_t *const lower = ld_picture(t, &rig, &eye);
    const lod_hit_t *const lower_hits = ld_hits(t, &rig);
    const shade_pixel_t *const lower_shaded = ld_shaded(t, &rig);
    uint64_t air_sides = 0u, air_dark = 0u;
    ld_sea_sides(lower, lower_hits, lower_shaded, true, &air_sides, &air_dark);

    printf("  %" PRIu64 " rows with both, at worst %" PRIu64 " bytes apart. Far sides under near water %" PRIu64
           ", dark %" PRIu64 ". From the air %" PRIu64 ", dark %" PRIu64 "\n",
           rows, worst, under_sides, under_dark, air_sides, air_dark);
    CHECK(rows > 0u);
    /* The means of real texels against the one mean of the texture, with colour and cover going together in its texels. It was 28. */
    CHECK(worst <= 10u);
    CHECK(under_sides > 0u);
    CHECK_EQ_U64(under_dark, 0u);
    CHECK(air_sides > 0u);
    CHECK_EQ_U64(air_dark, 0u);
    ld_end(t, &rig);
}

TEST(the_far_field_stands_for_real_terrain) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = 5u,
                                  .width = LD_WIDTH,
                                  .height = LD_HEIGHT,
                                  .features_off = 0u,
                                  .god_rays = 0u,
                                  .show_date = 0u,
                                  .full_detail = 2u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    render_lod_t *const lod = rig.render->borrowed_lod;
    REQUIRE(lod != nullptr);
    CHECK_EQ_U64(rig.world->map.view_radius, 2u);
    /* The fog goes with what is drawn, a chunk short of it. */
    CHECK_NEAR(rig.render->frame.fog_end, 4.0f32 * 16.0f32, 1e-3f32);
    CHECK_EQ_U64(lod->stats.columns_no_room, 0u);
    printf("  %" PRIu64 " columns of real terrain in %.2f s of workers' time, %.1f MiB\n", lod->stats.columns_built,
           (libc_double_t)((float64_t)lod->stats.build_ns * 1e-9f64),
           (libc_double_t)((float64_t)lod->stats.bytes_sent / 1048576.0f64));
    /* Against what the terrain itself makes, voxel by voxel, at places all over. */
    const render_lod_level_t *const level = &lod->levels[0];
    device_block_t scratch = {0};
    enum : uint32_t { VOXELS = 4096u };
    REQUIRE(device_memory_alloc(&rig.bridge->device_memory, DEVICE_POOL_FRAMES, VOXELS * WORLD_BUFFERS_PROBE_BYTES,
                                "probe", &scratch) == GPU_STATUS_OK);
    world_buffers_probe_query_t *const queries = test_alloc(t, VOXELS * sizeof(world_buffers_probe_query_t));
    world_buffers_probe_result_t *const results = test_alloc(t, VOXELS * sizeof(world_buffers_probe_result_t));
    uint16_t *const wanted = test_alloc(t, VOXELS * sizeof(uint16_t));
    uint16_t *const column = test_alloc(t, RENDER_LOD_HEIGHT_BLOCKS * sizeof(uint16_t));
    uint16_t *const cells = test_alloc(t, RENDER_LOD_HEIGHT_BLOCKS * sizeof(uint16_t));
    uint64_t state = 5u, surface = 0u;
    uint32_t filled = 0u;
    while (filled < VOXELS) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const int32_t x = level->shown_min_x * 16 + (int32_t)((state >> 20) % (uint64_t)(level->shown_side * 16u));
        const int32_t y = level->shown_min_y * 16 + (int32_t)((state >> 36) % (uint64_t)(level->shown_side * 16u));
        const int32_t size = 1 << level->shift;
        render_lod_sample_terrain(rig.terrain, x * size + size / 2, y * size + size / 2, level->shift, column);
        const int32_t top = render_lod_cells_from_column(column, level->shift, 1u, cells);
        for (int32_t z = top - 8; z <= top + 8 && filled < VOXELS; z++) {
            queries[filled] = (world_buffers_probe_query_t){x, y, z - (int32_t)WORLD_BUFFERS_Z_ORIGIN};
            wanted[filled] =
                z >= 0 && z < (int32_t)(RENDER_LOD_HEIGHT_BLOCKS >> level->shift) ? cells[z] : (uint16_t)BLOCK_ID_AIR;
            filled++;
        }
    }
    REQUIRE(world_buffers_probe(&lod->levels[0].buffers, &scratch, queries, results, VOXELS) == GPU_STATUS_OK);
    uint64_t wrong = 0u;
    for (uint32_t i = 0u; i < VOXELS; i++) {
        surface += wanted[i] != BLOCK_ID_AIR ? 1u : 0u;
        const bool right = wanted[i] == BLOCK_ID_AIR
                               ? results[i].state == WORLD_BUFFERS_VOXEL_EMPTY
                               : results[i].state == WORLD_BUFFERS_VOXEL_OCCUPIED && results[i].material == wanted[i];
        wrong += right ? 0u : 1u;
    }
    printf("  %" PRIu32 " voxels of real terrain looked at, %" PRIu64 " with a block in, %" PRIu64 " wrong\n",
           (uint32_t)VOXELS, surface, wrong);
    CHECK_EQ_U64(wrong, 0u);
    CHECK(surface > 0u);
    REQUIRE(device_memory_free(&rig.bridge->device_memory, &scratch) == GPU_STATUS_OK);
    game_close(game);
    /* The setting off: the world stops where full detail does, and nothing of the far field is taken. */
    game_config_t off = config;
    off.features_off = (uint16_t)SHADE_FEATURE_LOD;
    REQUIRE(game_open(game, &off));
    CHECK(rig.render->borrowed_lod == nullptr);
    CHECK_EQ_U64(rig.world->map.view_radius, 2u);
    CHECK_NEAR(rig.render->frame.fog_end, 16.0f32, 1e-3f32);
    game_close(game);
    /* Full detail reaching all of it: none, whatever the setting. */
    game_config_t near = config;
    near.full_detail = 0u;
    REQUIRE(game_open(game, &near));
    CHECK(rig.render->borrowed_lod == nullptr);
    CHECK_EQ_U64(rig.world->map.view_radius, 5u);
    game_close(game);
    sk_rig_end(t, &rig);
}

/* ---- What a frame costs the machine ---------------------------------------------------------------------------------------------- */

TEST(a_frame_with_a_far_field_takes_nothing_from_the_heap_and_refuses_what_is_too_small) {
    sk_rig_t rig;
    const ld_source_t source = {LD_GROUND_FLAT, {0}};
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 8.5f64, 40.5f64, 0.0f32, -0.1f32);
    REQUIRE(ld_begin(t, &rig, &source, LD_HAND_RADIUS, 40u, LD_FOG_FAR, LD_PLAIN, eye.position));
    render_lod_t *const lod = rig.render->borrowed_lod;
    heap_counters_t before, after;
    REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    heap_counters_read(&before);
    for (uint32_t i = 0u; i < 8u; i++) {
        /* A frame, moving, with columns on their way. */
        eye.position[0] += 40.0f64;
        REQUIRE(render_lod_frame(lod, eye.position) == GPU_STATUS_OK);
        REQUIRE(sk_draw(t, &rig, &eye) != nullptr);
    }
    REQUIRE(ld_settle(t, &rig, eye.position));
    heap_counters_read(&after);
    CHECK_MEM_EQ(&before, &after, sizeof(before));

    gpu_error_t error;
    memset(&error, 0, sizeof(error));
    traverse_camera_t view;
    render_camera_to_traverse(&eye, LD_WIDTH, LD_HEIGHT, &view);
    lod_scene_t scene;
    render_lod_scene(lod, &scene);
    const shade_scene_t *const shade = &rig.render->frame.scene;
    const lod_light_t light = {.borrowed_textures = shade->borrowed_textures,
                               .borrowed_faces = shade->borrowed_faces,
                               .material_count = shade->material_count};
    const traverse_world_t world = {.borrowed_table = rig.render->buffers.borrowed_table,
                                    .borrowed_pool = rig.render->buffers.borrowed_pool,
                                    .window = rig.render->buffers.window};
    const traverse_camera_t empty = {0};
    CHECK_EQ_U64(lod_frame(&error, &scene, &light, &world, &empty, &rig.render->owned_hits),
                 GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(error.borrowed_what, "lod");
    CHECK_EQ_U64(lod_frame(&error, &scene, &light, &world, &view, &rig.render->owned_coarse),
                 GPU_STATUS_INVALID_ARGUMENT);
    lod_scene_t no_hits = scene;
    no_hits.borrowed_hits = nullptr;
    CHECK_EQ_U64(lod_frame(&error, &no_hits, &light, &world, &view, &rig.render->owned_hits),
                 GPU_STATUS_INVALID_ARGUMENT);
    lod_scene_t too_small = scene;
    too_small.borrowed_hits = &rig.render->owned_coarse;
    CHECK_EQ_U64(lod_frame(&error, &too_small, &light, &world, &view, &rig.render->owned_hits),
                 GPU_STATUS_INVALID_ARGUMENT);
    lod_scene_t too_many = scene;
    too_many.level_count = LOD_LEVELS_MAX + 1u;
    CHECK_EQ_U64(lod_frame(&error, &too_many, &light, &world, &view, &rig.render->owned_hits),
                 GPU_STATUS_INVALID_ARGUMENT);
    lod_scene_t too_wide = scene;
    too_wide.levels[0].shown_side = too_wide.levels[0].window.side + 1u;
    CHECK_EQ_U64(lod_frame(&error, &too_wide, &light, &world, &view, &rig.render->owned_hits),
                 GPU_STATUS_INVALID_ARGUMENT);
    lod_scene_t outside = scene;
    outside.levels[0].shown_min_x += 100000;
    CHECK_EQ_U64(lod_frame(&error, &outside, &light, &world, &view, &rig.render->owned_hits),
                 GPU_STATUS_INVALID_ARGUMENT);
    ld_end(t, &rig);
}

TEST(a_pool_with_no_room_says_so_and_shows_what_it_has) {
    sk_rig_t rig;
    render_camera_t eye;
    render_camera_start(&eye, 8.5f64, 8.5f64, 300.5f64, 0.0f32, -0.1f32);
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, LD_HAND_RADIUS, LD_WIDTH, LD_HEIGHT, LD_FOG_FAR));
    REQUIRE(sk_hand_world_sync(t, &rig));
    /* The Owner's work, by hand, for a small pool of the finest level's hills. */
    device_memory_t *const memory = &rig.bridge->device_memory;
    const ld_source_t source = {LD_GROUND_HILLS, {0}};
    const render_lod_plan_t plan = render_lod_plan(LD_HAND_RADIUS, 16u);
    device_block_t table = {0}, pool = {0};
    REQUIRE(device_memory_alloc(memory, DEVICE_POOL_VOXELS, render_lod_table_bytes(&plan, 0u), "table", &table) ==
            GPU_STATUS_OK);
    REQUIRE(device_memory_alloc(memory, DEVICE_POOL_VOXELS, 4u * WORLD_BUFFERS_PAGE_BYTES, "pool", &pool) ==
            GPU_STATUS_OK);
    const uint64_t bytes = render_lod_arena_bytes(&plan);
    arena_t arena;
    arena_init(&arena, "test_lod", test_alloc(t, bytes), bytes);
    render_lod_t *const lod = sk_aligned(t, sizeof(render_lod_t));
    render_lod_plan_t one = plan;
    one.level_count = 1u;
    REQUIRE(render_lod_init(lod, &arena, memory, rig.job, &one, &table, &pool, &rig.render->owned_hits, ld_sample,
                            &source) == GPU_STATUS_OK);
    for (uint64_t pass = 0u; pass < 100000000ull; pass++) {
        REQUIRE(render_lod_frame(lod, eye.position) == GPU_STATUS_OK);
        if (render_lod_idle(lod)) {
            break;
        }
        time_sleep_ns(20000);
    }
    REQUIRE(render_lod_idle(lod));
    REQUIRE(render_lod_frame(lod, eye.position) == GPU_STATUS_OK);
    printf("  %" PRIu64 " columns built, %" PRIu64 " with no room, %" PRIu32 " wide shown\n", lod->stats.columns_built,
           lod->stats.columns_no_room, lod->levels[0].shown_side);
    CHECK(lod->stats.columns_no_room > 0u);
    CHECK(lod->stats.columns_built > 0u);
    /* What is shown holds no gap, and so stops short of what could not fit. */
    /* Workers finish in any order, so which columns got the room differs from run to run, and that may leave none around the eye. */
    const render_lod_level_t *const level = &lod->levels[0];
    for (uint32_t dy = 0u; dy < level->shown_side; dy++) {
        for (uint32_t dx = 0u; dx < level->shown_side; dx++) {
            const int32_t column_x = level->shown_min_x + (int32_t)dx, column_y = level->shown_min_y + (int32_t)dy;
            uint32_t slot = 0u;
            REQUIRE(world_buffers_slot_of(&level->buffers.window, column_x, column_y, &slot));
            const world_buffers_column_t *const column = &level->buffers.borrowed_shadow[slot];
            CHECK(column->ready != 0u && column->chunk_x == column_x && column->chunk_y == column_y);
        }
    }
    CHECK(lod->levels[0].shown_side < 2u * ((uint32_t)lod->levels[0].radius - RENDER_LOD_MARGIN_COLUMNS) + 1u);
    render_lod_deinit(lod);
    arena_deinit(&arena);
    REQUIRE(device_memory_free(memory, &pool) == GPU_STATUS_OK);
    REQUIRE(device_memory_free(memory, &table) == GPU_STATUS_OK);
    ld_end(t, &rig);
}

#include "cuda/lod/fixtures/rest.h"

TEST_MAIN()
