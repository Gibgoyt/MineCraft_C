/* ctest render_feature_water. See README.md. */
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
    WA_WIDTH = 320u,
    WA_HEIGHT = 180u,
    WA_GOLDEN_TOLERANCE = 2u,
    WA_PROGRAM_TIMEOUT_MS = 120000u,
    WA_COST_FRAMES = 30u,
    WA_COST_WIDTH = 1920u,
    WA_COST_HEIGHT = 1080u,
    WA_COST_RENDER_DISTANCE = 10u,
    WA_REACH_STEPS = 300u,
    /* All of this stage's. */
    WA_FEATURES_OURS = SHADE_FEATURE_TRANSPARENCY | SHADE_FEATURE_WATER_REFLECTION | SHADE_FEATURE_WATER_TINT,
    WA_FEATURES_BEFORE = SHADE_FEATURES_ALL & ~(uint32_t)WA_FEATURES_OURS,
    /* The reference world: a box of cells, from z = WA_GRID_Z_LOW up. */
    WA_GRID_SIDE = 32u,
    WA_GRID_TALL = 26u,
    WA_GRID_CELLS = WA_GRID_SIDE * WA_GRID_SIDE * WA_GRID_TALL,
};
enum : int32_t { WA_GRID_Z_LOW = -13 };

#define WA_MORNING 0.31f32
#define WA_NOON 0.5f32
#define WA_EVENING 0.71f32
#define WA_DUSK 0.748f32
#define WA_NIGHT 0.03f32
#define WA_NEAR_NOTHING 1e-5f32

#include "cuda/sky/fixtures/rig.h"

/* ---- The arithmetic, on numbers worked out by hand ------------------------------------------------------------------------- */

TEST(reflection_tint_and_fog_behind_add_up_as_on_paper) {
    /* Straight on, water's own number. Along it, everything. Half way, a thirty-second of what is left. */
    CHECK_NEAR(shade_water_fresnel(1.0f32), 0.02f32, 0.0f32);
    CHECK_NEAR(shade_water_fresnel(0.0f32), 1.0f32, WA_NEAR_NOTHING);
    CHECK_NEAR(shade_water_fresnel(0.5f32), 0.02f32 + 0.98f32 / 32.0f32, WA_NEAR_NOTHING);
    CHECK_NEAR(shade_water_fresnel(2.0f32), 0.02f32, 0.0f32);
    CHECK_NEAR(shade_water_fresnel(-1.0f32), 1.0f32, WA_NEAR_NOTHING);

    float32_t through[3], given[3];
    shade_water_tint(0.0f32, through, given);
    for (uint32_t i = 0u; i < 3u; i++) {
        CHECK_NEAR(through[i], 1.0f32, 0.0f32);
        CHECK_NEAR(given[i], 0.0f32, 0.0f32);
    }
    /* e^-3, e^-1, e^-0.7. */
    shade_water_tint(10.0f32, through, given);
    CHECK_NEAR(through[0], 0.049787f32, WA_NEAR_NOTHING);
    CHECK_NEAR(through[1], 0.367879f32, WA_NEAR_NOTHING);
    CHECK_NEAR(through[2], 0.496585f32, WA_NEAR_NOTHING);
    CHECK_NEAR(given[0], 0.012f32 * (1.0f32 - 0.049787f32), WA_NEAR_NOTHING);
    CHECK_NEAR(given[2], 0.10f32 * (1.0f32 - 0.496585f32), WA_NEAR_NOTHING);
    /* Red goes first, then green, then blue, or deep water is not blue. */
    CHECK(through[0] < through[1] && through[1] < through[2]);
    /* Where a ray with tint on stops looking, what is behind is under a twenty-fifth of itself in the channel that lasts longest. */
    shade_water_tint(SHADE_WATER_SEEN_BLOCKS, through, given);
    CHECK(through[2] < 0.035f32);

    /* What is behind, with what post then does for the whole pixel, comes to its own distance's fog. */
    const post_settings_t post = {.fog_start = 20.0f32, .fog_end = 30.0f32};
    CHECK_NEAR(shade_fog_behind(&post, 0.0f32, 10.0f32, 15.0f32), 0.0f32, 0.0f32);
    CHECK_NEAR(shade_fog_behind(&post, 0.0f32, 10.0f32, 25.0f32), 0.25f32, WA_NEAR_NOTHING);
    CHECK_NEAR(shade_fog_behind(&post, 0.0f32, 40.0f32, 50.0f32), 0.0f32, 0.0f32);
    const float32_t first = post_fog(&post, 24.0f32), behind = shade_fog_behind(&post, 0.0f32, 24.0f32, 28.0f32);
    CHECK_NEAR(1.0f32 - (1.0f32 - first) * (1.0f32 - behind), post_fog(&post, 28.0f32), WA_NEAR_NOTHING);

    /* The next voxel along, of the same block, come into across the shared face. */
    const int16_t here[3] = {5, 6, 7}, east[3] = {6, 6, 7}, above[3] = {5, 6, 8}, apart[3] = {7, 6, 7};
    CHECK(shade_own_kind_behind(9u, here, 9u, east, TRAVERSE_FACE_NEG_X));
    CHECK(!shade_own_kind_behind(9u, here, 9u, east, TRAVERSE_FACE_POS_X));
    CHECK(!shade_own_kind_behind(9u, here, 8u, east, TRAVERSE_FACE_NEG_X));
    CHECK(shade_own_kind_behind(9u, here, 9u, above, TRAVERSE_FACE_NEG_Z));
    CHECK(!shade_own_kind_behind(9u, here, 9u, apart, TRAVERSE_FACE_NEG_X));
    CHECK(!shade_own_kind_behind(9u, here, 9u, here, TRAVERSE_FACE_INSIDE));
    CHECK(shade_own_kind_behind(9u, above, 9u, here, TRAVERSE_FACE_POS_Z));
}

/* ---- An exact reference, written apart from the kernels ----------------------------------------------------------------- */

typedef struct wa_scene_t {
    uint16_t states[WA_GRID_CELLS];            /* 0: every voxel a whole cube, in this test */
    uint8_t lights[WA_GRID_CELLS];             /* 53248 */
    uint32_t texture_ids[BLOCK_TEXTURE_COUNT]; /* 79872: the blob's, by the block table's order */
} wa_scene_t;
static_assert(sizeof(wa_scene_t) == 79872u + 4u * BLOCK_TEXTURE_COUNT);

static bool wa_in_grid(const int32_t cell[3]) {
    return cell[0] >= 0 && cell[0] < (int32_t)WA_GRID_SIDE && cell[1] >= 0 && cell[1] < (int32_t)WA_GRID_SIDE &&
           cell[2] >= WA_GRID_Z_LOW && cell[2] < WA_GRID_Z_LOW + (int32_t)WA_GRID_TALL;
}

static uint32_t wa_index(const int32_t cell[3]) {
    return ((uint32_t)(cell[2] - WA_GRID_Z_LOW) * WA_GRID_SIDE + (uint32_t)cell[1]) * WA_GRID_SIDE + (uint32_t)cell[0];
}

static uint32_t wa_state_at(const wa_scene_t *scene, const int32_t cell[3]) {
    return wa_in_grid(cell) ? scene->states[wa_index(cell)] : (uint32_t)BLOCK_ID_AIR;
}

/* Into the picture's list and the world. */
static bool wa_put(test_context_t *t, sk_rig_t *rig, wa_scene_t *scene, int32_t x, int32_t y, int32_t z,
                   uint32_t state) {
    const int32_t cell[3] = {x, y, z};
    if (!CHECK(wa_in_grid(cell)) ||
        !CHECK_EQ_U64(world_edit_block_set(&rig->world->edit, x, y, z, (uint16_t)state), WORLD_EDIT_STATUS_CHANGED)) {
        return false;
    }
    scene->states[wa_index(cell)] = (uint16_t)state;
    scene->lights[wa_index(cell)] = SHADE_LIGHT_OPEN_SKY;
    return true;
}

static bool wa_scene_begin(test_context_t *t, const sk_rig_t *rig, wa_scene_t *scene) {
    memset(scene, 0, sizeof(*scene));
    for (uint32_t i = 0u; i < BLOCK_TEXTURE_COUNT; i++) {
        if (!CHECK(texture_find(rig->texture, block_texture_names[i], &scene->texture_ids[i]))) {
            return false;
        }
    }
    return true;
}

/* The next cube a ray from eye enters after a distance, by walking the cells from the eye in float64_t. inside says the eye is in it
 * and the caller let that count. */
typedef struct wa_found_t {
    float64_t t;      /* 0 */
    int32_t cell[3];  /* 8 */
    uint32_t state;   /* 20 */
    uint32_t face;    /* 24 */
    uint8_t found;    /* 28 */
    uint8_t _pad0[3]; /* 29 */
} wa_found_t;
static_assert(sizeof(wa_found_t) == 32);

