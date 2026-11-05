/* See README.md. The C side of a frame: the tables the kernels read, the camera, the pictures on their way from the card, and
 * putting one on the screen. */
#include "render/frame/index.h"

#include "assert/index.h"
#include "log/index.h"
#include "render/entities/index.h"
#include "render/lod/index.h"
#include "simd/copy/index.h"
#include "tables/blocks/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The block table and the rays name a voxel's faces alike, and the two are compiled apart. */
static_assert((uint32_t)BLOCK_FACE_WEST == (uint32_t)TRAVERSE_FACE_NEG_X);
static_assert((uint32_t)BLOCK_FACE_EAST == (uint32_t)TRAVERSE_FACE_POS_X);
static_assert((uint32_t)BLOCK_FACE_SOUTH == (uint32_t)TRAVERSE_FACE_NEG_Y);
static_assert((uint32_t)BLOCK_FACE_NORTH == (uint32_t)TRAVERSE_FACE_POS_Y);
static_assert((uint32_t)BLOCK_FACE_BOTTOM == (uint32_t)TRAVERSE_FACE_NEG_Z);
static_assert((uint32_t)BLOCK_FACE_TOP == (uint32_t)TRAVERSE_FACE_POS_Z);
static_assert((uint32_t)BLOCK_NO_TEXTURE == (uint32_t)SHADE_NO_TEXTURE);
static_assert((uint32_t)TEXTURE_BLOB_MAX_LEVELS == (uint32_t)SHADE_LEVELS_MAX);
static_assert((uint32_t)DISPLAY_FLIP_BYTES_PER_PIXEL == (uint32_t)POST_BYTES_PER_PIXEL);

enum : uint32_t {
    RENDER_EXCHANGE_FRESH = 1u << 8,
    RENDER_EXCHANGE_SLOT_MASK = RENDER_EXCHANGE_FRESH - 1u,
    /* The card pins whole pages, so a picture has its pages to itself. */
    RENDER_FRAME_PICTURE_ALIGNMENT = 4096u,
};

#define RENDER_DAY_PI 3.14159265358979f32
/* How far from straight overhead the sun's path leans, towards the south, in radians: 20 degrees. */
#define RENDER_DAY_TILT 0.35f32

/* ---- The exchange ------------------------------------------------------------------------------------------------- */

void render_exchange_start(render_exchange_t *exchange) {
    memset(exchange, 0, sizeof(*exchange));
    exchange->back = 0u;
    atomic_store_explicit(&exchange->exchange, 1u, memory_order_relaxed);
    exchange->front = 2u;
}

uint32_t render_exchange_publish(render_exchange_t *exchange) {
    /* Release gives the reader what was written to the slot, and acquire gives this side the slot back with the reader done. */
    const uint32_t previous =
        atomic_exchange_explicit(&exchange->exchange, exchange->back | RENDER_EXCHANGE_FRESH, memory_order_acq_rel);
    exchange->back = previous & RENDER_EXCHANGE_SLOT_MASK;
    return exchange->back;
}

bool render_exchange_take(render_exchange_t *exchange, uint32_t *out_slot) {
    if ((atomic_load_explicit(&exchange->exchange, memory_order_relaxed) & RENDER_EXCHANGE_FRESH) == 0u) {
        return false;
    }
    const uint32_t previous = atomic_exchange_explicit(&exchange->exchange, exchange->front, memory_order_acq_rel);
    exchange->front = previous & RENDER_EXCHANGE_SLOT_MASK;
    *out_slot = exchange->front;
    return true;
}

bool render_exchange_waiting(const render_exchange_t *exchange) {
    return (atomic_load_explicit(&exchange->exchange, memory_order_relaxed) & RENDER_EXCHANGE_FRESH) != 0u;
}

/* ---- The camera --------------------------------------------------------------------------------------------------- */

void render_camera_start(render_camera_t *camera, float64_t x, float64_t y, float64_t z, float32_t yaw,
                         float32_t pitch) {
    *camera = (render_camera_t){
        .position = {x, y, z},
        .yaw = yaw,
        .pitch = pitch,
        .half_fov_tangent = RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT,
        .speed = RENDER_CAMERA_SPEED_DEFAULT,
    };
}

