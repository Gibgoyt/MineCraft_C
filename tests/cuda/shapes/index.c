/* ctest render_feature_shapes. See README.md. */
#include "harness/index.h"

#include "png/index.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "job/index.h"
#include "log/index.h"
#include "main/index.h"
#include "render/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "world/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    SH_WIDTH = 320u,
    SH_HEIGHT = 180u,
    SH_GOLDEN_TOLERANCE = 2u,
    SH_PROGRAM_TIMEOUT_MS = 120000u,
    SH_BOXES_MAX = 64u,
    SH_BLOCKS_MAX = 16u,
    SH_BOXES_PER_BLOCK_MAX = 4u,
    SH_COST_FRAMES = 30u,
    SH_COST_WIDTH = 1920u,
    SH_COST_HEIGHT = 1080u,
    SH_COST_RENDER_DISTANCE = 10u,
};

#define SH_PI 3.14159265358979f32
/* A plate is one texel thick: from 15/32 to 17/32 through the middle of a block. */
#define SH_THIN_LOW (15.0f64 / 32.0f64)
#define SH_THIN_HIGH (17.0f64 / 32.0f64)

/* ---- A rig: everything a real run has, on memory of its own ----------------------------------------------------------- */

typedef struct sh_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    thread_topology_t *topology; /* 56 */
} sh_rig_t;
static_assert(sizeof(sh_rig_t) == 64);

static void *sh_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool sh_rig_begin(test_context_t *t, sh_rig_t *rig) {
    rig->bridge = sh_aligned(t, sizeof(gpu_bridge_t));
    rig->job = sh_aligned(t, sizeof(job_system_t));
    rig->world = sh_aligned(t, sizeof(world_t));
    rig->terrain = sh_aligned(t, sizeof(terrain_t));
    rig->render = sh_aligned(t, sizeof(render_t));
    rig->texture = sh_aligned(t, sizeof(texture_t));
    rig->game = sh_aligned(t, sizeof(game_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default()))) {
        return false;
    }
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    return true;
}