static void wa_cell_walk(const wa_scene_t *scene, const float64_t eye[3], const float64_t direction[3], float64_t after,
                         bool may_be_inside, wa_found_t *out) {
    memset(out, 0, sizeof(*out));
    int32_t cell[3];
    int32_t step[3];
    float64_t along[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        cell[i] = (int32_t)floor(eye[i]);
        along[i] = fabs(direction[i]) < 1e-12f64 ? 1e-12f64 : direction[i];
        step[i] = along[i] > 0.0f64 ? 1 : -1;
    }
    float64_t entered = -1e30f64;
    uint32_t face = (uint32_t)TRAVERSE_FACE_INSIDE;
    for (uint32_t s = 0u; s < WA_REACH_STEPS; s++) {
        const uint32_t state = wa_state_at(scene, cell);
        if (state != BLOCK_ID_AIR) {
            const bool started_in = s == 0u;
            if (started_in ? may_be_inside : entered > after) {
                out->found = 1u;
                out->state = state;
                out->face = face;
                out->t = started_in ? 0.0f64 : entered;
                memcpy(out->cell, cell, sizeof(cell));
                return;
            }
        }
        /* Each plane from the eye, so that where a ray enters a cell is the same number however it got there. */
        uint32_t axis = 0u;
        float64_t nearest = 1e30f64;
        for (uint32_t i = 0u; i < 3u; i++) {
            const float64_t plane = (float64_t)cell[i] + (step[i] > 0 ? 1.0f64 : 0.0f64);
            const float64_t at = (plane - eye[i]) / along[i];
            if (at < nearest) {
                nearest = at;
                axis = i;
            }
        }
        entered = nearest;
        cell[axis] += step[axis];
        face = axis * 2u + (step[axis] > 0 ? 0u : 1u);
    }
}

/* What one ray must show. */
typedef struct wa_seen_t {
    uint64_t signature;     /* 0: every surface on its way, to tell one ray's story from a neighbour's */
    uint32_t expected;      /* 8: the pixel */
    uint32_t water_entered; /* 12 */
    uint32_t glass_passed;  /* 16 */
    uint32_t ice_passed;    /* 20 */
    uint32_t sun_in_water;  /* 24 */
    uint32_t mirrored_hit;  /* 28 */
    uint32_t cut_short;     /* 32 */
    uint8_t hit;            /* 36 */
    uint8_t usable;         /* 37: nothing on its way was near enough an edge for rounding to choose */
    uint8_t _pad0[2];       /* 38 */
} wa_seen_t;
static_assert(sizeof(wa_seen_t) == 40);

static void wa_note(wa_seen_t *out, const wa_found_t *found) {
    const uint32_t place = wa_in_grid(found->cell) ? ((uint32_t)found->cell[2] + 64u) * 4096u +
                                                         (uint32_t)found->cell[1] * 64u + (uint32_t)found->cell[0]
                                                   : 0u;
    out->signature = out->signature * 1000003u + (uint64_t)place * 8u + found->face + 1u;
}