void render_camera_fly(render_camera_t *camera, const render_fly_input_t *input, float32_t seconds) {
    /* Mouse right turns to the right, which is clockwise from above, and yaw counts the other way. */
    camera->yaw -= (float32_t)input->mouse_dx * RENDER_CAMERA_RADIANS_PER_COUNT;
    camera->pitch -= (float32_t)input->mouse_dy * RENDER_CAMERA_RADIANS_PER_COUNT;
    camera->pitch = camera->pitch > RENDER_CAMERA_PITCH_LIMIT ? RENDER_CAMERA_PITCH_LIMIT : camera->pitch;
    camera->pitch = camera->pitch < -RENDER_CAMERA_PITCH_LIMIT ? -RENDER_CAMERA_PITCH_LIMIT : camera->pitch;
    /* A wheel's sum is bounded by the kernel's own, so this cannot run on for long. */
    for (int64_t i = 0; i < input->wheel && camera->speed < RENDER_CAMERA_SPEED_MAX; i++) {
        camera->speed *= RENDER_CAMERA_SPEED_STEP;
    }
    for (int64_t i = 0; i > input->wheel && camera->speed > RENDER_CAMERA_SPEED_MIN; i--) {
        camera->speed /= RENDER_CAMERA_SPEED_STEP;
    }
    const float64_t distance = (float64_t)camera->speed * (float64_t)seconds *
                               (float64_t)(input->fast != 0u ? RENDER_CAMERA_FAST_FACTOR : 1.0f32);
    const float64_t forward_x = (float64_t)cosf(camera->yaw), forward_y = (float64_t)sinf(camera->yaw);
    camera->position[0] += distance * ((float64_t)input->forward * forward_x + (float64_t)input->right * forward_y);
    camera->position[1] += distance * ((float64_t)input->forward * forward_y - (float64_t)input->right * forward_x);
    camera->position[2] += distance * (float64_t)input->up;
}

void render_camera_to_traverse(const render_camera_t *camera, uint32_t width, uint32_t height, traverse_camera_t *out) {
    const float32_t cos_pitch = cosf(camera->pitch), sin_pitch = sinf(camera->pitch);
    const float32_t cos_yaw = cosf(camera->yaw), sin_yaw = sinf(camera->yaw);
    const float32_t forward[3] = {cos_pitch * cos_yaw, cos_pitch * sin_yaw, sin_pitch};
    /* Right-handed with z up: right is forward x up. */
    const float32_t right[3] = {sin_yaw, -cos_yaw, 0.0f32};
    const float32_t up[3] = {right[1] * forward[2] - right[2] * forward[1],
                             right[2] * forward[0] - right[0] * forward[2],
                             right[0] * forward[1] - right[1] * forward[0]};
    const float32_t across = camera->half_fov_tangent * (float32_t)width / (float32_t)height;
    memset(out, 0, sizeof(*out));
    out->width = width;
    out->height = height;
    for (uint32_t i = 0u; i < 3u; i++) {
        const float64_t whole = floor(camera->position[i]);
        out->eye_cell[i] = (int32_t)whole;
        out->eye_frac[i] = (float32_t)(camera->position[i] - whole);
        out->forward[i] = forward[i];
        out->right[i] = right[i] * across;
        out->up[i] = up[i] * camera->half_fov_tangent;
    }
}

/* ---- The tables --------------------------------------------------------------------------------------------------- */

uint64_t render_frame_texture_table_bytes(void) {
    return (uint64_t)BLOCK_TEXTURE_COUNT * sizeof(shade_texture_t);
}

uint64_t render_frame_face_table_bytes(void) {
    return (uint64_t)BLOCK_COUNT * SHADE_FACES * sizeof(uint16_t);
}

uint64_t render_frame_shape_table_bytes(void) {
    return (uint64_t)BLOCK_COUNT * sizeof(uint8_t);
}

uint64_t render_frame_arena_bytes(uint32_t width, uint32_t height) {
    /* Each allocation can waste its alignment. */
    return (uint64_t)RENDER_EXCHANGE_SLOTS * (post_xrgb_bytes(width, height) + 2ull * RENDER_FRAME_PICTURE_ALIGNMENT) +
           (uint64_t)RENDER_FRAME_SAMPLES * sizeof(float32_t) + render_frame_texture_table_bytes() +
           render_frame_face_table_bytes() + render_frame_shape_table_bytes() + 5u * MC_CPU_CACHE_LINE_BYTES;
}