static void sh_rig_end(test_context_t *t, sh_rig_t *rig) {
    game_deinit(rig->game);
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

/* Every column of a table empty but there, and nothing else: so that all a picture holds is what a test put in it. */
static bool sh_hand_world_open(test_context_t *t, sh_rig_t *rig, uint32_t radius, uint32_t width, uint32_t height) {
    world_t *const world = rig->world;
    render_t *const render = rig->render;
    if (!CHECK(world_open(world, radius, 0, 0))) {
        return false;
    }
    const render_upload_budget_t budget = {.bytes = RENDER_UPLOAD_BUDGET_BYTES_DEFAULT,
                                           .sections = RENDER_UPLOAD_BUDGET_SECTIONS_DEFAULT};
    if (!CHECK_EQ_U64(render_open(render, &world->snapshot, RENDER_POOL_BYTES_MIN, budget), GPU_STATUS_OK) ||
        !CHECK_EQ_U64(render_open_frame(render, rig->texture, width, height, 100000.0f32), GPU_STATUS_OK)) {
        return false;
    }
    world_map_request_t request;
    while (world_map_stream_take(&world->map, &request, 1u) == 1u) {
        if (!CHECK(world_map_stream_commit(&world->map, &request) != nullptr)) {
            return false;
        }
        world_edit_column_filled(&world->edit, request.slot);
    }
    return true;
}

static void sh_hand_world_close(sh_rig_t *rig) {
    render_close(rig->render);
    world_close(rig->world);
}

/* Publishes, and pumps until the card has it all. */
static bool sh_hand_world_sync(test_context_t *t, sh_rig_t *rig) {
    world_t *const world = rig->world;
    const world_edit_snapshot_t *const snapshot = &world->snapshot;
    for (uint32_t pass = 0u; pass < 1000000u; pass++) {
        (void)render_upload_resend_drain(&rig->render->upload, &world->edit);
        (void)world_edit_snapshot_publish(&world->snapshot);
        if (!CHECK_EQ_U64(render_upload_frame(&rig->render->upload), GPU_STATUS_OK)) {
            return false;
        }
        if (snapshot->published_count == snapshot->acquired_count + snapshot->overwritten_count &&
            world->edit.queue_count[WORLD_EDIT_CONSUMER_RENDER] == 0u && render_upload_idle(&rig->render->upload)) {
            return true;
        }
    }
    return CHECK(false);
}

static const uint8_t *sh_draw(test_context_t *t, sh_rig_t *rig, const render_camera_t *camera) {
    render_t *const render = rig->render;
    const gpu_status_t status = render_frame_draw(&render->frame, &render->buffers, camera);
    if (!CHECK_EQ_U64(status, GPU_STATUS_OK)) {
        gpu_error_log(&render->frame.gpu.last_error);
        return nullptr;
    }
    const uint8_t *const picture = render_frame_take(&render->frame);
    CHECK(picture != nullptr);
    return picture;
}

static uint32_t sh_pixel(const uint8_t *picture, uint32_t width, uint32_t x, uint32_t y) {
    uint32_t value = 0u;
    memcpy(&value, picture + ((uint64_t)y * width + x) * 4u, 4u);
    return value & 0xFFFFFFu;
}

static bool sh_bytes_near(uint32_t a, uint32_t b, uint32_t tolerance) {
    for (uint32_t shift = 0u; shift < 24u; shift += 8u) {
        const int32_t gap = (int32_t)((a >> shift) & 255u) - (int32_t)((b >> shift) & 255u);
        if ((uint32_t)abs(gap) > tolerance) {
            return false;
        }
    }
    return true;
}

/* ---- The boxes, worked out here apart from the table on the card ------------------------------------------------------- */

typedef struct sh_box_t {
    float64_t low[3];  /* 0: in a block, from 0 to 1 */
    float64_t high[3]; /* 24 */
} sh_box_t;
static_assert(sizeof(sh_box_t) == 48);

static uint32_t sh_boxes_of(uint32_t state, sh_box_t *out) {
    const float64_t half = 0.5f64, whole = 1.0f64, none = 0.0f64;
    const float64_t quarter_low = 7.0f64 / 32.0f64, quarter_high = 9.0f64 / 32.0f64;
    const float64_t far_low = 23.0f64 / 32.0f64, far_high = 25.0f64 / 32.0f64;
    switch ((block_shape_t)block_defs[state].shape) {
    case BLOCK_SHAPE_SLAB:
        out[0] = (sh_box_t){{none, none, none}, {whole, whole, half}};
        return 1u;
    case BLOCK_SHAPE_STAIRS:
        out[0] = (sh_box_t){{none, none, none}, {whole, whole, half}};
        out[1] = (sh_box_t){{none, half, half}, {whole, whole, whole}};
        return 2u;
    case BLOCK_SHAPE_CROSS:
        out[0] = (sh_box_t){{none, SH_THIN_LOW, none}, {whole, SH_THIN_HIGH, whole}};
        out[1] = (sh_box_t){{SH_THIN_LOW, none, none}, {SH_THIN_HIGH, whole, whole}};
        return 2u;
    case BLOCK_SHAPE_CROP:
        out[0] = (sh_box_t){{none, quarter_low, none}, {whole, quarter_high, whole}};
        out[1] = (sh_box_t){{none, far_low, none}, {whole, far_high, whole}};
        out[2] = (sh_box_t){{quarter_low, none, none}, {quarter_high, whole, whole}};
        out[3] = (sh_box_t){{far_low, none, none}, {far_high, whole, whole}};
        return 4u;
    case BLOCK_SHAPE_TORCH:
        out[0] = (sh_box_t){{7.0f64 / 16.0f64, 7.0f64 / 16.0f64, none}, {9.0f64 / 16.0f64, 9.0f64 / 16.0f64, 0.625f64}};
        return 1u;
    case BLOCK_SHAPE_THIN:
        if (state == BLOCK_ID_LILY_PAD) {
            out[0] = (sh_box_t){{none, none, none}, {whole, whole, 1.0f64 / 16.0f64}};
        } else {
            out[0] = (sh_box_t){{none, 15.0f64 / 16.0f64, none}, {whole, whole, whole}};
        }
        return 1u;
    case BLOCK_SHAPE_NONE:
    case BLOCK_SHAPE_CUBE:
    case BLOCK_SHAPE_FLUID:
        break;
    }
    out[0] = (sh_box_t){{none, none, none}, {whole, whole, whole}};
    return 1u;
}

/* ---- By hand: one ray, one number ------------------------------------------------------------------------------------- */

/* A ray from an eye some blocks from a block, and what it must meet. */
typedef struct sh_ray_case_t {
    uint16_t state;      /* 0 */
    uint8_t _pad0[2];    /* 2 */
    int32_t eye_cell[3]; /* 4: from the block */
    float32_t frac[3];   /* 16 */
    float32_t dir[3];    /* 28 */
    float32_t t_start;   /* 40 */
    float32_t want_t;    /* 44 */
    uint32_t want_face;  /* 48 */
} sh_ray_case_t;
static_assert(sizeof(sh_ray_case_t) == 52);

#define SH_32NDS(n) ((float32_t)(n) / 32.0f32)

static const sh_ray_case_t sh_ray_cases[] = {
    /* A slab: its top is at a half, and over it is air. */
    {BLOCK_ID_STONE_SLAB, {0}, {0, 0, 3}, {0.5f32, 0.5f32, 0.5f32}, {0, 0, -1}, 0.0f32, 3.0f32, TRAVERSE_FACE_POS_Z},
    {BLOCK_ID_STONE_SLAB, {0}, {-2, 0, 0}, {0.5f32, 0.5f32, 0.25f32}, {1, 0, 0}, 0.0f32, 1.5f32, TRAVERSE_FACE_NEG_X},
    {BLOCK_ID_STONE_SLAB, {0}, {-2, 0, 0}, {0.5f32, 0.5f32, 0.75f32}, {1, 0, 0}, 0.0f32, 0.0f32, TRAVERSE_FACE_MISS},
    /* From inside it, and from the air in its voxel. */
    {BLOCK_ID_STONE_SLAB, {0}, {0, 0, 0}, {0.5f32, 0.5f32, 0.25f32}, {1, 0, 0}, 0.0f32, 0.0f32, TRAVERSE_FACE_INSIDE},
    {BLOCK_ID_STONE_SLAB, {0}, {0, 0, 0}, {0.5f32, 0.5f32, 0.75f32}, {0, 0, 1}, 0.0f32, 0.0f32, TRAVERSE_FACE_MISS},
    {BLOCK_ID_STONE_SLAB, {0}, {0, 0, 0}, {0.5f32, 0.5f32, 0.75f32}, {0, 0, -1}, 0.0f32, 0.25f32, TRAVERSE_FACE_POS_Z},
    /* A ray told to start past it. */
    {BLOCK_ID_STONE_SLAB, {0}, {-2, 0, 0}, {0.5f32, 0.5f32, 0.25f32}, {1, 0, 0}, 5.0f32, 0.0f32, TRAVERSE_FACE_MISS},
    /* Stairs: the lower half whole, the upper half on the north side. */
    {BLOCK_ID_OAK_STAIRS, {0}, {0, -2, 0}, {0.5f32, 0.5f32, 0.75f32}, {0, 1, 0}, 0.0f32, 2.0f32, TRAVERSE_FACE_NEG_Y},
    {BLOCK_ID_OAK_STAIRS, {0}, {0, -2, 0}, {0.5f32, 0.5f32, 0.25f32}, {0, 1, 0}, 0.0f32, 1.5f32, TRAVERSE_FACE_NEG_Y},
    {BLOCK_ID_OAK_STAIRS, {0}, {0, 2, 0}, {0.5f32, 0.5f32, 0.75f32}, {0, -1, 0}, 0.0f32, 1.5f32, TRAVERSE_FACE_POS_Y},
    {BLOCK_ID_OAK_STAIRS, {0}, {-2, 0, 0}, {0.5f32, 0.25f32, 0.75f32}, {1, 0, 0}, 0.0f32, 0.0f32, TRAVERSE_FACE_MISS},
    {BLOCK_ID_OAK_STAIRS, {0}, {-2, 0, 0}, {0.5f32, 0.75f32, 0.75f32}, {1, 0, 0}, 0.0f32, 1.5f32, TRAVERSE_FACE_NEG_X},
    {BLOCK_ID_OAK_STAIRS, {0}, {0, 0, 3}, {0.5f32, 0.25f32, 0.5f32}, {0, 0, -1}, 0.0f32, 3.0f32, TRAVERSE_FACE_POS_Z},
    {BLOCK_ID_OAK_STAIRS, {0}, {0, 0, 3}, {0.5f32, 0.75f32, 0.5f32}, {0, 0, -1}, 0.0f32, 2.5f32, TRAVERSE_FACE_POS_Z},
    /* A plant: a plate along each axis, through the middle. */
    {BLOCK_ID_TALL_GRASS,
     {0},
     {-2, 0, 0},
     {0.5f32, 0.25f32, 0.5f32},
     {1, 0, 0},
     0.0f32,
     1.5f32 + SH_32NDS(15),
     TRAVERSE_FACE_NEG_X},
    {BLOCK_ID_TALL_GRASS,
     {0},
     {0, -2, 0},
     {0.25f32, 0.5f32, 0.5f32},
     {0, 1, 0},
     0.0f32,
     1.5f32 + SH_32NDS(15),
     TRAVERSE_FACE_NEG_Y},
    {BLOCK_ID_TALL_GRASS,
     {0},
     {2, 0, 0},
     {0.5f32, 0.25f32, 0.5f32},
     {-1, 0, 0},
     0.0f32,
     2.5f32 - SH_32NDS(17),
     TRAVERSE_FACE_POS_X},
    {BLOCK_ID_TALL_GRASS, {0}, {-2, 0, 0}, {0.5f32, 0.5f32, 0.5f32}, {1, 0, 0}, 0.0f32, 1.5f32, TRAVERSE_FACE_NEG_X},
    {BLOCK_ID_TALL_GRASS, {0}, {0, 0, 3}, {0.25f32, 0.25f32, 0.5f32}, {0, 0, -1}, 0.0f32, 0.0f32, TRAVERSE_FACE_MISS},
    /* An eye in the middle of a plant, inside both plates, sees on past them. */
    {BLOCK_ID_TALL_GRASS, {0}, {0, 0, 0}, {0.5f32, 0.5f32, 0.5f32}, {1, 0, 0}, 0.0f32, 0.0f32, TRAVERSE_FACE_MISS},
    /* A crop: two plates along each axis. */
    {BLOCK_ID_WHEAT_STAGE_8,
     {0},
     {-2, 0, 0},
     {0.5f32, 0.5f32, 0.5f32},
     {1, 0, 0},
     0.0f32,
     1.5f32 + SH_32NDS(7),
     TRAVERSE_FACE_NEG_X},
    {BLOCK_ID_WHEAT_STAGE_8,
     {0},
     {2, 0, 0},
     {0.5f32, 0.5f32, 0.5f32},
     {-1, 0, 0},
     0.0f32,
     2.5f32 - SH_32NDS(25),
     TRAVERSE_FACE_POS_X},
    {BLOCK_ID_WHEAT_STAGE_8,
     {0},
     {0, -2, 0},
     {0.5f32, 0.5f32, 0.5f32},
     {0, 1, 0},
     0.0f32,
     1.5f32 + SH_32NDS(7),
     TRAVERSE_FACE_NEG_Y},
    {BLOCK_ID_WHEAT_STAGE_8, {0}, {0, 0, 3}, {0.5f32, 0.5f32, 0.5f32}, {0, 0, -1}, 0.0f32, 0.0f32, TRAVERSE_FACE_MISS},
    /* A torch: a stick in the middle, ten texels tall. */
    {BLOCK_ID_TORCH,
     {0},
     {-2, 0, 0},
     {0.5f32, 0.5f32, 0.25f32},
     {1, 0, 0},
     0.0f32,
     1.5f32 + SH_32NDS(14),
     TRAVERSE_FACE_NEG_X},
    {BLOCK_ID_TORCH, {0}, {-2, 0, 0}, {0.5f32, 0.3f32, 0.25f32}, {1, 0, 0}, 0.0f32, 0.0f32, TRAVERSE_FACE_MISS},
    {BLOCK_ID_TORCH, {0}, {-2, 0, 0}, {0.5f32, 0.5f32, 0.75f32}, {1, 0, 0}, 0.0f32, 0.0f32, TRAVERSE_FACE_MISS},
    {BLOCK_ID_TORCH, {0}, {0, 0, 3}, {0.5f32, 0.5f32, 0.5f32}, {0, 0, -1}, 0.0f32, 2.875f32, TRAVERSE_FACE_POS_Z},
    /* A ladder, on its one side for now, and a lily pad on the floor. */
    {BLOCK_ID_LADDER,
     {0},
     {0, -2, 0},
     {0.5f32, 0.5f32, 0.5f32},
     {0, 1, 0},
     0.0f32,
     1.5f32 + SH_32NDS(30),
     TRAVERSE_FACE_NEG_Y},
    {BLOCK_ID_LADDER, {0}, {0, 2, 0}, {0.5f32, 0.5f32, 0.5f32}, {0, -1, 0}, 0.0f32, 1.5f32, TRAVERSE_FACE_POS_Y},
    {BLOCK_ID_LADDER, {0}, {-2, 0, 0}, {0.5f32, 0.5f32, 0.5f32}, {1, 0, 0}, 0.0f32, 0.0f32, TRAVERSE_FACE_MISS},
    {BLOCK_ID_LILY_PAD,
     {0},
     {0, 0, 3},
     {0.5f32, 0.5f32, 0.5f32},
     {0, 0, -1},
     0.0f32,
     3.5f32 - SH_32NDS(2),
     TRAVERSE_FACE_POS_Z},
    {BLOCK_ID_LILY_PAD, {0}, {-2, 0, 0}, {0.5f32, 0.5f32, 0.5f32}, {1, 0, 0}, 0.0f32, 0.0f32, TRAVERSE_FACE_MISS},
    {BLOCK_ID_LILY_PAD, {0}, {-2, 0, 0}, {0.5f32, 0.5f32, 0.03f32}, {1, 0, 0}, 0.0f32, 1.5f32, TRAVERSE_FACE_NEG_X},
    /* A cube is what it was, and water stays one until Stage 09.3. */
    {BLOCK_ID_STONE, {0}, {-2, 0, 0}, {0.5f32, 0.5f32, 0.75f32}, {1, 0, 0}, 0.0f32, 1.5f32, TRAVERSE_FACE_NEG_X},
    {BLOCK_ID_WATER, {0}, {0, 0, 3}, {0.5f32, 0.5f32, 0.5f32}, {0, 0, -1}, 0.0f32, 2.5f32, TRAVERSE_FACE_POS_Z},
    /* And a material past the table is a cube. */
    {60000u, {0}, {-2, 0, 0}, {0.5f32, 0.5f32, 0.5f32}, {1, 0, 0}, 0.0f32, 1.5f32, TRAVERSE_FACE_NEG_X},
};
enum : uint32_t { SH_RAY_CASES = sizeof(sh_ray_cases) / sizeof(sh_ray_cases[0]) };

static traverse_world_t sh_world_of(const sh_rig_t *rig, bool shapes) {
    const render_t *const render = rig->render;
    return (traverse_world_t){.borrowed_table = render->buffers.borrowed_table,
                              .borrowed_pool = render->buffers.borrowed_pool,
                              .window = render->buffers.window,
                              .borrowed_shapes = shapes ? render->frame.borrowed_shapes : nullptr,
                              .shape_count = BLOCK_COUNT};
}

/* A real kernel, over a list of rays. */
static bool sh_cast(test_context_t *t, sh_rig_t *rig, const traverse_world_t *world, const traverse_ray_t *rays,
                    uint32_t count, traverse_hit_t *out_hits) {
    device_memory_t *const memory = &rig->bridge->device_memory;
    device_block_t ray_block = {0}, hit_block = {0};
    gpu_error_t error = {0};
    bool good = CHECK_EQ_U64(device_memory_alloc(memory, DEVICE_POOL_FRAMES, count * sizeof(traverse_ray_t), "sh rays",
                                                 &ray_block),
                             GPU_STATUS_OK) &&
                CHECK_EQ_U64(device_memory_alloc(memory, DEVICE_POOL_FRAMES, count * sizeof(traverse_hit_t), "sh hits",
                                                 &hit_block),
                             GPU_STATUS_OK) &&
                CHECK_EQ_U64(device_memory_write(memory, &ray_block, 0u, rays, count * sizeof(traverse_ray_t)),
                             GPU_STATUS_OK) &&
                CHECK_EQ_U64(traverse_rays(&error, world, &ray_block, &hit_block, count), GPU_STATUS_OK) &&
                CHECK_EQ_U64(traverse_wait(&error), GPU_STATUS_OK) &&
                CHECK_EQ_U64(device_memory_read(memory, &hit_block, 0u, out_hits, count * sizeof(traverse_hit_t)),
                             GPU_STATUS_OK);
    good = CHECK_EQ_U64(device_memory_free(memory, &hit_block), GPU_STATUS_OK) && good;
    good = CHECK_EQ_U64(device_memory_free(memory, &ray_block), GPU_STATUS_OK) && good;
    return good;
}

/* Each case gets a place of its own, on a diagonal: no ray of one can meet the block of another. The map loads a circle of
 * columns, so the radius below is well past the far corner of this. */
static void sh_case_cell(uint32_t index, int32_t cell[3]) {
    cell[0] = -36 + (int32_t)index * 3;
    cell[1] = -36 + (int32_t)index * 3;
    cell[2] = 8;
}

TEST(each_shape_is_met_where_its_boxes_say) {
    sh_rig_t rig;
    REQUIRE(sh_rig_begin(t, &rig));
    REQUIRE(sh_hand_world_open(t, &rig, 8u, 64u, 64u));
    traverse_ray_t *const rays = test_alloc(t, SH_RAY_CASES * sizeof(traverse_ray_t));
    traverse_hit_t *const hits = test_alloc(t, SH_RAY_CASES * sizeof(traverse_hit_t));
    for (uint32_t i = 0u; i < SH_RAY_CASES; i++) {
        const sh_ray_case_t *const c = &sh_ray_cases[i];
        int32_t cell[3];
        sh_case_cell(i, cell);
        CHECK_EQ_U64(world_edit_block_set(&rig.world->edit, cell[0], cell[1], cell[2], c->state),
                     WORLD_EDIT_STATUS_CHANGED);
        rays[i] =
            (traverse_ray_t){.eye_cell = {cell[0] + c->eye_cell[0], cell[1] + c->eye_cell[1], cell[2] + c->eye_cell[2]},
                             .eye_frac = {c->frac[0], c->frac[1], c->frac[2]},
                             .direction = {c->dir[0], c->dir[1], c->dir[2]},
                             .t_start = c->t_start};
    }
    REQUIRE(sh_hand_world_sync(t, &rig));
    const traverse_world_t world = sh_world_of(&rig, true);
    REQUIRE(sh_cast(t, &rig, &world, rays, SH_RAY_CASES, hits));
    for (uint32_t i = 0u; i < SH_RAY_CASES; i++) {
        const sh_ray_case_t *const c = &sh_ray_cases[i];
        const uint32_t face = traverse_hit_face(hits[i].info);
        const bool right_face = face == c->want_face;
        const bool right_t = c->want_face == TRAVERSE_FACE_MISS || fabsf(hits[i].t - c->want_t) < 1e-4f32;
        if (!right_face || !right_t) {
            printf("  case %" PRIu32 ": %s: face %" PRIu32 ", wanted %" PRIu32 "; t %f, wanted %f\n", i,
                   c->state < BLOCK_COUNT ? block_defs[c->state].name : "(no block)", face, c->want_face,
                   (libc_double_t)hits[i].t, (libc_double_t)c->want_t);
        }
        CHECK(right_face);
        CHECK(right_t);
        CHECK_EQ_U64((uint32_t)hits[i].info & TRAVERSE_INFO_CAPPED, 0u);
        if (c->want_face != TRAVERSE_FACE_MISS) {
            CHECK_EQ_U64(hits[i].value & TRAVERSE_VALUE_MATERIAL_MASK, c->state);
            CHECK_EQ_I64(hits[i].offset[0], -c->eye_cell[0]);
            CHECK_EQ_I64(hits[i].offset[1], -c->eye_cell[1]);
            CHECK_EQ_I64(hits[i].offset[2], -c->eye_cell[2]);
        }
    }
    /* With the setting off, every one of those is a whole cube: the rays that passed a slab by now stop on it. */
    const traverse_world_t cubes = sh_world_of(&rig, false);
    REQUIRE(sh_cast(t, &rig, &cubes, rays, SH_RAY_CASES, hits));
    CHECK_EQ_U64(traverse_hit_face(hits[2].info), TRAVERSE_FACE_NEG_X);
    CHECK_NEAR(hits[2].t, 1.5f32, 1e-4f32);
    CHECK_EQ_U64(traverse_hit_face(hits[0].info), TRAVERSE_FACE_POS_Z);
    CHECK_NEAR(hits[0].t, 2.5f32, 1e-4f32);
    /* A table too short for what the world says is refused. */
    traverse_world_t bad = world;
    bad.shape_count = 1u << 24;
    gpu_error_t error = {0};
    device_block_t *const blocks = test_alloc(t, sizeof(device_block_t));
    memset(blocks, 0, sizeof(device_block_t));
    CHECK_EQ_U64(traverse_rays(&error, &bad, blocks, blocks, 1u), GPU_STATUS_INVALID_ARGUMENT);
    CHECK_CONTAINS(error.borrowed_what, "shape table");
    sh_hand_world_close(&rig);
    sh_rig_end(t, &rig);
}

/* A section all of one block has no masks, so a ray learns that it missed a box only in the box's voxel, one voxel at a time. */
TEST(a_whole_section_of_one_shape_lets_a_ray_through) {
    sh_rig_t rig;
    REQUIRE(sh_rig_begin(t, &rig));
    REQUIRE(sh_hand_world_open(t, &rig, 2u, 64u, 64u));
    for (int32_t z = 0; z < 16; z++) {
        for (int32_t y = 0; y < 16; y++) {
            for (int32_t x = 0; x < 16; x++) {
                REQUIRE(world_edit_block_set(&rig.world->edit, x, y, z, BLOCK_ID_STONE_SLAB) ==
                        WORLD_EDIT_STATUS_CHANGED);
            }
        }
    }
    REQUIRE(sh_hand_world_sync(t, &rig));
    /* It really is the short form. */
    CHECK_EQ_U64(rig.render->buffers.section_count, 1u);
    CHECK_EQ_U64(rig.render->buffers.slab_bytes, 128u);
    const traverse_ray_t rays[] = {
        /* Through the upper half of the layer at z = 3, all the way along. */
        {.eye_cell = {-3, 8, 3}, .eye_frac = {0.5f32, 0.5f32, 0.75f32}, .direction = {1.0f32, 0.0f32, 0.0f32}},
        /* The lower half: the first voxel stops it. */
        {.eye_cell = {-3, 8, 3}, .eye_frac = {0.5f32, 0.5f32, 0.25f32}, .direction = {1.0f32, 0.0f32, 0.0f32}},
        /* From above, onto the top layer's slab: z = 15.5. */
        {.eye_cell = {8, 8, 20}, .eye_frac = {0.5f32, 0.5f32, 0.5f32}, .direction = {0.0f32, 0.0f32, -1.0f32}},
        /* Up from under the section, onto the lowest slab's underside. */
        {.eye_cell = {8, 8, -3}, .eye_frac = {0.5f32, 0.5f32, 0.5f32}, .direction = {0.0f32, 0.0f32, 1.0f32}},
    };
    traverse_hit_t hits[4];
    const traverse_world_t world = sh_world_of(&rig, true);
    REQUIRE(sh_cast(t, &rig, &world, rays, 4u, hits));
    CHECK_EQ_U64(traverse_hit_face(hits[0].info), TRAVERSE_FACE_MISS);
    CHECK_EQ_U64(traverse_hit_face(hits[1].info), TRAVERSE_FACE_NEG_X);
    CHECK_NEAR(hits[1].t, 2.5f32, 1e-4f32);
    CHECK_EQ_U64(traverse_hit_face(hits[2].info), TRAVERSE_FACE_POS_Z);
    CHECK_NEAR(hits[2].t, 5.0f32, 1e-4f32);
    CHECK_EQ_U64(traverse_hit_face(hits[3].info), TRAVERSE_FACE_NEG_Z);
    CHECK_NEAR(hits[3].t, 2.5f32, 1e-4f32);
    sh_hand_world_close(&rig);
    sh_rig_end(t, &rig);
}

/* ---- A world of a few blocks of every shape, against an exact reference ---------------------------------------------------- */

typedef struct sh_scene_box_t {
    float64_t low[3];  /* 0: world coordinates */
    float64_t high[3]; /* 24 */
    uint32_t block;    /* 48: index into the scene's blocks, or SH_BLOCKS_MAX for the floor */
    uint32_t state;    /* 52 */
} sh_scene_box_t;
static_assert(sizeof(sh_scene_box_t) == 56);

typedef struct sh_scene_block_t {
    int32_t cell[3]; /* 0 */
    uint32_t state;  /* 12 */
} sh_scene_block_t;
static_assert(sizeof(sh_scene_block_t) == 16);

/* What each block's shape is seen for, from every side. */
static const sh_scene_block_t sh_scene_blocks[] = {
    {{2, 2, 0}, BLOCK_ID_STONE_SLAB},    {{5, 2, 0}, BLOCK_ID_OAK_STAIRS},  {{8, 2, 0}, BLOCK_ID_COBBLESTONE_STAIRS},
    {{11, 2, 0}, BLOCK_ID_OAK_SLAB},     {{2, 6, 0}, BLOCK_ID_TALL_GRASS},  {{5, 6, 0}, BLOCK_ID_DANDELION},
    {{8, 6, 0}, BLOCK_ID_WHEAT_STAGE_8}, {{11, 6, 0}, BLOCK_ID_TORCH},      {{2, 10, 0}, BLOCK_ID_LADDER},
    {{5, 10, 0}, BLOCK_ID_LILY_PAD},     {{8, 10, 0}, BLOCK_ID_OAK_LEAVES}, {{11, 10, 0}, BLOCK_ID_STONE},
    {{8, 11, 0}, BLOCK_ID_OAK_LEAVES},   {{3, 13, 0}, BLOCK_ID_FERN},
};
enum : uint32_t { SH_SCENE_BLOCKS = sizeof(sh_scene_blocks) / sizeof(sh_scene_blocks[0]) };
static_assert((uint32_t)SH_SCENE_BLOCKS <= (uint32_t)SH_BLOCKS_MAX);

typedef struct sh_scene_t {
    sh_scene_box_t boxes[SH_BOXES_MAX]; /* 0 */
    uint32_t box_count;                 /* 3584 */
    uint8_t alpha;                      /* 3588: rays go on through holes */
    uint8_t _pad0[3];                   /* 3589 */
} sh_scene_t;
static_assert(sizeof(sh_scene_t) == 3592);

/* The stone floor is one box: 16 x 16 and a block deep, below z = 0. */
static void sh_scene_build(sh_scene_t *scene, uint32_t features) {
    memset(scene, 0, sizeof(*scene));
    scene->alpha = (features & SHADE_FEATURE_ALPHA) != 0u ? 1u : 0u;
    scene->boxes[0] = (sh_scene_box_t){.low = {0.0f64, 0.0f64, -1.0f64},
                                       .high = {16.0f64, 16.0f64, 0.0f64},
                                       .block = SH_BLOCKS_MAX,
                                       .state = BLOCK_ID_STONE};
    scene->box_count = 1u;
    for (uint32_t b = 0u; b < SH_SCENE_BLOCKS; b++) {
        const sh_scene_block_t *const block = &sh_scene_blocks[b];
        sh_box_t boxes[SH_BOXES_PER_BLOCK_MAX];
        uint32_t count = 1u;
        if ((features & SHADE_FEATURE_SHAPES) != 0u) {
            count = sh_boxes_of(block->state, boxes);
        } else {
            boxes[0] = (sh_box_t){{0.0f64, 0.0f64, 0.0f64}, {1.0f64, 1.0f64, 1.0f64}};
        }
        for (uint32_t i = 0u; i < count; i++) {
            sh_scene_box_t *const out = &scene->boxes[scene->box_count];
            scene->box_count++;
            out->block = b;
            out->state = block->state;
            for (uint32_t k = 0u; k < 3u; k++) {
                out->low[k] = (float64_t)block->cell[k] + boxes[i].low[k];
                out->high[k] = (float64_t)block->cell[k] + boxes[i].high[k];
            }
        }
    }
}

/* A texture's coordinates, written apart from the kernel's: up the texture is +z on a side, and each side's right hand comes from
 * which way someone looking at it faces. Tests/run/render/frame holds these to a furnace's front. */
static void sh_reference_uv(uint32_t face, const float64_t local[3], float64_t *out_u, float64_t *out_v) {
    switch (face) {
    case 0u:
        *out_u = 1.0f64 - local[1];
        *out_v = 1.0f64 - local[2];
        break;
    case 1u:
        *out_u = local[1];
        *out_v = 1.0f64 - local[2];
        break;
    case 2u:
        *out_u = local[0];
        *out_v = 1.0f64 - local[2];
        break;
    case 3u:
        *out_u = 1.0f64 - local[0];
        *out_v = 1.0f64 - local[2];
        break;
    case 4u:
        *out_u = local[0];
        *out_v = local[1];
        break;
    default:
        *out_u = local[0];
        *out_v = 1.0f64 - local[1];
        break;
    }
}

static bool sh_near_whole(float64_t value, float64_t margin) {
    return fabs(value - round(value)) < margin;
}

/* What one ray must show. */
typedef struct sh_seen_t {
    uint64_t signature; /* 0: every surface on its way, to tell one ray's story from a neighbour's */
    uint32_t expected;  /* 8: the pixel */
    uint32_t state;     /* 12: of what it stopped on */
    uint32_t layers;    /* 16: holes it went through */
    uint32_t face;      /* 20 */
    uint8_t hit;        /* 24 */
    uint8_t usable;     /* 25: nothing on its way was near enough an edge for rounding to choose */
    uint8_t _pad0[6];   /* 26 */
} sh_seen_t;
static_assert(sizeof(sh_seen_t) == 32);

/* Goes through the boxes in float64_t, by the slab test, from surface to surface. */
static void sh_trace(test_context_t *t, const sh_rig_t *rig, const sh_scene_t *scene, const traverse_camera_t *view,
                     const float64_t eye[3], const float64_t direction[3], sh_seen_t *out) {
    const post_settings_t *const post = &rig->render->frame.post;
    memset(out, 0, sizeof(*out));
    out->usable = 1u;
    float64_t after = 0.0f64;
    for (uint32_t layer = 0u; layer < SHADE_ALPHA_LAYERS; layer++) {
        float64_t best = 1e30f64;
        uint32_t best_box = 0u, best_face = 0u;
        bool found = false;
        for (uint32_t b = 0u; b < scene->box_count; b++) {
            const sh_scene_box_t *const box = &scene->boxes[b];
            float64_t t_in = -1e30f64, t_out = 1e30f64;
            uint32_t face = 0u;
            bool possible = true;
            for (uint32_t i = 0u; i < 3u && possible; i++) {
                const float64_t low = box->low[i] - eye[i], high = box->high[i] - eye[i];
                if (fabs(direction[i]) < 1e-12f64) {
                    possible = low <= 0.0f64 && high >= 0.0f64;
                    continue;
                }
                const float64_t a = low / direction[i], c = high / direction[i];
                const float64_t entered = a < c ? a : c, left = a < c ? c : a;
                if (entered > t_in) {
                    t_in = entered;
                    face = i * 2u + (direction[i] > 0.0f64 ? 0u : 1u);
                }
                t_out = left < t_out ? left : t_out;
            }
            if (possible && t_in < t_out && t_in > after && t_in < best) {
                best = t_in;
                best_box = b;
                best_face = face;
                found = true;
            }
        }
        if (!found) {
            const float32_t unit[3] = {(float32_t)direction[0], (float32_t)direction[1], (float32_t)direction[2]};
            float32_t sky[3];
            post_sky(post, unit, sky);
            out->expected = post_xrgb(post_tone_map(sky[0]), post_tone_map(sky[1]), post_tone_map(sky[2]));
            return;
        }
        const sh_scene_box_t *const box = &scene->boxes[best_box];
        out->signature = out->signature * 1000003u + best_box * 8u + best_face + 1u;
        out->hit = 1u;
        out->face = best_face;
        out->state = box->state;
        float64_t local[3];
        for (uint32_t i = 0u; i < 3u; i++) {
            const float64_t point = eye[i] + direction[i] * best;
            local[i] = box->block == SH_BLOCKS_MAX ? point - floor(point)
                                                   : point - (float64_t)sh_scene_blocks[box->block].cell[i];
        }
        float64_t u = 0.0f64, v = 0.0f64;
        sh_reference_uv(best_face, local, &u, &v);
        uint32_t id = 0u;
        REQUIRE(texture_find(rig->texture, block_texture_names[block_defs[box->state].faces[best_face]], &id));
        texture_info_t info;
        REQUIRE(texture_info_get(rig->texture, id, &info));
        const texture_blob_entry_t *const entry = info.borrowed_entry;
        const float32_t tangent = shade_pixel_tangent(view);
        const uint32_t level = shade_mip_level((float32_t)best, tangent, entry->width, entry->level_count);
        texture_mip_t mip;
        REQUIRE(texture_mip_get(rig->texture, id, level, 0u, &mip));
        const float32_t per_pixel = (float32_t)best * tangent * (float32_t)entry->width;
        if (sh_near_whole(u * (float64_t)mip.width, 0.02f64) || sh_near_whole(v * (float64_t)mip.height, 0.02f64) ||
            (per_pixel > 1.0f32 && sh_near_whole((float64_t)log2f(per_pixel) + 0.5f64, 0.005f64))) {
            out->usable = 0u;
        }
        const uint32_t x = shade_texel_of((float32_t)u, mip.width), y = shade_texel_of((float32_t)v, mip.height);
        const uint8_t *const texel = mip.borrowed_pixels + ((uint64_t)y * mip.width + x) * 4u;
        if (texel[3] == 0u && scene->alpha != 0u && layer + 1u < SHADE_ALPHA_LAYERS) {
            after = best;
            out->layers++;
            continue;
        }
        const float64_t brightness[SHADE_FACES] = {0.6f64, 0.6f64, 0.8f64, 0.8f64, 0.5f64, 1.0f64};
        const float32_t cover = (float32_t)texel[3] / 255.0f32;
        uint32_t channel[3];
        for (uint32_t i = 0u; i < 3u; i++) {
            const float32_t seen = shade_srgb_to_linear((float32_t)texel[i] / 255.0f32);
            const float32_t behind = shade_srgb_to_linear((float32_t)entry->mean_rgba[i] / 255.0f32);
            const float32_t colour = texel[3] == 255u ? seen : behind + (seen - behind) * cover;
            channel[i] = post_tone_map((float32_t)((float64_t)colour * brightness[best_face]));
        }
        out->expected = post_xrgb(channel[0], channel[1], channel[2]);
        return;
    }
}

typedef struct sh_tally_t {
    uint64_t compared;                /* 0 */
    uint64_t hits;                    /* 8 */
    uint64_t wrong;                   /* 16 */
    uint64_t through_holes;           /* 24: pixels showing what is behind a hole */
    uint64_t face_seen[SHADE_FACES];  /* 32 */
    uint64_t state_seen[BLOCK_COUNT]; /* 80 */
} sh_tally_t;
static_assert(sizeof(sh_tally_t) == 80u + BLOCK_COUNT * 8u);

static void sh_compare_picture(test_context_t *t, const sh_rig_t *rig, const sh_scene_t *scene,
                               const render_camera_t *camera, const uint8_t *picture, uint32_t width, uint32_t height,
                               sh_tally_t *out) {
    traverse_camera_t view;
    render_camera_to_traverse(camera, width, height, &view);
    for (uint32_t py = 0u; py < height; py++) {
        for (uint32_t px = 0u; px < width; px++) {
            const float64_t a = 2.0f64 * ((float64_t)px + 0.5f64) / (float64_t)width - 1.0f64;
            const float64_t b = 1.0f64 - 2.0f64 * ((float64_t)py + 0.5f64) / (float64_t)height;
            float64_t direction[3];
            float64_t length = 0.0f64;
            for (uint32_t i = 0u; i < 3u; i++) {
                direction[i] = (float64_t)view.forward[i] + a * (float64_t)view.right[i] + b * (float64_t)view.up[i];
                length += direction[i] * direction[i];
            }
            length = sqrt(length);
            for (uint32_t i = 0u; i < 3u; i++) {
                direction[i] /= length;
            }
            sh_seen_t seen;
            sh_trace(t, rig, scene, &view, camera->position, direction, &seen);
            bool usable = seen.usable != 0u;
            /* A silhouette: this ray or one a hair from it may go either way. Decided by looking a little to each side. */
            for (uint32_t i = 0u; i < 2u && usable; i++) {
                float64_t nudged[3];
                for (uint32_t k = 0u; k < 3u; k++) {
                    const float64_t sideways = (i == 0u ? 1.0f64 : -1.0f64) * 0.01f64 / (float64_t)height;
                    nudged[k] = direction[k] + sideways * ((float64_t)view.right[k] + (float64_t)view.up[k]);
                }
                sh_seen_t other;
                sh_trace(t, rig, scene, &view, camera->position, nudged, &other);
                usable = other.signature == seen.signature;
            }
            if (!usable) {
                continue;
            }
            out->compared++;
            if (seen.hit != 0u) {
                out->hits++;
                out->face_seen[seen.face]++;
                out->state_seen[seen.state]++;
            }
            out->through_holes += seen.layers != 0u ? 1u : 0u;
            if (!sh_bytes_near(sh_pixel(picture, width, px, py), seen.expected, 1u)) {
                if (out->wrong < 8u) {
                    printf("  pixel %" PRIu32 ", %" PRIu32 ": card %06" PRIX32 ", reference %06" PRIX32 ", %" PRIu32
                           " holes, face %" PRIu32 ", %s\n",
                           px, py, sh_pixel(picture, width, px, py), seen.expected, seen.layers, seen.face,
                           seen.hit != 0u ? block_defs[seen.state].name : "sky");
                }
                out->wrong++;
            }
        }
    }
}

static bool sh_scene_put(test_context_t *t, sh_rig_t *rig) {
    world_edit_t *const edit = &rig->world->edit;
    for (int32_t y = 0; y < 16; y++) {
        for (int32_t x = 0; x < 16; x++) {
            if (!CHECK_EQ_U64(world_edit_block_set(edit, x, y, -1, BLOCK_ID_STONE), WORLD_EDIT_STATUS_CHANGED)) {
                return false;
            }
        }
    }
    for (uint32_t b = 0u; b < SH_SCENE_BLOCKS; b++) {
        const sh_scene_block_t *const block = &sh_scene_blocks[b];
        if (!CHECK_EQ_U64(
                world_edit_block_set(edit, block->cell[0], block->cell[1], block->cell[2], (uint16_t)block->state),
                WORLD_EDIT_STATUS_CHANGED)) {
            return false;
        }
    }
    return sh_hand_world_sync(t, rig);
}

static void sh_camera_around(render_camera_t *camera, float32_t yaw, float32_t pitch, float64_t reach) {
    render_camera_start(camera, 7.5f64 - reach * (float64_t)cosf(pitch) * (float64_t)cosf(yaw),
                        6.5f64 - reach * (float64_t)cosf(pitch) * (float64_t)sinf(yaw),
                        0.5f64 - reach * (float64_t)sinf(pitch), yaw, pitch);
}

static void sh_report(const char *what, const sh_tally_t *tally) {
    printf("  %s: %" PRIu64 " pixels compared, %" PRIu64 " hits, %" PRIu64 " through a hole, %" PRIu64 " wrong\n", what,
           tally->compared, tally->hits, tally->through_holes, tally->wrong);
}

/* Every usable pixel of pictures from all sides, each against a trace that shares nothing with the kernel but its tables. */
static void sh_compare_from_all_sides(test_context_t *t, sh_rig_t *rig, uint32_t features, sh_tally_t *tally) {
    sh_scene_t *const scene = test_alloc(t, sizeof(sh_scene_t));
    sh_scene_build(scene, features);
    render_frame_features_set(&rig->render->frame, features);
    const float64_t reaches[] = {6.0f64, 11.0f64, 30.0f64};
    for (uint32_t d = 0u; d < sizeof(reaches) / sizeof(reaches[0]); d++) {
        for (uint32_t turn = 0u; turn < 8u; turn++) {
            for (uint32_t level = 0u; level < 2u; level++) {
                const float32_t yaw = (float32_t)turn * SH_PI / 4.0f32 + 0.11f32;
                const float32_t pitch = level == 0u ? -0.35f32 : -0.8f32;
                render_camera_t camera;
                sh_camera_around(&camera, yaw, pitch, reaches[d]);
                const uint8_t *const picture = sh_draw(t, rig, &camera);
                REQUIRE(picture != nullptr);
                sh_compare_picture(t, rig, scene, &camera, picture, SH_WIDTH, SH_HEIGHT, tally);
            }
        }
    }
}

TEST(every_shape_and_every_hole_matches_an_exact_trace_from_all_sides) {
    sh_rig_t rig;
    REQUIRE(sh_rig_begin(t, &rig));
    REQUIRE(sh_hand_world_open(t, &rig, 2u, SH_WIDTH, SH_HEIGHT));
    REQUIRE(sh_scene_put(t, &rig));
    sh_tally_t *const tally = test_alloc(t, sizeof(sh_tally_t));
    memset(tally, 0, sizeof(*tally));
    sh_compare_from_all_sides(t, &rig, SHADE_FEATURES_ALL, tally);
    sh_report("shapes and holes", tally);
    CHECK_EQ_U64(tally->wrong, 0u);
    CHECK(tally->compared > (uint64_t)SH_WIDTH * SH_HEIGHT * 24u);
    CHECK(tally->through_holes > 2000u);
    /* Every eye here is above the floor, so an underside is seen by hand alone, in the section of slabs. */
    for (uint32_t face = 0u; face < SHADE_FACES; face++) {
        CHECK(face == TRAVERSE_FACE_NEG_Z || tally->face_seen[face] > 100u);
    }
    /* A test that looks at almost none of a block passes on it too. */
    for (uint32_t b = 0u; b < SH_SCENE_BLOCKS; b++) {
        if (!CHECK(tally->state_seen[sh_scene_blocks[b].state] > 50u)) {
            printf("  hardly saw %s: %" PRIu64 "\n", block_defs[sh_scene_blocks[b].state].name,
                   tally->state_seen[sh_scene_blocks[b].state]);
        }
    }
    sh_hand_world_close(&rig);
    sh_rig_end(t, &rig);
}

TEST(each_setting_off_is_exactly_what_it_says) {
    sh_rig_t rig;
    REQUIRE(sh_rig_begin(t, &rig));
    REQUIRE(sh_hand_world_open(t, &rig, 2u, SH_WIDTH, SH_HEIGHT));
    REQUIRE(sh_scene_put(t, &rig));
    /* What every frame starts with. */
    CHECK_EQ_U64(rig.render->frame.scene.features, SHADE_FEATURES_ALL);
    const uint32_t settings[] = {0u, SHADE_FEATURE_SHAPES, SHADE_FEATURE_ALPHA};
    const char *const names[] = {"all off", "shapes alone", "alpha alone"};
    for (uint32_t s = 0u; s < 3u; s++) {
        sh_tally_t *const tally = test_alloc(t, sizeof(sh_tally_t));
        memset(tally, 0, sizeof(*tally));
        sh_compare_from_all_sides(t, &rig, settings[s], tally);
        sh_report(names[s], tally);
        CHECK_EQ_U64(tally->wrong, 0u);
        CHECK(tally->compared > (uint64_t)SH_WIDTH * SH_HEIGHT * 24u);
        /* No ray goes on with alpha off, and with it on some do, even through whole cubes. */
        CHECK(((settings[s] & SHADE_FEATURE_ALPHA) != 0u) == (tally->through_holes > 0u));
    }
    /* What is asked for beyond what there is does nothing. */
    render_frame_features_set(&rig.render->frame, 0xFFFFFFFFu);
    CHECK_EQ_U64(rig.render->frame.scene.features, SHADE_FEATURES_ALL);
    sh_hand_world_close(&rig);
    sh_rig_end(t, &rig);
}

/* With and without the coarse pass, the same rays find the same things: boxes are inside the voxels the pass sees. */
TEST(the_coarse_pass_never_starts_a_ray_past_a_box) {
    sh_rig_t rig;
    REQUIRE(sh_rig_begin(t, &rig));
    REQUIRE(sh_hand_world_open(t, &rig, 2u, SH_WIDTH, SH_HEIGHT));
    REQUIRE(sh_scene_put(t, &rig));
    render_t *const render = rig.render;
    device_memory_t *const memory = &rig.bridge->device_memory;
    const uint64_t bytes = traverse_hits_bytes(SH_WIDTH, SH_HEIGHT);
    traverse_hit_t *const with = test_alloc(t, bytes);
    traverse_hit_t *const without = test_alloc(t, bytes);
    const traverse_world_t world = sh_world_of(&rig, true);
    const traverse_targets_t targets = {&render->owned_coarse, &render->owned_hits};
    uint64_t rays = 0u, different = 0u, hits = 0u, started_later = 0u;
    for (uint32_t turn = 0u; turn < 8u; turn++) {
        render_camera_t camera;
        sh_camera_around(&camera, (float32_t)turn * SH_PI / 4.0f32 + 0.11f32, -0.3f32, 9.0f64);
        REQUIRE(sh_draw(t, &rig, &camera) != nullptr);
        REQUIRE(device_memory_read(memory, &render->owned_hits, 0u, with, bytes) == GPU_STATUS_OK);
        traverse_camera_t view;
        render_camera_to_traverse(&camera, SH_WIDTH, SH_HEIGHT, &view);
        gpu_error_t error = {0};
        REQUIRE(traverse_primary(&error, &world, &view, &targets, false) == GPU_STATUS_OK);
        REQUIRE(traverse_wait(&error) == GPU_STATUS_OK);
        REQUIRE(device_memory_read(memory, &render->owned_hits, 0u, without, bytes) == GPU_STATUS_OK);
        float32_t *const coarse = test_alloc(t, traverse_coarse_bytes(SH_WIDTH, SH_HEIGHT));
        REQUIRE(device_memory_read(memory, &render->owned_coarse, 0u, coarse,
                                   traverse_coarse_bytes(SH_WIDTH, SH_HEIGHT)) == GPU_STATUS_OK);
        for (uint32_t i = 0u; i < traverse_tiles_across(SH_WIDTH) * traverse_tiles_across(SH_HEIGHT); i++) {
            started_later += coarse[i] > 0.0f32 ? 1u : 0u;
        }
        for (uint64_t i = 0u; i < (uint64_t)SH_WIDTH * SH_HEIGHT; i++) {
            rays++;
            const uint32_t face = traverse_hit_face(with[i].info);
            hits += face != TRAVERSE_FACE_MISS ? 1u : 0u;
            if (face != traverse_hit_face(without[i].info) ||
                (face != TRAVERSE_FACE_MISS &&
                 (with[i].t != without[i].t || with[i].value != without[i].value ||
                  memcmp(with[i].offset, without[i].offset, sizeof(with[i].offset)) != 0))) {
                different++;
            }
        }
    }
    printf("  %" PRIu64 " rays, %" PRIu64 " hits, %" PRIu64 " different, %" PRIu64 " tiles started later than 0\n",
           rays, hits, different, started_later);
    CHECK_EQ_U64(different, 0u);
    CHECK(hits > rays / 4u);
    CHECK(started_later > 100u); /* or this says nothing about the pass */
    sh_hand_world_close(&rig);
    sh_rig_end(t, &rig);
}

/* ---- How far a pixel goes through holes -------------------------------------------------------------------------------------- */

/* A straight line of leaves, with the eye set so that the ray through the middle of the picture goes through one hole in each,
 * then a stone. A narrow view keeps every leaf at the finest mip. */
TEST(a_pixel_goes_through_as_many_holes_as_it_may_and_stops_on_the_last) {
    sh_rig_t rig;
    REQUIRE(sh_rig_begin(t, &rig));
    REQUIRE(sh_hand_world_open(t, &rig, 3u, SH_WIDTH, SH_HEIGHT));
    uint32_t id = 0u;
    REQUIRE(texture_find(rig.texture, "blocks/oak_leaves", &id));
    texture_mip_t mip;
    REQUIRE(texture_mip_get(rig.texture, id, 0u, 0u, &mip));
    texture_info_t info;
    REQUIRE(texture_info_get(rig.texture, id, &info));
    /* A hole with holes around it, so that a ray a little off its middle is still in it. */
    int32_t hole_x = -1, hole_y = -1;
    for (uint32_t y = 0u; y < mip.height && hole_x < 0; y++) {
        for (uint32_t x = 0u; x < mip.width && hole_x < 0; x++) {
            if (mip.borrowed_pixels[((uint64_t)y * mip.width + x) * 4u + 3u] == 0u) {
                hole_x = (int32_t)x;
                hole_y = (int32_t)y;
            }
        }
    }
    REQUIRE(hole_x >= 0);
    /* The side towards -x, looked at from the -x side: u = 1 - y, v = 1 - z. */
    const float64_t eye_y = 8.0f64 + 1.0f64 - ((float64_t)hole_x + 0.5f64) / (float64_t)mip.width;
    const float64_t eye_z = 8.0f64 + 1.0f64 - ((float64_t)hole_y + 0.5f64) / (float64_t)mip.height;
    const int32_t first = 0;
    const int32_t count = (int32_t)SHADE_ALPHA_LAYERS; /* of leaves */
    for (int32_t i = 0; i < count; i++) {
        REQUIRE(world_edit_block_set(&rig.world->edit, first + i, 8, 8, BLOCK_ID_OAK_LEAVES) ==
                WORLD_EDIT_STATUS_CHANGED);
    }
    REQUIRE(world_edit_block_set(&rig.world->edit, first + count, 8, 8, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(sh_hand_world_sync(t, &rig));
    render_camera_t camera;
    render_camera_start(&camera, -4.5f64, eye_y, eye_z, 0.0f32, 0.0f32);
    camera.half_fov_tangent = 0.02f32;
    /* Holes all the way, and out of them at last: the sixteenth surface is a hole too, so it stops there, on its mean colour. */
    const uint32_t x = SH_WIDTH / 2u, y = SH_HEIGHT / 2u;
    const float32_t brightness = 0.6f32;
    uint32_t mean_channel[3], stone_channel[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        mean_channel[i] =
            post_tone_map(shade_srgb_to_linear((float32_t)info.borrowed_entry->mean_rgba[i] / 255.0f32) * brightness);
    }
    const uint32_t mean = post_xrgb(mean_channel[0], mean_channel[1], mean_channel[2]);
    const uint8_t *picture = sh_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    CHECK(sh_bytes_near(sh_pixel(picture, SH_WIDTH, x, y), mean, 1u));
    /* With alpha off it is the same colour at the first leaf, and the hit is where it was. */
    render_frame_features_set(&rig.render->frame, SHADE_FEATURE_SHAPES);
    picture = sh_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    CHECK(sh_bytes_near(sh_pixel(picture, SH_WIDTH, x, y), mean, 1u));
    render_frame_features_set(&rig.render->frame, SHADE_FEATURES_ALL);
    /* One leaf fewer, and the stone behind them is the sixteenth surface. It is what the middle of the picture now shows. */
    REQUIRE(world_edit_block_set(&rig.world->edit, first, 8, 8, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(sh_hand_world_sync(t, &rig));
    picture = sh_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    const uint32_t shown = sh_pixel(picture, SH_WIDTH, x, y);
    /* The stone's own texel there: the same place on its side as the hole was on a leaf's. */
    uint32_t stone_id = 0u;
    REQUIRE(texture_find(rig.texture, "blocks/stone", &stone_id));
    texture_mip_t stone_mip;
    REQUIRE(texture_mip_get(rig.texture, stone_id, 0u, 0u, &stone_mip));
    const uint8_t *const texel =
        stone_mip.borrowed_pixels + ((uint64_t)((uint32_t)hole_y * stone_mip.height / mip.height) * stone_mip.width +
                                     (uint32_t)hole_x * stone_mip.width / mip.width) *
                                        4u;
    for (uint32_t i = 0u; i < 3u; i++) {
        stone_channel[i] = post_tone_map(shade_srgb_to_linear((float32_t)texel[i] / 255.0f32) * brightness);
    }
    const uint32_t stone = post_xrgb(stone_channel[0], stone_channel[1], stone_channel[2]);
    printf("  through 15 leaves: %06" PRIX32 ", the stone: %06" PRIX32 "; on the last hole: %06" PRIX32 "\n", shown,
           stone, mean);
    CHECK(sh_bytes_near(shown, stone, 1u));
    sh_hand_world_close(&rig);
    sh_rig_end(t, &rig);
}

/* ---- Golden pictures -------------------------------------------------------------------------------------------------------- */

static void sh_golden_compare(test_context_t *t, const char *path, const uint8_t *picture) {
    const uint64_t capacity = display_png_bytes_needed(SH_WIDTH, SH_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        uint64_t written = 0u;
        REQUIRE(display_png_encode(picture, SH_WIDTH * 4u, SH_WIDTH, SH_HEIGHT, file, capacity, &written));
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
    REQUIRE(info.width == SH_WIDTH && info.height == SH_HEIGHT);
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    REQUIRE(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK);
    uint64_t wrong = 0u, exact = 0u;
    for (uint32_t y = 0u; y < SH_HEIGHT; y++) {
        for (uint32_t x = 0u; x < SH_WIDTH; x++) {
            const uint64_t i = (uint64_t)y * SH_WIDTH + x;
            const uint32_t ours = sh_pixel(picture, SH_WIDTH, x, y);
            const uint32_t golden = post_xrgb(rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u]);
            wrong += sh_bytes_near(ours, golden, SH_GOLDEN_TOLERANCE) ? 0u : 1u;
            exact += ours == golden ? 1u : 0u;
        }
    }
    printf("  %s: %" PRIu64 " of %" PRIu64 " pixels exact, %" PRIu64 " past the tolerance\n", path, exact,
           (uint64_t)SH_WIDTH * SH_HEIGHT, wrong);
    CHECK_EQ_U64(wrong, 0u);
}

TEST(golden_pictures_match) {
    sh_rig_t rig;
    REQUIRE(sh_rig_begin(t, &rig));
    REQUIRE(sh_hand_world_open(t, &rig, 2u, SH_WIDTH, SH_HEIGHT));
    REQUIRE(sh_scene_put(t, &rig));
    render_camera_t camera;
    sh_camera_around(&camera, 1.2f32, -0.55f32, 12.0f64);
    const uint8_t *picture = sh_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    sh_golden_compare(t, "tests/cuda/shapes/fixtures/all_shapes.png", picture);
    /* Near, and low, where a hole's edge and a plate's thickness show. */
    render_camera_start(&camera, 6.2f64, 3.4f64, 1.3f64, 1.05f32, -0.3f32);
    picture = sh_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    sh_golden_compare(t, "tests/cuda/shapes/fixtures/near.png", picture);
    sh_hand_world_close(&rig);
    sh_rig_end(t, &rig);
}

/* ---- What it costs, on the card's own clock -------------------------------------------------------------------------------- */

typedef struct sh_cost_t {
    float64_t primary_ms; /* 0 */
    float64_t shade_ms;   /* 8 */
    float64_t total_ms;   /* 16 */
} sh_cost_t;
static_assert(sizeof(sh_cost_t) == 24);

static void sh_cost_measure(test_context_t *t, sh_rig_t *rig, const render_camera_t *camera, uint32_t features,
                            sh_cost_t *out) {
    render_frame_t *const frame = &rig->render->frame;
    render_frame_features_set(frame, features);
    REQUIRE(sh_draw(t, rig, camera) != nullptr); /* a first frame pays for what the others find ready */
    const render_frame_stats_t before = frame->stats;
    for (uint32_t i = 0u; i < SH_COST_FRAMES; i++) {
        REQUIRE(sh_draw(t, rig, camera) != nullptr);
    }
    const render_frame_stats_t *const after = &frame->stats;
    const float64_t frames = (float64_t)SH_COST_FRAMES;
    out->primary_ms = (after->sum_ms[FRAME_PASS_PRIMARY] - before.sum_ms[FRAME_PASS_PRIMARY]) / frames;
    out->shade_ms = (after->sum_ms[FRAME_PASS_SHADE] - before.sum_ms[FRAME_PASS_SHADE]) / frames;
    out->total_ms = (after->sum_total_ms - before.sum_total_ms) / frames;
}

static void sh_cost_row(test_context_t *t, sh_rig_t *rig, const render_camera_t *camera, const char *where) {
    const uint32_t settings[] = {0u, SHADE_FEATURE_SHAPES, SHADE_FEATURE_ALPHA, SHADE_FEATURES_ALL};
    const char *const names[] = {"all off", "shapes", "alpha", "both"};
    for (uint32_t s = 0u; s < 4u; s++) {
        sh_cost_t cost = {0};
        sh_cost_measure(t, rig, camera, settings[s], &cost);
        printf("  cost, %s, %" PRIu32 " x %" PRIu32 ", %-7s: primary %7.3f ms, shade %7.3f ms, frame %7.3f ms\n", where,
               rig->render->frame.width, rig->render->frame.height, names[s], (libc_double_t)cost.primary_ms,
               (libc_double_t)cost.shade_ms, (libc_double_t)cost.total_ms);
        CHECK(cost.total_ms > 0.0f64);
    }
}

/* Real terrain, then the worst case on purpose: nothing but plants and leaves, as far as can be seen. Nothing here is a limit:
 * the numbers go in the stage file. The switch has to do something on real terrain, or none of it was looked at. */
TEST(the_cost_is_measured_on_real_terrain_and_on_a_field_of_plants) {
    sh_rig_t rig;
    REQUIRE(sh_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = SH_COST_RENDER_DISTANCE,
                                  .width = SH_COST_WIDTH,
                                  .height = SH_COST_HEIGHT,
                                  .features_off = 0u,
                                  ._pad0 = {0}};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    render_t *const render = rig.render;
    render_camera_t camera = game->camera;
    camera.pitch = -0.35f32;
    sh_cost_row(t, &rig, &camera, "terrain");
    /* What the setting changes, here. */
    device_memory_t *const memory = &rig.bridge->device_memory;
    const uint64_t bytes = (uint64_t)SH_COST_WIDTH * SH_COST_HEIGHT * 4u;
    uint8_t *const both = test_alloc(t, bytes);
    render_frame_features_set(&render->frame, SHADE_FEATURES_ALL);
    const uint8_t *picture = sh_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    memcpy(both, picture, bytes);
    uint64_t shaped = 0u;
    render_frame_features_set(&render->frame, 0u);
    picture = sh_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    const uint64_t hit_bytes = traverse_hits_bytes(SH_COST_WIDTH, SH_COST_HEIGHT);
    traverse_hit_t *const hits = test_alloc(t, hit_bytes);
    REQUIRE(device_memory_read(memory, &render->owned_hits, 0u, hits, hit_bytes) == GPU_STATUS_OK);
    uint64_t changed = 0u;
    for (uint64_t i = 0u; i < (uint64_t)SH_COST_WIDTH * SH_COST_HEIGHT; i++) {
        changed += memcmp(both + i * 4u, picture + i * 4u, 3u) != 0 ? 1u : 0u;
        const uint32_t material = hits[i].value & TRAVERSE_VALUE_MATERIAL_MASK;
        if (traverse_hit_face(hits[i].info) != TRAVERSE_FACE_MISS && material < BLOCK_COUNT) {
            const uint32_t shape = block_defs[material].shape;
            shaped += shape != BLOCK_SHAPE_CUBE && shape != BLOCK_SHAPE_FLUID ? 1u : 0u;
        }
    }
    printf("  terrain, setting on against off: %" PRIu64 " pixels of %" PRIu64 " change, %" PRIu64
           " stop on something not a cube\n",
           changed, (uint64_t)SH_COST_WIDTH * SH_COST_HEIGHT, shaped);
    CHECK(shaped > 0u);
    CHECK(changed > 0u);
    game_close(game);

    REQUIRE(sh_hand_world_open(t, &rig, 3u, SH_COST_WIDTH, SH_COST_HEIGHT));
    for (int32_t y = -32; y < 32; y++) {
        for (int32_t x = -32; x < 32; x++) {
            REQUIRE(world_edit_block_set(&rig.world->edit, x, y, -1, BLOCK_ID_GRASS_BLOCK) ==
                    WORLD_EDIT_STATUS_CHANGED);
            const bool leaf = ((x + y * 3) & 7) == 0;
            const uint16_t block = leaf ? (uint16_t)BLOCK_ID_OAK_LEAVES : (uint16_t)BLOCK_ID_TALL_GRASS;
            REQUIRE(world_edit_block_set(&rig.world->edit, x, y, 0, block) == WORLD_EDIT_STATUS_CHANGED);
        }
    }
    REQUIRE(sh_hand_world_sync(t, &rig));
    render_camera_start(&camera, -30.5f64, 0.5f64, 1.6f64, 0.0f32, -0.05f32);
    sh_cost_row(t, &rig, &camera, "plants ");
    sh_hand_world_close(&rig);
    sh_rig_end(t, &rig);
}

/* ---- The built program ------------------------------------------------------------------------------------------------------ */

typedef struct sh_temp_dir_t {
    char path[64]; /* 0 */
} sh_temp_dir_t;
static_assert(sizeof(sh_temp_dir_t) == 64);

static bool sh_temp_dir_make(sh_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_render_shapes_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void sh_temp_dir_remove(const sh_temp_dir_t *dir) {
    DIR *const handle = opendir(dir->path);
    if (handle != nullptr) {
        char path[512];
        for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
            if (entry->d_name[0] != '.') {
                snprintf(path, sizeof(path), "%s/%s", dir->path, entry->d_name);
                unlink(path);
            }
        }
        closedir(handle);
    }
    rmdir(dir->path);
}

/* Looks a good way down at the ground, in a picture wide enough for a plant or two to be in it. */
static const char sh_script[] = "0 move 0 400\n"
                                "2 screenshot view.png\n"
                                "3 end\n";

static void sh_program_run(test_context_t *t, const sh_temp_dir_t *dir, const char *script, const char *flag,
                           test_process_t *out) {
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--test-world",
                          (char *)"--render-distance",
                          (char *)"6",
                          (char *)"--script",
                          (char *)script,
                          (char *)"--screenshot-dir",
                          (char *)dir->path,
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"360",
                          (char *)flag,
                          nullptr};
    test_process_run(t, argv, SH_PROGRAM_TIMEOUT_MS, out);
}

static uint8_t *sh_program_picture(test_context_t *t, const sh_temp_dir_t *dir, uint64_t *out_length) {
    char path[256];
    snprintf(path, sizeof(path), "%s/view.png", dir->path);
    const uint64_t capacity = display_png_bytes_needed(640u, 360u);
    uint8_t *const bytes = test_alloc(t, capacity);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, capacity, out_length), FILE_OK)) {
        *out_length = 0u;
    }
    return bytes;
}

TEST(the_program_takes_the_settings_and_they_change_the_picture) {
    sh_temp_dir_t on, off, again;
    REQUIRE(sh_temp_dir_make(&on));
    REQUIRE(sh_temp_dir_make(&off));
    REQUIRE(sh_temp_dir_make(&again));
    char script[128];
    snprintf(script, sizeof(script), "%s/view.script", on.path);
    REQUIRE(file_write_atomic(script, sh_script, sizeof(sh_script) - 1u) == FILE_OK);
    test_process_t run;
    sh_program_run(t, &on, script, nullptr, &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    sh_program_run(t, &off, script, "--no-shapes", &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    sh_program_run(t, &again, script, "--no-alpha", &run);
    REQUIRE(run.started != 0u);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
    uint64_t on_bytes = 0u, off_bytes = 0u, again_bytes = 0u;
    const uint8_t *const on_picture = sh_program_picture(t, &on, &on_bytes);
    const uint8_t *const off_picture = sh_program_picture(t, &off, &off_bytes);
    const uint8_t *const again_picture = sh_program_picture(t, &again, &again_bytes);
    REQUIRE(on_bytes == off_bytes && on_bytes == again_bytes && on_bytes != 0u);
    CHECK(memcmp(on_picture, off_picture, on_bytes) != 0);
    CHECK(memcmp(on_picture, again_picture, on_bytes) != 0);
    sh_temp_dir_remove(&on);
    sh_temp_dir_remove(&off);
    sh_temp_dir_remove(&again);
    /* They mean nothing without a world to draw. */
    char *const alone[] = {t->borrowed_args[0], (char *)"--headless",  (char *)"--ticks",
                           (char *)"1",         (char *)"--no-shapes", nullptr};
    test_process_run(t, alone, SH_PROGRAM_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
    CHECK_CONTAINS(run.output, "only mean something with --test-world");
}

TEST_MAIN()