/* Texture coordinates, written apart from the kernel's. */
static void wa_uv(uint32_t face, const float64_t local[3], float64_t *out_u, float64_t *out_v) {
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

static bool wa_near_whole(float64_t value, float64_t margin) {
    return fabs(value - round(value)) < margin;
}

/* The texel at a point of a found face, seen from distance_seen. */
static const uint8_t *wa_texel(test_context_t *t, const sk_rig_t *rig, const wa_scene_t *scene,
                               const traverse_camera_t *view, const wa_found_t *found, const float64_t point[3],
                               float64_t distance_seen, const texture_blob_entry_t **out_entry, wa_seen_t *out) {
    float64_t local[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        local[i] = point[i] - (float64_t)found->cell[i];
    }
    float64_t u = 0.0f64, v = 0.0f64;
    wa_uv(found->face, local, &u, &v);
    const uint32_t look = found->face == TRAVERSE_FACE_INSIDE ? (uint32_t)TRAVERSE_FACE_POS_Z : found->face;
    const uint32_t id = scene->texture_ids[block_defs[found->state].faces[look]];
    texture_info_t info;
    if (!CHECK(texture_info_get(rig->texture, id, &info))) {
        return nullptr;
    }
    const texture_blob_entry_t *const entry = info.borrowed_entry;
    const float32_t tangent = shade_pixel_tangent(view);
    const uint32_t level = shade_mip_level((float32_t)distance_seen, tangent, entry->width, entry->level_count);
    texture_mip_t mip;
    if (!CHECK(texture_mip_get(rig->texture, id, level, 0u, &mip))) {
        return nullptr;
    }
    const float32_t per_pixel = (float32_t)distance_seen * tangent * (float32_t)entry->width;
    if (wa_near_whole(u * (float64_t)mip.width, 0.02f64) || wa_near_whole(v * (float64_t)mip.height, 0.02f64) ||
        (per_pixel > 1.0f32 && wa_near_whole((float64_t)log2f(per_pixel) + 0.5f64, 0.005f64))) {
        out->usable = 0u;
    }
    *out_entry = entry;
    const uint32_t x = shade_texel_of((float32_t)u, mip.width), y = shade_texel_of((float32_t)v, mip.height);
    return mip.borrowed_pixels + ((uint64_t)y * mip.width + x) * 4u;
}

static bool wa_next_to(const int32_t last[3], const int32_t cell[3], uint32_t face) {
    if (face >= 6u) {
        return false;
    }
    const uint32_t axis = face >> 1;
    for (uint32_t i = 0u; i < 3u; i++) {
        const int32_t towards = i != axis ? 0 : ((face & 1u) != 0u ? 1 : -1);
        if (cell[i] + towards != last[i]) {
            return false;
        }
    }
    return true;
}

/* How a ray leaves a body of water it is in, cell by cell. */
typedef struct wa_walk_t {
    float64_t last_in;  /* 0 */
    float64_t out;      /* 8 */
    int32_t last[3];    /* 16 */
    uint32_t cut_short; /* 28 */
} wa_walk_t;
static_assert(sizeof(wa_walk_t) == 32);

static void wa_water_walk(const wa_scene_t *scene, const float64_t eye[3], const float64_t direction[3],
                          const wa_found_t *from, float64_t reach, wa_walk_t *out) {
    int32_t cell[3];
    memcpy(cell, from->cell, sizeof(cell));
    memcpy(out->last, from->cell, sizeof(cell));
    out->last_in = from->t;
    out->out = from->t;
    out->cut_short = 0u;
    for (uint32_t s = 0u; s < WA_REACH_STEPS; s++) {
        float64_t nearest = 1e30f64;
        uint32_t axis = 0u;
        for (uint32_t i = 0u; i < 3u; i++) {
            if (fabs(direction[i]) < 1e-12f64) {
                continue;
            }
            const float64_t plane = (float64_t)cell[i] + (direction[i] > 0.0f64 ? 1.0f64 : 0.0f64);
            const float64_t at = (plane - eye[i]) / direction[i];
            if (at < nearest) {
                nearest = at;
                axis = i;
            }
        }
        out->out = nearest;
        if (nearest - from->t >= reach) {
            out->cut_short = 1u;
            return;
        }
        cell[axis] += direction[axis] > 0.0f64 ? 1 : -1;
        if (wa_state_at(scene, cell) != BLOCK_ID_WATER) {
            return;
        }
        out->last_in = nearest;
        memcpy(out->last, cell, sizeof(cell));
    }
}

/* What is needed to shade a ray in this test. */
typedef struct wa_ask_t {
    const sk_rig_t *rig;           /* 0 */
    const wa_scene_t *scene;       /* 8 */
    const traverse_camera_t *view; /* 16 */
    uint32_t features;             /* 24 */
    uint8_t _pad0[4];              /* 28 */
} wa_ask_t;
static_assert(sizeof(wa_ask_t) == 32);

/* The one sun ray. */
static float32_t wa_sun_seen(test_context_t *t, const wa_ask_t *ask, const float64_t point[3], uint32_t face,
                             float64_t distance_seen, wa_seen_t *out) {
    const shade_sun_t *const sun = &ask->rig->render->frame.scene.sun;
    const bool see_through = (ask->features & SHADE_FEATURE_TRANSPARENCY) != 0u;
    const bool tint = (ask->features & SHADE_FEATURE_WATER_TINT) != 0u;
    float64_t origin[3], direction[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        origin[i] = point[i];
        direction[i] = (float64_t)sun->direction[i];
    }
    const float64_t bias = (float64_t)shade_sun_bias((float32_t)distance_seen);
    origin[face >> 1] += (face & 1u) != 0u ? bias : -bias;
    float32_t seen = 1.0f32;
    float64_t after = 0.0f64;
    uint32_t last_state = BLOCK_ID_AIR;
    int32_t last[3] = {0, 0, 0};
    for (uint32_t layer = 0u; layer < SHADE_SUN_LAYERS; layer++) {
        wa_found_t found;
        wa_cell_walk(ask->scene, origin, direction, after, false, &found);
        if (found.found == 0u) {
            return seen;
        }
        wa_note(out, &found);
        after = found.t;
        if (see_through && found.state == BLOCK_ID_WATER) {
            wa_walk_t walk;
            wa_water_walk(ask->scene, origin, direction, &found, tint ? (float64_t)SHADE_WATER_SEEN_BLOCKS : 1e30f64,
                          &walk);
            if (walk.cut_short != 0u) {
                return 0.0f32;
            }
            out->sun_in_water++;
            if (tint) {
                seen *= expf(-SHADE_WATER_ABSORB_SUN * (float32_t)(walk.out - found.t));
            }
            after = walk.last_in;
            last_state = found.state;
            memcpy(last, walk.last, sizeof(last));
            continue;
        }
        if (see_through && found.state == last_state && wa_next_to(last, found.cell, found.face)) {
            memcpy(last, found.cell, sizeof(last));
            continue;
        }
        float64_t on_it[3];
        for (uint32_t i = 0u; i < 3u; i++) {
            on_it[i] = origin[i] + direction[i] * found.t;
        }
        const texture_blob_entry_t *entry = nullptr;
        const uint8_t *const texel =
            wa_texel(t, ask->rig, ask->scene, ask->view, &found, on_it, distance_seen, &entry, out);
        if (texel == nullptr || texel[3] == SHADE_ALPHA_OPAQUE) {
            return 0.0f32;
        }
        seen *= 1.0f32 - (float32_t)texel[3] / 255.0f32;
        last_state = texel[3] != 0u || found.state == BLOCK_ID_GLASS ? found.state : (uint32_t)BLOCK_ID_AIR;
        memcpy(last, found.cell, sizeof(last));
    }
    return 0.0f32;
}

static void wa_mix_in(float32_t sum[3], const float32_t through[3], const float32_t colour[3], const float32_t sky[3],
                      float32_t fog, float32_t weight) {
    for (uint32_t i = 0u; i < 3u; i++) {
        sum[i] += through[i] * weight * (colour[i] + (sky[i] - colour[i]) * fog);
    }
}

/* The one ray off a surface of water. */
static void wa_reflection(test_context_t *t, const wa_ask_t *ask, const float64_t point[3], uint32_t face,
                          const float64_t direction[3], float64_t distance, float64_t first, wa_seen_t *out,
                          float32_t colour[3]) {
    const render_frame_t *const frame = &ask->rig->render->frame;
    const post_settings_t *const post = &frame->post;
    const uint32_t axis = face >> 1;
    float64_t origin[3], mirrored[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        origin[i] = point[i];
        mirrored[i] = i == axis ? -direction[i] : direction[i];
    }
    const float64_t bias = (float64_t)shade_sun_bias((float32_t)distance);
    origin[axis] += (face & 1u) != 0u ? bias : -bias;
    const float32_t unit[3] = {(float32_t)mirrored[0], (float32_t)mirrored[1], (float32_t)mirrored[2]};
    float32_t sky[3];
    post_sky(post, unit, sky);
    float64_t after = 0.0f64;
    uint32_t last_state = BLOCK_ID_AIR;
    int32_t last[3] = {0, 0, 0};
    for (uint32_t layer = 0u;; layer++) {
        wa_found_t found;
        wa_cell_walk(ask->scene, origin, mirrored, after, false, &found);
        if (found.found == 0u) {
            post_sky_discs(post, unit, sky);
            memcpy(colour, sky, sizeof(sky));
            return;
        }
        wa_note(out, &found);
        const float64_t whole_way = distance + found.t;
        float64_t on_it[3];
        for (uint32_t i = 0u; i < 3u; i++) {
            on_it[i] = origin[i] + mirrored[i] * found.t;
        }
        const texture_blob_entry_t *entry = nullptr;
        const uint8_t *const texel =
            wa_texel(t, ask->rig, ask->scene, ask->view, &found, on_it, whole_way, &entry, out);
        REQUIRE(texel != nullptr);
        const bool last_layer = layer + 1u >= SHADE_REFLECTION_LAYERS;
        const bool see_through = (ask->features & SHADE_FEATURE_TRANSPARENCY) != 0u;
        const bool behind =
            see_through && !last_layer && found.state == last_state && wa_next_to(last, found.cell, found.face);
        if (behind || (texel[3] == 0u && !last_layer)) {
            last_state = found.state == BLOCK_ID_GLASS ? found.state : (uint32_t)BLOCK_ID_AIR;
            memcpy(last, found.cell, sizeof(last));
            after = found.t;
            continue;
        }
        out->mirrored_hit = 1u;
        float32_t light[3];
        shade_surface_light(&frame->scene.sun, found.face, ask->scene->lights[wa_index(found.cell)], 1.0f32, light);
        const float32_t fog = shade_fog_behind(post, (float32_t)direction[2], (float32_t)first, (float32_t)whole_way);
        const float32_t cover = (float32_t)texel[3] / 255.0f32;
        for (uint32_t i = 0u; i < 3u; i++) {
            const float32_t shown = shade_srgb_to_linear((float32_t)texel[i] / 255.0f32);
            const float32_t mean = shade_srgb_to_linear((float32_t)entry->mean_rgba[i] / 255.0f32);
            const float32_t lit = (texel[3] == 255u ? shown : mean + (shown - mean) * cover) * light[i];
            colour[i] = lit + (sky[i] - lit) * fog;
        }
        return;
    }
}

/* Through the cells from surface to surface in float64_t, near to far. What adds up the light, the sky and the tone map are the same
 * lines as the card's, which tests/cuda/sky/ holds to paper, and the top of this file holds this stage's. */
static void wa_trace(test_context_t *t, const wa_ask_t *ask, const float64_t eye[3], const float64_t direction[3],
                     wa_seen_t *out) {
    const render_frame_t *const frame = &ask->rig->render->frame;
    const post_settings_t *const post = &frame->post;
    const shade_sun_t *const sun = &frame->scene.sun;
    const uint32_t features = ask->features;
    const bool see_through = (features & SHADE_FEATURE_TRANSPARENCY) != 0u;
    const bool tint = (features & SHADE_FEATURE_WATER_TINT) != 0u;
    memset(out, 0, sizeof(*out));
    out->usable = 1u;
    const float32_t unit[3] = {(float32_t)direction[0], (float32_t)direction[1], (float32_t)direction[2]};
    float32_t sky[3];
    post_sky(post, unit, sky);
    float32_t sum[3] = {0.0f32, 0.0f32, 0.0f32}, through[3] = {1.0f32, 1.0f32, 1.0f32};
    float64_t first = 0.0f64, after = 0.0f64;
    bool drawn = false;
    uint32_t last_state = BLOCK_ID_AIR;
    int32_t last[3] = {0, 0, 0};
    bool done = false;
    for (uint32_t layer = 0u; !done; layer++) {
        wa_found_t found;
        wa_cell_walk(ask->scene, eye, direction, after, layer == 0u, &found);
        if (found.found == 0u) {
            if (drawn) {
                post_sky_discs(post, unit, sky);
                const float32_t none[3] = {0.0f32, 0.0f32, 0.0f32};
                wa_mix_in(sum, through, sky, none, 0.0f32, 1.0f32);
            }
            break;
        }
        wa_note(out, &found);
        float64_t point[3];
        for (uint32_t i = 0u; i < 3u; i++) {
            point[i] = eye[i] + direction[i] * found.t;
        }
        const texture_blob_entry_t *entry = nullptr;
        const uint8_t *const texel = wa_texel(t, ask->rig, ask->scene, ask->view, &found, point, found.t, &entry, out);
        REQUIRE(texel != nullptr);
        const bool may_go_on = layer + 1u < shade_alpha_layers_at((float32_t)found.t);
        const bool own_kind = see_through && found.state == last_state && wa_next_to(last, found.cell, found.face);
        const bool is_water = see_through && may_go_on && found.state == BLOCK_ID_WATER;
        float64_t next = found.t;
        int32_t then[3];
        memcpy(then, found.cell, sizeof(then));
        if (own_kind && may_go_on && !is_water) {
            /* Nothing to draw. */
        } else if (texel[3] == 0u && may_go_on) {
            last_state = found.state == BLOCK_ID_GLASS ? found.state : (uint32_t)BLOCK_ID_AIR;
            out->glass_passed += found.state == BLOCK_ID_GLASS ? 1u : 0u;
        } else {
            first = drawn ? first : found.t;
            const float32_t fog =
                drawn ? shade_fog_behind(post, (float32_t)direction[2], (float32_t)first, (float32_t)found.t) : 0.0f32;
            drawn = true;
            out->hit = 1u;
            const uint32_t stored = ask->scene->lights[wa_index(found.cell)];
            const bool part = see_through && may_go_on && texel[3] != SHADE_ALPHA_OPAQUE;
            const bool has_face = !is_water || (found.face != TRAVERSE_FACE_INSIDE && !own_kind);
            float32_t light[3] = {0.0f32, 0.0f32, 0.0f32};
            if (has_face) {
                float32_t sun_seen = 1.0f32;
                if ((features & SHADE_FEATURE_SHADOW) != 0u && shade_sun_towards(sun, found.face, stored) > 0.0f32 &&
                    sun->direct[0] + sun->direct[1] + sun->direct[2] > 0.0f32) {
                    sun_seen = wa_sun_seen(t, ask, point, found.face, found.t, out);
                }
                shade_surface_light(sun, found.face, stored, sun_seen, light);
            }
            if (is_water && has_face && (features & SHADE_FEATURE_WATER_REFLECTION) != 0u) {
                const float32_t share = shade_water_fresnel(fabsf(unit[found.face >> 1]));
                float32_t mirror[3];
                wa_reflection(t, ask, point, found.face, direction, found.t, first, out, mirror);
                wa_mix_in(sum, through, mirror, sky, 0.0f32, share);
                for (uint32_t i = 0u; i < 3u; i++) {
                    through[i] *= 1.0f32 - share;
                }
            }
            if (!is_water && !part) {
                const float32_t cover = (float32_t)texel[3] / 255.0f32;
                float32_t colour[3];
                for (uint32_t i = 0u; i < 3u; i++) {
                    const float32_t shown = shade_srgb_to_linear((float32_t)texel[i] / 255.0f32);
                    const float32_t mean = shade_srgb_to_linear((float32_t)entry->mean_rgba[i] / 255.0f32);
                    colour[i] = (texel[3] == 255u ? shown : mean + (shown - mean) * cover) * light[i];
                }
                wa_mix_in(sum, through, colour, sky, fog, 1.0f32);
                done = true;
                break;
            }
            if (has_face) {
                const float32_t cover = (float32_t)texel[3] / 255.0f32;
                float32_t colour[3];
                for (uint32_t i = 0u; i < 3u; i++) {
                    colour[i] = shade_srgb_to_linear((float32_t)texel[i] / 255.0f32) * light[i];
                }
                wa_mix_in(sum, through, colour, sky, fog, cover);
                for (uint32_t i = 0u; i < 3u; i++) {
                    through[i] *= 1.0f32 - cover;
                }
            }
            out->ice_passed += !is_water ? 1u : 0u;
            last_state = found.state;
            if (is_water) {
                out->water_entered++;
                wa_walk_t walk;
                wa_water_walk(ask->scene, eye, direction, &found, tint ? (float64_t)SHADE_WATER_SEEN_BLOCKS : 1e30f64,
                              &walk);
                /* Where it came out is part of this ray's story: a neighbour who leaves by another voxel is not this ray. */
                wa_found_t left = found;
                memcpy(left.cell, walk.last, sizeof(left.cell));
                wa_note(out, &left);
                if (tint) {
                    float32_t full[3], passed[3], given[3];
                    shade_surface_light(sun, TRAVERSE_FACE_POS_Z, stored, 1.0f32, full);
                    shade_water_tint((float32_t)(walk.out - found.t), passed, given);
                    for (uint32_t i = 0u; i < 3u; i++) {
                        given[i] *= full[i];
                    }
                    wa_mix_in(sum, through, given, sky, fog, 1.0f32);
                    for (uint32_t i = 0u; i < 3u; i++) {
                        through[i] *= passed[i];
                    }
                }
                if (walk.cut_short != 0u) {
                    out->cut_short = 1u;
                    done = true;
                    break;
                }
                next = walk.last_in;
                memcpy(then, walk.last, sizeof(then));
            }
        }
        memcpy(last, then, sizeof(last));
        after = next;
    }
    float32_t seen[3];
    if (drawn) {
        const float32_t fog = post_fog_along(post, (float32_t)direction[2], (float32_t)first);
        for (uint32_t i = 0u; i < 3u; i++) {
            seen[i] = sum[i] + (sky[i] - sum[i]) * fog;
        }
    } else {
        post_sky_discs(post, unit, sky);
        memcpy(seen, sky, sizeof(seen));
    }
    out->expected = post_xrgb(post_tone_map(seen[0]), post_tone_map(seen[1]), post_tone_map(seen[2]));
}

typedef struct wa_tally_t {
    uint64_t compared;      /* 0 */
    uint64_t hits;          /* 8 */
    uint64_t wrong;         /* 16 */
    uint64_t in_water;      /* 24: pixels whose ray went into water */
    uint64_t through_glass; /* 32 */
    uint64_t through_ice;   /* 40 */
    uint64_t sun_in_water;  /* 48 */
    uint64_t mirrored_hit;  /* 56: a reflection that met a block */
    uint64_t cut_short;     /* 64 */
} wa_tally_t;
static_assert(sizeof(wa_tally_t) == 72);

static void wa_compare_picture(test_context_t *t, const wa_ask_t *ask, const render_camera_t *camera,
                               const uint8_t *picture, wa_tally_t *out) {
    traverse_camera_t view;
    render_camera_to_traverse(camera, WA_WIDTH, WA_HEIGHT, &view);
    wa_ask_t here = *ask;
    here.view = &view;
    for (uint32_t py = 0u; py < WA_HEIGHT; py++) {
        for (uint32_t px = 0u; px < WA_WIDTH; px++) {
            const float64_t a = 2.0f64 * ((float64_t)px + 0.5f64) / (float64_t)WA_WIDTH - 1.0f64;
            const float64_t b = 1.0f64 - 2.0f64 * ((float64_t)py + 0.5f64) / (float64_t)WA_HEIGHT;
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
            wa_seen_t seen;
            wa_trace(t, &here, camera->position, direction, &seen);
            bool usable = seen.usable != 0u;
            /* A silhouette, the edge of a shadow or of a reflection: this ray or one a hair from it may go either way. */
            for (uint32_t i = 0u; i < 2u && usable; i++) {
                float64_t nudged[3];
                for (uint32_t k = 0u; k < 3u; k++) {
                    const float64_t sideways = (i == 0u ? 1.0f64 : -1.0f64) * 0.01f64 / (float64_t)WA_HEIGHT;
                    nudged[k] = direction[k] + sideways * ((float64_t)view.right[k] + (float64_t)view.up[k]);
                }
                wa_seen_t other;
                wa_trace(t, &here, camera->position, nudged, &other);
                usable = other.signature == seen.signature;
            }
            if (!usable) {
                continue;
            }
            out->compared++;
            out->hits += seen.hit != 0u ? 1u : 0u;
            out->in_water += seen.water_entered != 0u ? 1u : 0u;
            out->through_glass += seen.glass_passed != 0u ? 1u : 0u;
            out->through_ice += seen.ice_passed != 0u ? 1u : 0u;
            out->sun_in_water += seen.sun_in_water != 0u ? 1u : 0u;
            out->mirrored_hit += seen.mirrored_hit != 0u ? 1u : 0u;
            out->cut_short += seen.cut_short != 0u ? 1u : 0u;
            if (!sk_bytes_near(sk_pixel(picture, WA_WIDTH, px, py), seen.expected, 1u)) {
                if (out->wrong < 8u) {
                    printf("  pixel %" PRIu32 ", %" PRIu32 ": card %06" PRIX32 ", reference %06" PRIX32 "\n", px, py,
                           sk_pixel(picture, WA_WIDTH, px, py), seen.expected);
                }
                out->wrong++;
            }
        }
    }
}

static void wa_report(const char *what, const wa_tally_t *tally) {
    printf("  %s: %" PRIu64 " compared, %" PRIu64 " hits, %" PRIu64 " in water, %" PRIu64 " past glass, %" PRIu64
           " past ice, %" PRIu64 " sun rays through water, %" PRIu64 " reflections on a block, %" PRIu64
           " past what water shows, %" PRIu64 " wrong\n",
           what, tally->compared, tally->hits, tally->in_water, tally->through_glass, tally->through_ice,
           tally->sun_in_water, tally->mirrored_hit, tally->cut_short, tally->wrong);
}

/* ---- The scene ---------------------------------------------------------------------------------------------------------- */

/* A stone basin full of water with a shelf and a pit, a block of bricks on its bed, a tower and a tree's crown over it, a glass wall two
 * thick standing in it and going up into the air, ice on the water, a piece of water hanging in the air, and a thin floor out past
 * the rim. Everything is a whole cube. */
static bool wa_scene_put(test_context_t *t, sk_rig_t *rig, wa_scene_t *scene) {
    if (!wa_scene_begin(t, rig, scene)) {
        return false;
    }
    bool good = true;
    /* The floor at z = -5, the pit in it down to z = -12, and the rim up to z = -1. */
    for (int32_t y = 3; y <= 28; y++) {
        for (int32_t x = 3; x <= 28; x++) {
            const bool rim = x == 3 || x == 28 || y == 3 || y == 28;
            const bool pit = x >= 16 && x <= 19 && y >= 14 && y <= 17;
            const bool shelf = x >= 4 && x <= 9 && y >= 4 && y <= 12;
            const int32_t floor_z = pit ? -12 : -5;
            good = good && wa_put(t, rig, scene, x, y, floor_z, BLOCK_ID_STONE);
            for (int32_t z = floor_z + 1; z <= -1; z++) {
                const uint32_t state = rim || (shelf && z <= -3) ? BLOCK_ID_STONE : BLOCK_ID_WATER;
                good = good && wa_put(t, rig, scene, x, y, z, state);
            }
        }
    }
    /* In and over it. */
    good = good && wa_put(t, rig, scene, 22, 22, -4, BLOCK_ID_BRICKS);
    for (int32_t z = 0; z < 7; z++) {
        good = good && wa_put(t, rig, scene, 21, 9, z, BLOCK_ID_COBBLESTONE);
    }
    for (int32_t x = 7; x <= 9; x++) {
        for (int32_t y = 19; y <= 21; y++) {
            good = good && wa_put(t, rig, scene, x, y, 3, BLOCK_ID_OAK_LEAVES);
        }
    }
    for (int32_t y = 8; y <= 11; y++) {
        for (int32_t z = -1; z <= 3; z++) {
            good = good && wa_put(t, rig, scene, 13, y, z, BLOCK_ID_GLASS);
            good = good && wa_put(t, rig, scene, 14, y, z, BLOCK_ID_GLASS);
        }
    }
    good = good && wa_put(t, rig, scene, 24, 18, 0, BLOCK_ID_ICE);
    good = good && wa_put(t, rig, scene, 24, 19, 0, BLOCK_ID_ICE);
    good = good && wa_put(t, rig, scene, 25, 18, 0, BLOCK_ID_ICE);
    good = good && wa_put(t, rig, scene, 11, 25, 6, BLOCK_ID_WATER);
    good = good && wa_put(t, rig, scene, 12, 25, 6, BLOCK_ID_WATER);
    good = good && wa_put(t, rig, scene, 11, 25, 7, BLOCK_ID_WATER);
    return good && sk_hand_world_sync(t, rig);
}

static void wa_camera_around(render_camera_t *camera, float32_t yaw, float32_t pitch, float64_t reach) {
    render_camera_start(camera, 16.0f64 - reach * (float64_t)cosf(pitch) * (float64_t)cosf(yaw),
                        16.0f64 - reach * (float64_t)cosf(pitch) * (float64_t)sinf(yaw),
                        0.3f64 - reach * (float64_t)sinf(pitch), yaw, pitch);
}

static void wa_compare_around(test_context_t *t, sk_rig_t *rig, const wa_scene_t *scene, float32_t day, uint32_t turns,
                              wa_tally_t *tally) {
    render_frame_t *const frame = &rig->render->frame;
    render_frame_time_set(frame, day);
    const wa_ask_t ask = {.rig = rig, .scene = scene, .features = frame->scene.features};
    const float64_t reaches[] = {11.0f64, 24.0f64};
    const float32_t pitches[] = {-0.6f32, -0.22f32};
    for (uint32_t d = 0u; d < 2u; d++) {
        for (uint32_t turn = 0u; turn < turns; turn++) {
            render_camera_t camera;
            const float32_t yaw = (float32_t)turn * 6.2831853f32 / (float32_t)turns + 0.17f32;
            wa_camera_around(&camera, yaw, pitches[d], reaches[d]);
            const uint8_t *const picture = sk_draw(t, rig, &camera);
            REQUIRE(picture != nullptr);
            wa_compare_picture(t, &ask, &camera, picture, tally);
        }
    }
}

/* ---- On the card ------------------------------------------------------------------------------------------------------- */

TEST(everything_starts_on_and_the_kernel_is_told_which_block_is_water) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 1u, WA_WIDTH, WA_HEIGHT, 1000.0f32));
    render_frame_t *const frame = &rig.render->frame;
    CHECK_EQ_U64(frame->scene.features, SHADE_FEATURES_ALL);
    CHECK_EQ_U64(frame->scene.features & WA_FEATURES_OURS, WA_FEATURES_OURS);
    CHECK_EQ_U64(frame->scene.water_material, BLOCK_ID_WATER);
    CHECK_EQ_U64(frame->scene.glass_material, BLOCK_ID_GLASS);
    render_frame_features_set(frame, 0xFFFFFFFFu);
    CHECK_EQ_U64(frame->scene.features, SHADE_FEATURES_EVERY);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