/* block_defs' faces already index block_texture_names, so that list, found in the blob, is the table the kernel reads. */
static bool frame_texture_table(const texture_t *texture, shade_texture_t *out) {
    for (uint32_t i = 0u; i < BLOCK_TEXTURE_COUNT; i++) {
        uint32_t id = 0u;
        texture_info_t info;
        if (!texture_find(texture, block_texture_names[i], &id) || !texture_info_get(texture, id, &info)) {
            LOG_ERROR("render: the block table names the texture %s, and the blob has none", block_texture_names[i]);
            return false;
        }
        const texture_blob_entry_t *const entry = info.borrowed_entry;
        shade_texture_t row = {
            .pixels_offset = entry->pixels_offset,
            .width = (uint16_t)entry->width,
            .height = (uint16_t)entry->height,
            .level_count = entry->level_count,
        };
        memcpy(row.level_offset, entry->level_offset, sizeof(row.level_offset));
        memcpy(row.mean_rgba, entry->mean_rgba, sizeof(row.mean_rgba));
        out[i] = row;
    }
    return true;
}

static void frame_face_table(uint16_t *out) {
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        for (uint32_t face = 0u; face < SHADE_FACES; face++) {
            out[block * SHADE_FACES + face] = block_defs[block].faces[face];
        }
    }
}

/* What each kind of block draws as. Water and lava are whole cubes: cuda/src/shade/README.md has what a ray does in water. `thin` is two shapes, as
 * config/blocks says one word for a lily pad on the floor and a ladder on a wall. No block has a facing yet, so stairs, ladders
 * and vines all have the one: Stage 09.1 issue 1. */
static uint8_t frame_shape_of(uint32_t block) {
    switch ((block_shape_t)block_defs[block].shape) {
    case BLOCK_SHAPE_SLAB:
        return TRAVERSE_SHAPE_SLAB;
    case BLOCK_SHAPE_STAIRS:
        return TRAVERSE_SHAPE_STAIRS;
    case BLOCK_SHAPE_CROSS:
        return TRAVERSE_SHAPE_CROSS;
    case BLOCK_SHAPE_CROP:
        return TRAVERSE_SHAPE_CROP;
    case BLOCK_SHAPE_TORCH:
        return TRAVERSE_SHAPE_TORCH;
    case BLOCK_SHAPE_THIN:
        return block == BLOCK_ID_LILY_PAD ? TRAVERSE_SHAPE_FLAT : TRAVERSE_SHAPE_PLATE;
    case BLOCK_SHAPE_NONE:
    case BLOCK_SHAPE_CUBE:
    case BLOCK_SHAPE_FLUID:
        break;
    }
    return TRAVERSE_SHAPE_CUBE;
}

static gpu_status_t frame_send_tables(render_frame_t *frame, device_memory_t *memory, arena_t *arena,
                                      const texture_t *texture, const render_frame_blocks_t *blocks) {
    shade_texture_t *const textures = arena_alloc(arena, render_frame_texture_table_bytes(), alignof(shade_texture_t));
    uint16_t *const faces = arena_alloc(arena, render_frame_face_table_bytes(), alignof(uint16_t));
    uint8_t *const shapes = arena_alloc(arena, render_frame_shape_table_bytes(), alignof(uint8_t));
    if (!frame_texture_table(texture, textures)) {
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    frame_face_table(faces);
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        shapes[block] = frame_shape_of(block);
    }
    const uint8_t *pixels = nullptr;
    uint64_t pixel_bytes = 0u;
    texture_pixels_get(texture, &pixels, &pixel_bytes);
    gpu_status_t status =
        device_memory_write(memory, blocks->borrowed_textures, 0u, textures, render_frame_texture_table_bytes());
    if (status == GPU_STATUS_OK) {
        status = device_memory_write(memory, blocks->borrowed_faces, 0u, faces, render_frame_face_table_bytes());
    }
    if (status == GPU_STATUS_OK) {
        status = device_memory_write(memory, blocks->borrowed_shapes, 0u, shapes, render_frame_shape_table_bytes());
    }
    if (status == GPU_STATUS_OK) {
        status = device_memory_write(memory, blocks->borrowed_pixels, 0u, pixels, pixel_bytes);
    }
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
        return status;
    }
    frame->scene = (shade_scene_t){
        .borrowed_textures = blocks->borrowed_textures,
        .borrowed_faces = blocks->borrowed_faces,
        .borrowed_pixels = blocks->borrowed_pixels,
        .texture_count = BLOCK_TEXTURE_COUNT,
        .material_count = BLOCK_COUNT,
        .features = SHADE_FEATURES_ALL,
        .water_material = BLOCK_ID_WATER,
        .glass_material = BLOCK_ID_GLASS,
        .borrowed_temporal = &frame->temporal,
    };
    frame->borrowed_shapes = blocks->borrowed_shapes;
    render_frame_time_set(frame, RENDER_DAY_NOON);
    return GPU_STATUS_OK;
}

gpu_status_t render_frame_init(render_frame_t *frame, arena_t *arena, device_memory_t *memory, const texture_t *texture,
                               const render_frame_blocks_t *blocks, uint32_t width, uint32_t height,
                               float32_t fog_end) {
    memset(frame, 0, sizeof(*frame));
    render_exchange_start(&frame->pictures);
    frame->borrowed_memory = memory;
    frame->width = width;
    frame->height = height;
    frame->picture_bytes = post_xrgb_bytes(width, height);
    frame->fog_end = fog_end;
    frame->targets = (frame_targets_t){
        .borrowed_coarse = blocks->borrowed_coarse,
        .borrowed_hits = blocks->borrowed_hits,
        .borrowed_pixels = blocks->borrowed_shaded,
        .borrowed_xrgb = blocks->borrowed_xrgb,
    };
    frame->temporal = (post_temporal_t){
        .borrowed_ao_last = blocks->borrowed_ao[0],
        .borrowed_ao_next = blocks->borrowed_ao[1],
        .borrowed_history_last = blocks->borrowed_history[0],
        .borrowed_history_next = blocks->borrowed_history[1],
        .borrowed_shafts = blocks->borrowed_shafts,
    };
    gpu_status_t status = frame_gpu_init(&frame->gpu);
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&frame->gpu.last_error);
        return status;
    }
    status = frame_send_tables(frame, memory, arena, texture, blocks);
    if (status != GPU_STATUS_OK) {
        render_frame_deinit(frame);
        return status;
    }
    frame->borrowed_samples = arena_alloc(arena, RENDER_FRAME_SAMPLES * sizeof(float32_t), alignof(float32_t));
    for (uint32_t i = 0u; i < RENDER_EXCHANGE_SLOTS; i++) {
        const uint64_t whole_pages = (frame->picture_bytes + RENDER_FRAME_PICTURE_ALIGNMENT - 1u) /
                                     RENDER_FRAME_PICTURE_ALIGNMENT * RENDER_FRAME_PICTURE_ALIGNMENT;
        frame->borrowed_pictures[i] = arena_alloc(arena, whole_pages, RENDER_FRAME_PICTURE_ALIGNMENT);
        status = frame_host_pin(&frame->gpu, frame->borrowed_pictures[i], frame->picture_bytes);
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&frame->gpu.last_error);
            render_frame_deinit(frame);
            return status;
        }
        frame->pinned++;
    }
    return GPU_STATUS_OK;
}

void render_frame_deinit(render_frame_t *frame) {
    for (uint32_t i = 0u; i < frame->pinned; i++) {
        if (frame_host_unpin(&frame->gpu, frame->borrowed_pictures[i]) != GPU_STATUS_OK) {
            gpu_error_log(&frame->gpu.last_error);
        }
    }
    if (frame_gpu_deinit(&frame->gpu) != GPU_STATUS_OK) {
        gpu_error_log(&frame->gpu.last_error);
    }
    memset(frame, 0, sizeof(*frame));
}

/* ---- A frame ------------------------------------------------------------------------------------------------------ */

static float32_t frame_smooth(float32_t from, float32_t to, float32_t value) {
    float32_t x = (value - from) / (to - from);
    x = x < 0.0f32 ? 0.0f32 : (x > 1.0f32 ? 1.0f32 : x);
    return x * x * (3.0f32 - 2.0f32 * x);
}