TEST(every_pixel_of_a_lake_matches_an_exact_trace_all_day) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 2u, WA_WIDTH, WA_HEIGHT, 100000.0f32));
    wa_scene_t *const scene = test_alloc(t, sizeof(wa_scene_t));
    REQUIRE(wa_scene_put(t, &rig, scene));
    const float32_t days[] = {WA_MORNING, WA_NOON, WA_EVENING, WA_DUSK, WA_NIGHT};
    const char *const names[] = {"morning", "noon", "evening", "dusk", "night"};
    wa_tally_t all = {0};
    for (uint32_t d = 0u; d < 5u; d++) {
        wa_tally_t tally = {0};
        wa_compare_around(t, &rig, scene, days[d], 4u, &tally);
        wa_report(names[d], &tally);
        CHECK_EQ_U64(tally.wrong, 0u);
        CHECK(tally.compared > (uint64_t)WA_WIDTH * WA_HEIGHT * 2u);
        all.in_water += tally.in_water;
        all.through_glass += tally.through_glass;
        all.through_ice += tally.through_ice;
        all.sun_in_water += tally.sun_in_water;
        all.mirrored_hit += tally.mirrored_hit;
    }
    /* Or the paths were not looked at. */
    CHECK(all.in_water > 100000u);
    CHECK(all.through_glass > 1000u);
    CHECK(all.through_ice > 100u);
    CHECK(all.sun_in_water > 10000u);
    CHECK(all.mirrored_hit > 1000u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

TEST(from_under_the_water_the_trace_agrees_too) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 2u, WA_WIDTH, WA_HEIGHT, 100000.0f32));
    wa_scene_t *const scene = test_alloc(t, sizeof(wa_scene_t));
    REQUIRE(wa_scene_put(t, &rig, scene));
    render_frame_t *const frame = &rig.render->frame;
    render_frame_time_set(frame, WA_NOON);
    const wa_ask_t ask = {.rig = &rig, .scene = scene, .features = frame->scene.features};
    wa_tally_t tally = {0};
    /* Looking along the bed, up at the surface from the pit, and out through a glass wall that stands in it. */
    const float32_t pitches[] = {-0.1f32, 0.9f32, 0.0f32, 0.5f32};
    const float32_t yaws[] = {0.4f32, 2.2f32, 3.3f32, 5.1f32};
    for (uint32_t i = 0u; i < 4u; i++) {
        render_camera_t camera;
        render_camera_start(&camera, 18.3f64, 15.7f64, -2.4f64, yaws[i], pitches[i]);
        const uint8_t *const picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        wa_compare_picture(t, &ask, &camera, picture, &tally);
    }
    wa_report("under water", &tally);
    CHECK_EQ_U64(tally.wrong, 0u);
    CHECK(tally.in_water > 100000u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

TEST(fog_behind_a_surface_is_exact_and_each_setting_off_is_what_it_says) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 2u, WA_WIDTH, WA_HEIGHT, 30.0f32));
    wa_scene_t *const scene = test_alloc(t, sizeof(wa_scene_t));
    REQUIRE(wa_scene_put(t, &rig, scene));
    const uint32_t settings[] = {
        SHADE_FEATURES_ALL,
        SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_WATER_REFLECTION,
        SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_WATER_TINT,
        SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_SHADOW,
        SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_FOG,
        SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_SKY,
        WA_FEATURES_BEFORE,
    };
    const char *const names[] = {"all",    "no reflection", "no tint",        "no shadows",
                                 "no fog", "no sky",        "no transparency"};
    for (uint32_t s = 0u; s < sizeof(settings) / sizeof(settings[0]); s++) {
        render_frame_features_set(&rig.render->frame, settings[s]);
        wa_tally_t tally = {0};
        wa_compare_around(t, &rig, scene, WA_MORNING, 3u, &tally);
        wa_report(names[s], &tally);
        CHECK_EQ_U64(tally.wrong, 0u);
        CHECK(((settings[s] & SHADE_FEATURE_TRANSPARENCY) != 0u) == (tally.in_water > 0u));
        CHECK(((settings[s] & SHADE_FEATURE_WATER_REFLECTION) != 0u) == (tally.mirrored_hit > 0u));
    }
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* Each spot of a picture on its own, seen straight down through a tiny field of view: the mean of each channel over all of it. */
typedef struct wa_mean_t {
    float64_t channel[3]; /* 0: red, green, blue */
} wa_mean_t;
static_assert(sizeof(wa_mean_t) == 24);