/* Three colours by how much of each of day, twilight and night it is. */
static void frame_day_mix(const float32_t day[3], const float32_t twilight[3], const float32_t night[3],
                          const float32_t parts[3], float32_t scale, float32_t out[3]) {
    for (uint32_t i = 0u; i < 3u; i++) {
        out[i] = (day[i] * parts[0] + twilight[i] * parts[1] + night[i] * parts[2]) * scale;
    }
}

void render_day_fill(float32_t day_fraction, post_settings_t *post, shade_sun_t *sun) {
    /* The sun goes round once a day, from the east over the south side, tilted so that it is never quite overhead: at noon a wall
     * still throws a shadow, and a block's top and sides do not all look the same. */
    const float32_t wrapped = day_fraction - floorf(day_fraction);
    const float32_t angle = 2.0f32 * RENDER_DAY_PI * (wrapped - 0.25f32);
    const float32_t height = sinf(angle);
    const float32_t sun_direction[3] = {cosf(angle), -height * sinf(RENDER_DAY_TILT), height * cosf(RENDER_DAY_TILT)};
    /* Night and day never overlap, and whatever they leave is twilight. */
    const float32_t day = frame_smooth(0.0f32, 0.35f32, height);
    const float32_t night = frame_smooth(0.0f32, 0.25f32, -height);
    const float32_t parts[3] = {day, 1.0f32 - day - night, night};

    static const float32_t zenith_day[3] = {0.16f32, 0.32f32, 0.70f32}, horizon_day[3] = {0.62f32, 0.75f32, 0.92f32};
    static const float32_t zenith_twilight[3] = {0.14f32, 0.18f32, 0.42f32},
                           horizon_twilight[3] = {0.85f32, 0.45f32, 0.25f32};
    static const float32_t zenith_night[3] = {0.004f32, 0.006f32, 0.018f32},
                           horizon_night[3] = {0.012f32, 0.016f32, 0.04f32};
    frame_day_mix(zenith_day, zenith_twilight, zenith_night, parts, 1.0f32, post->sky_zenith);
    frame_day_mix(horizon_day, horizon_twilight, horizon_night, parts, 1.0f32, post->sky_horizon);
    static const float32_t glow_day[3] = {0.25f32, 0.22f32, 0.16f32}, glow_twilight[3] = {0.60f32, 0.30f32, 0.15f32};
    static const float32_t none[3] = {0.0f32, 0.0f32, 0.0f32};
    frame_day_mix(glow_day, glow_twilight, none, parts, 1.0f32, post->sun_glow);
    memcpy(post->sun_direction, sun_direction, sizeof(sun_direction));
    static const float32_t disc_sun[3] = {20.0f32, 18.0f32, 14.0f32}, disc_moon[3] = {0.9f32, 0.9f32, 1.0f32};
    memcpy(post->sun_disc, disc_sun, sizeof(disc_sun));
    memcpy(post->moon_disc, disc_moon, sizeof(disc_moon));

    /* Noon's sky and sun add up to about 1 on the top of a block, so that the pack's colours are what shows. */
    static const float32_t direct_day[3] = {0.43f32, 0.417f32, 0.387f32},
                           direct_twilight[3] = {0.45f32, 0.25f32, 0.13f32},
                           direct_night[3] = {0.05f32, 0.06f32, 0.10f32};
    static const float32_t ambient_day[3] = {0.60f32, 0.62f32, 0.65f32},
                           ambient_twilight[3] = {0.32f32, 0.28f32, 0.34f32},
                           ambient_night[3] = {0.03f32, 0.04f32, 0.07f32};
    /* The light swaps from sun to moon at the horizon, so it goes to nothing there and no shadow jumps. */
    const bool sun_up = height >= 0.0f32;
    const float32_t fade = frame_smooth(0.0f32, 0.12f32, fabsf(height));
    const float32_t direct_parts[3] = {parts[0], sun_up ? parts[1] : 0.0f32, parts[2]};
    frame_day_mix(direct_day, direct_twilight, direct_night, direct_parts, fade, sun->direct);
    frame_day_mix(ambient_day, ambient_twilight, ambient_night, parts, 1.0f32, sun->ambient);
    for (uint32_t i = 0u; i < 3u; i++) {
        sun->direction[i] = sun_up ? sun_direction[i] : -sun_direction[i];
    }
    sun->block[0] = 1.0f32;
    sun->block[1] = 0.82f32;
    sun->block[2] = 0.58f32;
}

/* The sun straight up and a sky that never changes: each kind of face has its one brightness. */
static void frame_day_plain(post_settings_t *post, shade_sun_t *sun) {
    *sun = (shade_sun_t){.direction = {0.0f32, 0.0f32, 1.0f32},
                         .ambient = {1.0f32, 1.0f32, 1.0f32},
                         .block = {1.0f32, 0.82f32, 0.58f32}};
    const float32_t fog_start = post->fog_start, fog_end = post->fog_end;
    *post = (post_settings_t){.sky_zenith = {0.16f32, 0.32f32, 0.70f32},
                              .sky_horizon = {0.62f32, 0.75f32, 0.92f32},
                              .fog_start = fog_start,
                              .fog_end = fog_end,
                              .sun_direction = {0.0f32, 0.0f32, 1.0f32}};
}

/* What eyes under water see: a colour that follows the light of the day, and is still there at midnight. By how much light the sky gives, and
 * not by how deep the eyes are, as post has only this one colour for all of a picture. */
static void frame_water_apply(render_frame_t *frame) {
    post_settings_t *const post = &frame->post;
    static const float32_t tint[3] = {POST_WATER_TINT_R, POST_WATER_TINT_G, POST_WATER_TINT_B};
    static const float32_t colour[3] = {POST_WATER_COLOUR_R, POST_WATER_COLOUR_G, POST_WATER_COLOUR_B};
    /* The sky's light in green, against what noon has: render_day_fill's ambient_day. */
    const float32_t light = frame->scene.sun.ambient[1] >= 0.62f32 ? 1.0f32 : frame->scene.sun.ambient[1] / 0.62f32;
    const float32_t share = POST_WATER_COLOUR_NIGHT + (1.0f32 - POST_WATER_COLOUR_NIGHT) * light;
    for (uint32_t i = 0u; i < 3u; i++) {
        post->water_tint[i] = tint[i];
        post->water_colour[i] = colour[i] * share;
    }
    post->water_fog_end = POST_WATER_FOG_END;
    post->under_water =
        frame->eyes_under_water != 0u && (frame->scene.features & SHADE_FEATURE_UNDER_WATER) != 0u ? 1u : 0u;
}

/* Everything the kernels read that follows from the settings and the time. */
static void frame_settings_apply(render_frame_t *frame) {
    const bool fog = (frame->scene.features & SHADE_FEATURE_FOG) != 0u;
    /* Off, nothing is fog but what hit nothing at all: TRAVERSE_COARSE_SKY is at this end, and every hit is before this start. */
    frame->post.fog_start = fog ? frame->fog_end * POST_FOG_START_SHARE : TRAVERSE_COARSE_SKY * 0.5f32;
    frame->post.fog_end = fog ? frame->fog_end : TRAVERSE_COARSE_SKY;
    if ((frame->scene.features & SHADE_FEATURE_SKY) != 0u) {
        render_day_fill(frame->day_fraction, &frame->post, &frame->scene.sun);
    } else {
        frame_day_plain(&frame->post, &frame->scene.sun);
    }
    frame_water_apply(frame);
}

void render_frame_features_set(render_frame_t *frame, uint32_t features) {
    frame->scene.features = features & SHADE_FEATURES_EVERY;
    frame->temporal.sun_apart = (features & SHADE_FEATURE_SUN_APART) != 0u ? 1u : 0u;
    frame_settings_apply(frame);
}

void render_frame_fog_set(render_frame_t *frame, float32_t fog_end) {
    frame->fog_end = fog_end;
    frame_settings_apply(frame);
}

void render_frame_time_set(render_frame_t *frame, float32_t day_fraction) {
    frame->day_fraction = day_fraction - floorf(day_fraction);
    frame_settings_apply(frame);
}

void render_frame_under_water_set(render_frame_t *frame, bool under_water) {
    frame->eyes_under_water = under_water ? 1u : 0u;
    frame_water_apply(frame);
}