static bool wa_look_down(test_context_t *t, sk_rig_t *rig, float64_t x, float64_t y, float64_t z, wa_mean_t *out) {
    render_camera_t camera;
    render_camera_start(&camera, x, y, z, 0.0f32, -RENDER_CAMERA_PITCH_LIMIT);
    camera.half_fov_tangent = 0.02f32;
    const uint8_t *const picture = sk_draw(t, rig, &camera);
    if (picture == nullptr) {
        return false;
    }
    uint64_t sums[3] = {0u, 0u, 0u};
    for (uint32_t py = 0u; py < WA_HEIGHT; py++) {
        for (uint32_t px = 0u; px < WA_WIDTH; px++) {
            const uint32_t pixel = sk_pixel(picture, WA_WIDTH, px, py);
            sums[0] += (pixel >> 16) & 255u;
            sums[1] += (pixel >> 8) & 255u;
            sums[2] += pixel & 255u;
        }
    }
    for (uint32_t i = 0u; i < 3u; i++) {
        out->channel[i] = (float64_t)sums[i] / (float64_t)(WA_WIDTH * WA_HEIGHT);
    }
    return true;
}

TEST(deeper_water_is_darker_and_bluer_by_hand_and_tint_off_is_not) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 2u, WA_WIDTH, WA_HEIGHT, 100000.0f32));
    wa_scene_t *const scene = test_alloc(t, sizeof(wa_scene_t));
    REQUIRE(wa_scene_begin(t, &rig, scene));
    /* Columns of water on a floor of pale stone, 1, 2, 4, 8 and 16 deep, in a row, the sun overhead. */
    const int32_t depths[] = {1, 2, 4, 8, 11};
    for (uint32_t d = 0u; d < 5u; d++) {
        const int32_t x = 4 + (int32_t)d * 4;
        for (int32_t px = x - 1; px <= x + 1; px++) {
            for (int32_t py = 3; py <= 5; py++) {
                REQUIRE(wa_put(t, &rig, scene, px, py, -depths[d] - 1, BLOCK_ID_CALCITE));
                for (int32_t z = -depths[d]; z < 0; z++) {
                    REQUIRE(wa_put(t, &rig, scene, px, py, z, BLOCK_ID_WATER));
                }
            }
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_t *const frame = &rig.render->frame;
    render_frame_time_set(frame, WA_NOON);
    render_frame_features_set(frame, SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_WATER_REFLECTION);
    wa_mean_t tinted[5], clear[5];
    for (uint32_t d = 0u; d < 5u; d++) {
        REQUIRE(wa_look_down(t, &rig, 4.5f64 + (float64_t)d * 4.0f64, 4.5f64, 6.0f64, &tinted[d]));
    }
    render_frame_features_set(frame, SHADE_FEATURES_ALL &
                                         ~(uint32_t)(SHADE_FEATURE_WATER_REFLECTION | SHADE_FEATURE_WATER_TINT));
    for (uint32_t d = 0u; d < 5u; d++) {
        REQUIRE(wa_look_down(t, &rig, 4.5f64 + (float64_t)d * 4.0f64, 4.5f64, 6.0f64, &clear[d]));
    }
    for (uint32_t d = 0u; d < 5u; d++) {
        printf("  %2" PRId32 " deep: tinted %.1f %.1f %.1f, clear %.1f %.1f %.1f\n", depths[d],
               (libc_double_t)tinted[d].channel[0], (libc_double_t)tinted[d].channel[1],
               (libc_double_t)tinted[d].channel[2], (libc_double_t)clear[d].channel[0],
               (libc_double_t)clear[d].channel[1], (libc_double_t)clear[d].channel[2]);
    }
    for (uint32_t d = 1u; d < 5u; d++) {
        /* Tinted: each is darker than the last, red falling away first. */
        CHECK(tinted[d].channel[0] < tinted[d - 1u].channel[0]);
        CHECK(tinted[d].channel[1] < tinted[d - 1u].channel[1]);
        /* Clear: the bed is the bed, however deep, but for the frame of the sun's own ray, which is tint's too. */
        CHECK_NEAR(clear[d].channel[0], clear[0].channel[0], 1.0f64);
        CHECK_NEAR(clear[d].channel[1], clear[0].channel[1], 1.0f64);
        CHECK_NEAR(clear[d].channel[2], clear[0].channel[2], 1.0f64);
    }
    /* Deep water is blue over green over red, and the tint took more red than blue. */
    CHECK(tinted[4].channel[2] > tinted[4].channel[1] && tinted[4].channel[1] > tinted[4].channel[0]);
    CHECK(tinted[4].channel[0] / clear[4].channel[0] < tinted[4].channel[2] / clear[4].channel[2]);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

TEST(the_sun_shows_in_water_along_the_mirror_way_and_reflections_grow_at_a_low_angle) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 2u, WA_WIDTH, WA_HEIGHT, 100000.0f32));
    wa_scene_t *const scene = test_alloc(t, sizeof(wa_scene_t));
    REQUIRE(wa_scene_begin(t, &rig, scene));
    /* A sea to the edge of the grid, 3 deep. */
    for (int32_t y = 0; y < 32; y++) {
        for (int32_t x = 0; x < 32; x++) {
            REQUIRE(wa_put(t, &rig, scene, x, y, -4, BLOCK_ID_STONE));
            for (int32_t z = -3; z < 0; z++) {
                REQUIRE(wa_put(t, &rig, scene, x, y, z, BLOCK_ID_WATER));
            }
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_t *const frame = &rig.render->frame;
    render_frame_time_set(frame, 0.31f32);
    const float32_t *const sun = frame->post.sun_direction;
    /* Looking down towards the sun's side, as steep as it stands: off the water, that ray goes straight at it. */
    const float32_t look[3] = {sun[0], sun[1], -sun[2]};
    const float64_t from = 6.0f64 / (float64_t)fabsf(look[2]);
    render_camera_t camera;
    render_camera_start(&camera, 16.0f64 - from * (float64_t)look[0], 16.0f64 - from * (float64_t)look[1], 6.0f64,
                        atan2f(look[1], look[0]), asinf(look[2]));
    render_frame_features_set(frame, SHADE_FEATURES_ALL);
    const uint8_t *picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    const uint32_t with = sk_pixel(picture, WA_WIDTH, WA_WIDTH / 2u, WA_HEIGHT / 2u);
    render_frame_features_set(frame, SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_WATER_REFLECTION);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    const uint32_t without = sk_pixel(picture, WA_WIDTH, WA_WIDTH / 2u, WA_HEIGHT / 2u);
    printf("  on the mirror way to the sun: %06" PRIX32 " with its reflection, %06" PRIX32 " without\n", with, without);
    /* Its disc is 20 times full light, and water gives back at least a fiftieth of that. */
    CHECK(((with >> 16) & 255u) > ((without >> 16) & 255u) + 60u);
    /* One pixel to the side of the disc there is only the sky in it, and far less. */
    const float32_t beside_look[3] = {sun[0] + 0.3f32, sun[1] + 0.3f32, -sun[2]};
    render_camera_start(&camera, 16.0f64 - from * (float64_t)beside_look[0], 16.0f64 - from * (float64_t)beside_look[1],
                        6.0f64, atan2f(beside_look[1], beside_look[0]),
                        asinf(beside_look[2] / sqrtf(beside_look[0] * beside_look[0] + beside_look[1] * beside_look[1] +
                                                     beside_look[2] * beside_look[2])));
    render_frame_features_set(frame, SHADE_FEATURES_ALL);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    const uint32_t beside = sk_pixel(picture, WA_WIDTH, WA_WIDTH / 2u, WA_HEIGHT / 2u);
    printf("  off the mirror way: %06" PRIX32 "\n", beside);
    CHECK(((beside >> 16) & 255u) + 60u < ((with >> 16) & 255u));

    /* A reflection adds, and it adds more the lower one looks. */
    render_frame_time_set(frame, WA_NOON);
    const float32_t pitches[] = {-1.3f32, -0.6f32, -0.2f32, -0.08f32};
    float64_t added[4];
    for (uint32_t p = 0u; p < 4u; p++) {
        /* Far enough that the middle of the picture is on the sea, whatever the pitch. */
        render_camera_start(&camera, 8.0f64, 16.0f64, 1.0f64, 0.0f32, pitches[p]);
        render_camera_t narrow = camera;
        narrow.half_fov_tangent = 0.01f32;
        narrow.position[0] = 16.0f64 - 1.0f64 / (float64_t)tanf(-pitches[p]);
        render_frame_features_set(frame, SHADE_FEATURES_ALL);
        picture = sk_draw(t, &rig, &narrow);
        REQUIRE(picture != nullptr);
        const uint32_t on = sk_pixel(picture, WA_WIDTH, WA_WIDTH / 2u, WA_HEIGHT / 2u);
        render_frame_features_set(frame, SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_WATER_REFLECTION);
        picture = sk_draw(t, &rig, &narrow);
        REQUIRE(picture != nullptr);
        const uint32_t off = sk_pixel(picture, WA_WIDTH, WA_WIDTH / 2u, WA_HEIGHT / 2u);
        added[p] = (float64_t)((on >> 8) & 255u) - (float64_t)((off >> 8) & 255u);
        printf("  pitch %.2f: green %" PRIu32 " with it, %" PRIu32 " without\n", (libc_double_t)pitches[p],
               (on >> 8) & 255u, (off >> 8) & 255u);
    }
    CHECK(added[1] > added[0]);
    CHECK(added[2] > added[1]);
    CHECK(added[3] > added[2]);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

TEST(a_tower_shows_upside_down_in_the_water_where_the_mirror_says) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 2u, WA_WIDTH, WA_HEIGHT, 100000.0f32));
    wa_scene_t *const scene = test_alloc(t, sizeof(wa_scene_t));
    REQUIRE(wa_scene_begin(t, &rig, scene));
    for (int32_t y = 0; y < 32; y++) {
        for (int32_t x = 0; x < 32; x++) {
            REQUIRE(wa_put(t, &rig, scene, x, y, -2, BLOCK_ID_STONE));
            REQUIRE(wa_put(t, &rig, scene, x, y, -1, BLOCK_ID_WATER));
        }
    }
    /* A tower with its near face at x = 20, 6 tall, seen from x = 8, one block up: the water surface is at z = 0. */
    for (int32_t z = 0; z < 6; z++) {
        for (int32_t y = 14; y <= 18; y++) {
            REQUIRE(wa_put(t, &rig, scene, 20, y, z, BLOCK_ID_BRICKS));
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_t *const frame = &rig.render->frame;
    render_frame_time_set(frame, WA_NOON);
    render_camera_t camera;
    render_camera_start(&camera, 8.5f64, 16.5f64, 1.0f64, 0.0f32, 0.0f32);
    /* A ray meets the water s on from the eye, which is 1 up, and rises the same way: at the tower, 11.5 on, it is 11.5 / s - 1 up.
     * The tower is 6 tall, so the last of these goes over it. */
    const float32_t surface_x[] = {4.0f32, 2.0f32, 8.0f32, 1.2f32};
    uint32_t with_tower[4], without_tower[4];
    camera.half_fov_tangent = 0.004f32;
    render_frame_features_set(frame, SHADE_FEATURES_ALL);
    for (uint32_t i = 0u; i < 4u; i++) {
        camera.pitch = -atan2f(1.0f32, surface_x[i]);
        const uint8_t *const picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        with_tower[i] = sk_pixel(picture, WA_WIDTH, WA_WIDTH / 2u, WA_HEIGHT / 2u);
    }
    for (int32_t z = 0; z < 6; z++) {
        for (int32_t y = 14; y <= 18; y++) {
            REQUIRE(wa_put(t, &rig, scene, 20, y, z, BLOCK_ID_AIR));
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    for (uint32_t i = 0u; i < 4u; i++) {
        camera.pitch = -atan2f(1.0f32, surface_x[i]);
        const uint8_t *const picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        without_tower[i] = sk_pixel(picture, WA_WIDTH, WA_WIDTH / 2u, WA_HEIGHT / 2u);
        printf("  the water %.1f on, the mirror %.2f up the tower: %06" PRIX32 " with it, %06" PRIX32 " with none\n",
               (libc_double_t)surface_x[i], (libc_double_t)(11.5f32 / surface_x[i] - 1.0f32), with_tower[i],
               without_tower[i]);
    }
    /* Bricks in place of the sky: less blue, by more than rounding. */
    for (uint32_t i = 0u; i < 3u; i++) {
        CHECK((with_tower[i] & 255u) + 2u <= (without_tower[i] & 255u));
    }
    /* Over the top, the same sky, whether or not there is a tower under it. */
    CHECK(sk_bytes_near(with_tower[3], without_tower[3], 1u));
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

TEST(glass_has_no_frames_inside_and_nothing_changes_where_nothing_is_see_through) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 2u, WA_WIDTH, WA_HEIGHT, 100000.0f32));
    wa_scene_t *const scene = test_alloc(t, sizeof(wa_scene_t));
    REQUIRE(wa_scene_begin(t, &rig, scene));
    for (int32_t y = 0; y < 32; y++) {
        for (int32_t x = 0; x < 32; x++) {
            REQUIRE(wa_put(t, &rig, scene, x, y, -1, BLOCK_ID_STONE));
        }
    }
    for (int32_t y = 8; y < 24; y++) {
        for (int32_t z = 0; z < 8; z++) {
            REQUIRE(wa_put(t, &rig, scene, 20, y, z, BLOCK_ID_BRICKS));
        }
    }
    /* Leaves and an arch: holes and shadows but nothing part-covered. */
    for (int32_t y = 12; y < 18; y++) {
        REQUIRE(wa_put(t, &rig, scene, 10, y, 2, BLOCK_ID_OAK_LEAVES));
        REQUIRE(wa_put(t, &rig, scene, 9, y, 3, BLOCK_ID_OAK_PLANKS));
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_t *const frame = &rig.render->frame;
    render_frame_time_set(frame, WA_MORNING);
    const uint64_t bytes = post_xrgb_bytes(WA_WIDTH, WA_HEIGHT);
    uint8_t *const on = test_alloc(t, bytes);
    render_camera_t camera;
    render_camera_start(&camera, 2.5f64, 15.5f64, 4.5f64, 0.0f32, -0.1f32);
    render_frame_features_set(frame, SHADE_FEATURES_ALL);
    const uint8_t *picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    memcpy(on, picture, bytes);
    render_frame_features_set(frame, WA_FEATURES_BEFORE);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    uint64_t changed = 0u;
    for (uint32_t i = 0u; i < WA_WIDTH * WA_HEIGHT; i++) {
        changed += sk_pixel(on, WA_WIDTH, i % WA_WIDTH, i / WA_WIDTH) !=
                           sk_pixel(picture, WA_WIDTH, i % WA_WIDTH, i / WA_WIDTH)
                       ? 1u
                       : 0u;
    }
    printf("  with nothing part-covered, the setting changes %" PRIu64 " pixels\n", changed);
    CHECK_EQ_U64(changed, 0u);

    /* A block of glass 4 thick in front of the bricks, looked at square on and from near, so that every ray stays in the block from
     * its near face to its far one: no frames inside means the same picture as glass 1 thick, and with them, far more of the frame. */
    for (int32_t y = 13; y < 18; y++) {
        for (int32_t z = 3; z < 7; z++) {
            for (int32_t x = 14; x < 18; x++) {
                REQUIRE(wa_put(t, &rig, scene, x, y, z, BLOCK_ID_GLASS));
            }
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_camera_start(&camera, 9.5f64, 15.5f64, 5.0f64, 0.0f32, 0.0f32);
    camera.half_fov_tangent = 0.08f32;
    render_frame_features_set(frame, SHADE_FEATURES_ALL);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    memcpy(on, picture, bytes);
    render_frame_features_set(frame, WA_FEATURES_BEFORE);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    uint64_t differ_with_frames = 0u;
    for (uint32_t i = 0u; i < WA_WIDTH * WA_HEIGHT; i++) {
        differ_with_frames += sk_bytes_near(sk_pixel(on, WA_WIDTH, i % WA_WIDTH, i / WA_WIDTH),
                                            sk_pixel(picture, WA_WIDTH, i % WA_WIDTH, i / WA_WIDTH), 1u)
                                  ? 0u
                                  : 1u;
    }
    for (int32_t y = 13; y < 18; y++) {
        for (int32_t z = 3; z < 7; z++) {
            for (int32_t x = 15; x < 18; x++) {
                REQUIRE(wa_put(t, &rig, scene, x, y, z, BLOCK_ID_AIR));
            }
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_features_set(frame, SHADE_FEATURES_ALL);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    uint64_t differ_thin = 0u;
    for (uint32_t i = 0u; i < WA_WIDTH * WA_HEIGHT; i++) {
        differ_thin += sk_bytes_near(sk_pixel(on, WA_WIDTH, i % WA_WIDTH, i / WA_WIDTH),
                                     sk_pixel(picture, WA_WIDTH, i % WA_WIDTH, i / WA_WIDTH), 1u)
                           ? 0u
                           : 1u;
    }
    printf("  glass 4 thick against 1 thick: %" PRIu64 " pixels differ; with its inside faces drawn: %" PRIu64 "\n",
           differ_thin, differ_with_frames);
    CHECK_EQ_U64(differ_thin, 0u);
    CHECK(differ_with_frames > 5000u);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- Golden pictures -------------------------------------------------------------------------------------------------------- */

static void wa_golden_compare(test_context_t *t, const char *path, const uint8_t *picture) {
    const uint64_t capacity = display_png_bytes_needed(WA_WIDTH, WA_HEIGHT);
    uint8_t *const file = test_alloc(t, capacity);
    if (getenv("MC_GOLDEN_WRITE") != nullptr) {
        uint64_t written = 0u;
        REQUIRE(display_png_encode(picture, WA_WIDTH * 4u, WA_WIDTH, WA_HEIGHT, file, capacity, &written));
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
    REQUIRE(info.width == WA_WIDTH && info.height == WA_HEIGHT);
    uint8_t *const rgba = test_alloc(t, info.pixel_bytes);
    uint8_t *const work = test_alloc(t, info.work_bytes);
    REQUIRE(png_decode(file, length, rgba, info.pixel_bytes, work, info.work_bytes, &info, &error) == PNG_OK);
    uint64_t wrong = 0u, exact = 0u;
    for (uint32_t y = 0u; y < WA_HEIGHT; y++) {
        for (uint32_t x = 0u; x < WA_WIDTH; x++) {
            const uint64_t i = (uint64_t)y * WA_WIDTH + x;
            const uint32_t ours = sk_pixel(picture, WA_WIDTH, x, y);
            const uint32_t golden = post_xrgb(rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u]);
            wrong += sk_bytes_near(ours, golden, WA_GOLDEN_TOLERANCE) ? 0u : 1u;
            exact += ours == golden ? 1u : 0u;
        }
    }
    printf("  %s: %" PRIu64 " of %" PRIu64 " pixels exact, %" PRIu64 " past the tolerance\n", path, exact,
           (uint64_t)WA_WIDTH * WA_HEIGHT, wrong);
    CHECK_EQ_U64(wrong, 0u);
}

TEST(golden_pictures_match) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    REQUIRE(sk_hand_world_open(t, &rig, 2u, WA_WIDTH, WA_HEIGHT, 60.0f32));
    wa_scene_t *const scene = test_alloc(t, sizeof(wa_scene_t));
    REQUIRE(wa_scene_put(t, &rig, scene));
    typedef struct golden_t {
        const char *path;  /* 0 */
        float32_t day;     /* 8 */
        uint32_t features; /* 12 */
    } golden_t;
    static_assert(sizeof(golden_t) == 16);
    const golden_t goldens[] = {
        {"tests/cuda/water/fixtures/lake_noon.png", WA_NOON, SHADE_FEATURES_ALL},
        {"tests/cuda/water/fixtures/lake_morning.png", WA_MORNING, SHADE_FEATURES_ALL},
        {"tests/cuda/water/fixtures/lake_dusk.png", WA_DUSK, SHADE_FEATURES_ALL},
        {"tests/cuda/water/fixtures/lake_night.png", WA_NIGHT, SHADE_FEATURES_ALL},
        {"tests/cuda/water/fixtures/no_reflection.png", WA_NOON,
         SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_WATER_REFLECTION},
        {"tests/cuda/water/fixtures/no_tint.png", WA_NOON, SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_WATER_TINT},
        {"tests/cuda/water/fixtures/no_transparency.png", WA_NOON, WA_FEATURES_BEFORE},
    };
    render_camera_t camera;
    wa_camera_around(&camera, 2.3f32, -0.33f32, 24.0f64);
    for (uint32_t g = 0u; g < sizeof(goldens) / sizeof(goldens[0]); g++) {
        render_frame_features_set(&rig.render->frame, goldens[g].features);
        render_frame_time_set(&rig.render->frame, goldens[g].day);
        const uint8_t *const picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        wa_golden_compare(t, goldens[g].path, picture);
    }
    render_frame_features_set(&rig.render->frame, SHADE_FEATURES_ALL);
    render_frame_time_set(&rig.render->frame, WA_NOON);
    render_camera_start(&camera, 6.5f64, 9.5f64, 2.5f64, 0.1f32, -0.1f32);
    const uint8_t *picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    wa_golden_compare(t, "tests/cuda/water/fixtures/through_glass.png", picture);
    render_camera_start(&camera, 18.3f64, 15.7f64, -2.4f64, 2.2f32, 0.5f32);
    picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    wa_golden_compare(t, "tests/cuda/water/fixtures/under_water.png", picture);
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- What it costs, on the card's own clock -------------------------------------------------------------------------------- */

typedef struct wa_cost_t {
    float64_t shade_ms; /* 0 */
    float64_t total_ms; /* 8 */
} wa_cost_t;
static_assert(sizeof(wa_cost_t) == 16);

static void wa_cost_measure(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera, uint32_t features,
                            wa_cost_t *out) {
    render_frame_t *const frame = &rig->render->frame;
    render_frame_features_set(frame, features);
    REQUIRE(sk_draw(t, rig, camera) != nullptr); /* a first frame pays for what the others find ready */
    const render_frame_stats_t before = frame->stats;
    for (uint32_t i = 0u; i < WA_COST_FRAMES; i++) {
        REQUIRE(sk_draw(t, rig, camera) != nullptr);
    }
    const render_frame_stats_t *const after = &frame->stats;
    const float64_t frames = (float64_t)WA_COST_FRAMES;
    out->shade_ms = (after->sum_ms[FRAME_PASS_SHADE] - before.sum_ms[FRAME_PASS_SHADE]) / frames;
    out->total_ms = (after->sum_total_ms - before.sum_total_ms) / frames;
}

static void wa_cost_row(test_context_t *t, sk_rig_t *rig, const render_camera_t *camera, const char *where) {
    const uint32_t off_reflection = SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_WATER_REFLECTION;
    const uint32_t off_tint = SHADE_FEATURES_ALL & ~(uint32_t)SHADE_FEATURE_WATER_TINT;
    const uint32_t settings[] = {WA_FEATURES_BEFORE, off_reflection & ~(uint32_t)SHADE_FEATURE_WATER_TINT,
                                 off_reflection, off_tint, SHADE_FEATURES_ALL};
    const char *const names[] = {"no transparency", "transparency only", "and tint", "and reflection", "all"};
    for (uint32_t s = 0u; s < 5u; s++) {
        wa_cost_t cost = {0};
        wa_cost_measure(t, rig, camera, settings[s], &cost);
        printf("  cost, %s, %" PRIu32 " x %" PRIu32 ", %-17s: shade %7.3f ms, frame %7.3f ms\n", where,
               rig->render->frame.width, rig->render->frame.height, names[s], (libc_double_t)cost.shade_ms,
               (libc_double_t)cost.total_ms);
        CHECK(cost.total_ms > 0.0f64);
    }
}

/* Real terrain and the worst there is on purpose, every pixel water to the horizon over a sea too deep to see the bottom of, then
 * shallow to its bed. Nothing here is a limit: the numbers go in the stage file. */
TEST(the_cost_is_measured_on_real_terrain_and_over_a_sea) {
    sk_rig_t rig;
    REQUIRE(sk_rig_begin(t, &rig));
    game_t *const game = rig.game;
    const game_config_t config = {.render_distance = WA_COST_RENDER_DISTANCE,
                                  .width = WA_COST_WIDTH,
                                  .height = WA_COST_HEIGHT,
                                  .features_off = 0u,
                                  .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    render_t *const render = rig.render;
    render_camera_t camera = game->camera;
    camera.pitch = -0.35f32;
    render_frame_time_set(&render->frame, (float32_t)GAME_DAY_START);
    wa_cost_row(t, &rig, &camera, "terrain");
    const uint64_t bytes = (uint64_t)WA_COST_WIDTH * WA_COST_HEIGHT * 4u;
    uint8_t *const all = test_alloc(t, bytes);
    render_frame_features_set(&render->frame, SHADE_FEATURES_ALL);
    const uint8_t *picture = sk_draw(t, &rig, &camera);
    REQUIRE(picture != nullptr);
    memcpy(all, picture, bytes);
    const uint32_t bits[] = {SHADE_FEATURE_TRANSPARENCY, SHADE_FEATURE_WATER_REFLECTION, SHADE_FEATURE_WATER_TINT};
    const char *const names[] = {"transparency", "reflection", "tint"};
    for (uint32_t b = 0u; b < 3u; b++) {
        render_frame_features_set(&render->frame, SHADE_FEATURES_ALL & ~bits[b]);
        picture = sk_draw(t, &rig, &camera);
        REQUIRE(picture != nullptr);
        uint64_t changed = 0u;
        for (uint64_t i = 0u; i < (uint64_t)WA_COST_WIDTH * WA_COST_HEIGHT; i++) {
            changed += memcmp(all + i * 4u, picture + i * 4u, 3u) != 0 ? 1u : 0u;
        }
        /* Reported, not held: this spot of real terrain may have no water in view. A lake with all of them on is above. */
        printf("  terrain, %s off against on: %" PRIu64 " pixels of %" PRIu64 " change\n", names[b], changed,
               (uint64_t)WA_COST_WIDTH * WA_COST_HEIGHT);
    }
    game_close(game);

    REQUIRE(sk_hand_world_open(t, &rig, 3u, WA_COST_WIDTH, WA_COST_HEIGHT, 100000.0f32));
    /* 32 deep: a ray down it sees water all the way and then a bed, with what tint leaves it. */
    for (int32_t z = -33; z < 0; z++) {
        for (int32_t y = -32; y < 48; y++) {
            for (int32_t x = -32; x < 48; x++) {
                const world_edit_status_t edited =
                    world_edit_block_set(&rig.world->edit, x, y, z, z == -33 ? BLOCK_ID_STONE : BLOCK_ID_WATER);
                if (edited != WORLD_EDIT_STATUS_CHANGED) {
                    printf("  the sea stopped at %" PRId32 ", %" PRId32 ", %" PRId32 ", with status %" PRIu32 "\n", x,
                           y, z, (uint32_t)edited);
                }
                REQUIRE(edited == WORLD_EDIT_STATUS_CHANGED);
            }
        }
    }
    REQUIRE(sk_hand_world_sync(t, &rig));
    render_frame_time_set(&render->frame, 0.4f32);
    render_camera_start(&camera, -30.5f64, 8.5f64, 6.0f64, 0.0f32, -0.5f32);
    wa_cost_row(t, &rig, &camera, "over a deep sea");
    render_camera_start(&camera, -30.5f64, 8.5f64, 1.5f64, 0.0f32, -0.12f32);
    wa_cost_row(t, &rig, &camera, "low over a deep sea");
    sk_hand_world_close(&rig);
    sk_rig_end(t, &rig);
}

/* ---- The built program ------------------------------------------------------------------------------------------------------ */

typedef struct wa_temp_dir_t {
    char path[64]; /* 0 */
} wa_temp_dir_t;
static_assert(sizeof(wa_temp_dir_t) == 64);

static bool wa_temp_dir_make(wa_temp_dir_t *dir) {
    strcpy(dir->path, "/tmp/mc_c_render_water_XXXXXX");
    return mkdtemp(dir->path) != nullptr;
}

static void wa_temp_dir_remove(const wa_temp_dir_t *dir) {
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

static const char wa_script[] = "0 move 0 120\n"
                                "2 screenshot view.png\n"
                                "3 end\n";

TEST(the_program_takes_the_settings_and_refuses_them_with_no_world) {
    wa_temp_dir_t base;
    REQUIRE(wa_temp_dir_make(&base));
    char script[128];
    snprintf(script, sizeof(script), "%s/view.script", base.path);
    REQUIRE(file_write_atomic(script, wa_script, sizeof(wa_script) - 1u) == FILE_OK);
    const char *const flags[] = {"--no-transparency", "--no-water-reflection", "--no-water-tint"};
    for (uint32_t f = 0u; f < 3u; f++) {
        char *const argv[] = {t->borrowed_args[0],
                              (char *)"--headless",
                              (char *)"--test-world",
                              (char *)"--render-distance",
                              (char *)"4",
                              (char *)"--script",
                              script,
                              (char *)"--screenshot-dir",
                              base.path,
                              (char *)"--width",
                              (char *)"160",
                              (char *)"--height",
                              (char *)"90",
                              (char *)flags[f],
                              nullptr};
        test_process_t run;
        test_process_run(t, argv, WA_PROGRAM_TIMEOUT_MS, &run);
        REQUIRE(run.started != 0u);
        CHECK_EQ_I64(run.exit_code, MAIN_EXIT_OK);
        char *const alone[] = {t->borrowed_args[0], (char *)"--headless", (char *)"--ticks",
                               (char *)"1",         (char *)flags[f],     nullptr};
        test_process_run(t, alone, WA_PROGRAM_TIMEOUT_MS, &run);
        CHECK_EQ_I64(run.exit_code, MAIN_EXIT_USAGE);
        CHECK_CONTAINS(run.output, "only mean something with --test-world");
    }
    wa_temp_dir_remove(&base);
}

TEST_MAIN()