gpu_status_t render_frame_draw(render_frame_t *frame, const world_buffers_t *buffers, const render_camera_t *camera) {
    traverse_camera_t view;
    render_camera_to_traverse(camera, frame->width, frame->height, &view);
    /* What the history is reprojected with has no jitter: otherwise a still camera would see its own picture move. */
    const traverse_camera_t steady = view;
    const uint32_t features = frame->scene.features;
    post_temporal_t *const temporal = &frame->temporal;
    const bool drawn_before = frame->stats.frames != 0u;
    temporal->ao_valid = drawn_before && (frame->drawn_features & SHADE_FEATURE_AO) != 0u ? 1u : 0u;
    temporal->history_valid = drawn_before && (frame->drawn_features & SHADE_FEATURE_TAA) != 0u ? 1u : 0u;
    temporal->jitter[0] = 0.0f32;
    temporal->jitter[1] = 0.0f32;
    if ((features & SHADE_FEATURE_TAA) != 0u) {
        post_jitter(temporal->frame, temporal->jitter);
        post_camera_jitter(&view, temporal->jitter);
    }
    for (uint32_t i = 0u; i < 3u; i++) {
        temporal->shaft_colour[i] = frame->scene.sun.direct[i] * SHADE_SHAFT_STRENGTH;
    }
    const traverse_world_t world = {
        .borrowed_table = buffers->borrowed_table,
        .borrowed_pool = buffers->borrowed_pool,
        .window = buffers->window,
        .borrowed_shapes = (frame->scene.features & SHADE_FEATURE_SHAPES) != 0u ? frame->borrowed_shapes : nullptr,
        .shape_count = BLOCK_COUNT,
        .top_limit = buffers->top_limit,
    };
    entity_rays_scene_t entities;
    if (frame->borrowed_entities != nullptr) {
        /* Past the fog a box is sky, and with no fog nothing is past anything. */
        const float32_t far = (features & SHADE_FEATURE_FOG) != 0u ? frame->fog_end : 0.0f32;
        const gpu_status_t sent = render_entities_frame(frame->borrowed_entities, &steady, features, far, &entities);
        if (sent != GPU_STATUS_OK) {
            frame->gpu.last_error = frame->borrowed_memory->last_error;
            return sent;
        }
    }
    lod_scene_t far_scene;
    const bool far = frame->borrowed_lod != nullptr && (features & SHADE_FEATURE_LOD) != 0u;
    if (far) {
        render_lod_scene(frame->borrowed_lod, &far_scene);
    }
    const shade_sun_t *const sun = &frame->scene.sun;
    /* With no sun to speak of, nothing is lost by no rays towards it. */
    const bool sun_shines = sun->direct[0] + sun->direct[1] + sun->direct[2] > 0.0f32;
    const lod_light_t far_light = {
        .borrowed_textures = frame->scene.borrowed_textures,
        .borrowed_faces = frame->scene.borrowed_faces,
        .sun_direction = {sun->direction[0], sun->direction[1], sun->direction[2]},
        .material_count = frame->scene.material_count,
        .shadows = (features & SHADE_FEATURE_SHADOW) != 0u && sun_shines ? 1u : 0u,
    };
    frame->scene.borrowed_lod_hits = far ? far_scene.borrowed_hits : nullptr;
    const frame_args_t args = {
        .borrowed_lod = far ? &far_scene : nullptr,
        .borrowed_lod_light = far ? &far_light : nullptr,
        .borrowed_world = &world,
        .borrowed_scene = &frame->scene,
        .borrowed_post = &frame->post,
        .borrowed_camera = &view,
        .borrowed_targets = &frame->targets,
        .borrowed_host_xrgb = frame->borrowed_pictures[frame->pictures.back],
        .borrowed_entities = frame->borrowed_entities != nullptr ? &entities : nullptr,
    };
    frame_timings_t timings;
    const gpu_status_t status = frame_gpu_run(&frame->gpu, &args, &timings);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    /* What this frame wrote is what the next one reads. */
    temporal->previous = steady;
    temporal->frame++;
    frame->drawn_features = features;
    if ((features & SHADE_FEATURE_AO) != 0u) {
        const device_block_t *const written = temporal->borrowed_ao_next;
        temporal->borrowed_ao_next = temporal->borrowed_ao_last;
        temporal->borrowed_ao_last = written;
    }
    if ((features & SHADE_FEATURE_TAA) != 0u) {
        const device_block_t *const written = temporal->borrowed_history_next;
        temporal->borrowed_history_next = temporal->borrowed_history_last;
        temporal->borrowed_history_last = written;
    }
    frame->last_timings = timings;
    render_frame_stats_t *const stats = &frame->stats;
    for (uint32_t i = 0u; i < FRAME_PASS_COUNT; i++) {
        stats->sum_ms[i] += (float64_t)timings.pass_ms[i];
        stats->max_ms[i] = timings.pass_ms[i] > stats->max_ms[i] ? timings.pass_ms[i] : stats->max_ms[i];
    }
    stats->sum_total_ms += (float64_t)timings.total_ms;
    stats->max_total_ms = timings.total_ms > stats->max_total_ms ? timings.total_ms : stats->max_total_ms;
    frame->borrowed_samples[stats->frames % RENDER_FRAME_SAMPLES] = timings.total_ms;
    stats->frames++;
    stats->readback_bytes += frame->picture_bytes;
    render_exchange_publish(&frame->pictures);
    return GPU_STATUS_OK;
}

const uint8_t *render_frame_take(render_frame_t *frame) {
    uint32_t slot = 0u;
    return render_exchange_take(&frame->pictures, &slot) ? frame->borrowed_pictures[slot] : nullptr;
}

void render_frame_present(const render_frame_t *frame, const uint8_t *picture, display_buffer_t *target) {
    ASSERT_ALWAYS(target->width == frame->width && target->height == frame->height);
    const uint64_t row_bytes = (uint64_t)frame->width * POST_BYTES_PER_PIXEL;
    copy_stream_rows_batch(target->owned_map, target->pitch, picture, row_bytes, row_bytes, frame->height);
}

void render_frame_present_full(const uint8_t *picture, display_buffer_t *target) {
    const uint64_t row_bytes = (uint64_t)target->width * POST_BYTES_PER_PIXEL;
    copy_stream_rows_batch(target->owned_map, target->pitch, picture, row_bytes, row_bytes, target->height);
}

static int32_t frame_compare_samples(const void *a, const void *b) {
    const float32_t left = *(const float32_t *)a, right = *(const float32_t *)b;
    return left < right ? -1 : (left > right ? 1 : 0);
}

void render_frame_percentiles(render_frame_t *frame, float32_t *out_p50, float32_t *out_p99) {
    const uint64_t count = frame->stats.frames < RENDER_FRAME_SAMPLES ? frame->stats.frames : RENDER_FRAME_SAMPLES;
    *out_p50 = 0.0f32;
    *out_p99 = 0.0f32;
    if (count == 0u) {
        return;
    }
    qsort(frame->borrowed_samples, count, sizeof(float32_t), frame_compare_samples);
    *out_p50 = frame->borrowed_samples[count / 2u];
    *out_p99 = frame->borrowed_samples[count * 99u / 100u];
}

void render_frame_report(render_frame_t *frame) {
    const render_frame_stats_t *const stats = &frame->stats;
    if (stats->frames == 0u) {
        LOG_INFO("frame: none drawn");
        return;
    }
    static const char *const names[FRAME_PASS_COUNT] = {"coarse", "primary", "shade", "post", "readback"};
    const float64_t frames = (float64_t)stats->frames;
    for (uint32_t i = 0u; i < FRAME_PASS_COUNT; i++) {
        LOG_INFO("frame: %-8s mean %7.3f ms, max %7.3f ms", names[i], (libc_double_t)(stats->sum_ms[i] / frames),
                 (libc_double_t)stats->max_ms[i]);
    }
    float32_t p50 = 0.0f32, p99 = 0.0f32;
    render_frame_percentiles(frame, &p50, &p99);
    LOG_INFO("frame: %" PRIu64 " frames of %" PRIu32 "x%" PRIu32
             ", total mean %.3f ms, p50 %.3f ms, p99 %.3f ms, max %.3f ms",
             stats->frames, frame->width, frame->height, (libc_double_t)(stats->sum_total_ms / frames),
             (libc_double_t)p50, (libc_double_t)p99, (libc_double_t)stats->max_total_ms);
}
