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
    render_camera_to_traverse_drawn(camera, width, height, width, height, out);
}

void render_camera_to_traverse_drawn(const render_camera_t *camera, uint32_t width, uint32_t height,
                                     uint32_t inner_width, uint32_t inner_height, traverse_camera_t *out) {
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
    out->width = inner_width;
    out->height = inner_height;
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

/* The faces, and after them the far field's stops: lod_light_t.stops_after_faces. */
uint64_t render_frame_face_table_bytes(void) {
    return lod_stops_offset((uint64_t)BLOCK_COUNT * SHADE_FACES * sizeof(uint16_t)) + sizeof(traverse_stops_t);
}

uint64_t render_frame_shape_table_bytes(void) {
    return (uint64_t)BLOCK_COUNT * sizeof(uint8_t);
}

/* Whole pages, as what the card pins has its pages to itself. */
static uint64_t frame_whole_pages(uint64_t bytes) {
    return (bytes + RENDER_FRAME_PICTURE_ALIGNMENT - 1u) / RENDER_FRAME_PICTURE_ALIGNMENT *
           RENDER_FRAME_PICTURE_ALIGNMENT;
}

uint64_t render_frame_arena_bytes(uint32_t width, uint32_t height, uint64_t staging_bytes) {
    /* Each allocation can waste its alignment. */
    return (uint64_t)RENDER_EXCHANGE_SLOTS * (post_xrgb_bytes(width, height) + 2ull * RENDER_FRAME_PICTURE_ALIGNMENT) +
           (uint64_t)RENDER_FRAME_SAMPLES * sizeof(float32_t) + render_frame_texture_table_bytes() +
           render_frame_face_table_bytes() + render_frame_shape_table_bytes() + 5u * MC_CPU_CACHE_LINE_BYTES +
           frame_flight_bytes() + RENDER_FRAME_PICTURE_ALIGNMENT + 2u * frame_whole_pages(staging_bytes) +
           RENDER_FRAME_PICTURE_ALIGNMENT + sizeof(device_block_t) + MC_CPU_CACHE_LINE_BYTES;
}

/* Plan 008 phase 04: the world-space AO cache, a device block of the frames' pool whose handle is in the frame arena, as render_frame_t and
 * render_t have no room for one. The frame owns it through temporal.borrowed_ao_cache, and render_frame_deinit gives it back. Without room on the
 * card the frame goes on with none: a newly seen pixel then takes the corner estimate alone. */
static void frame_ao_cache_take(render_frame_t *frame, arena_t *arena, device_memory_t *memory) {
    device_block_t *const block = arena_alloc(arena, sizeof(device_block_t), MC_CPU_CACHE_LINE_BYTES);
    memset(block, 0, sizeof(*block));
    if (device_memory_alloc(memory, DEVICE_POOL_FRAMES, shade_ao_cache_bytes(), "AO cache", block) != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
        LOG_WARN("render: no room on the card for the AO cache, so a pixel newly seen takes its corner estimate alone");
        return;
    }
    if (device_memory_zero(memory, block, 0u, shade_ao_cache_bytes()) != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
        (void)device_memory_free(memory, block);
        return;
    }
    frame->temporal.borrowed_ao_cache = block;
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
 * config/blocks says one word for a lily pad on the floor and a ladder on a wall. Each shape is as it faces north, and a voxel's
 * facing turns it on the card: run/include/types/block_state/. */
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
        /* The census's snow layer lies on the floor too, a little thicker (plan 004 phase 08). */
        return block == BLOCK_ID_LILY_PAD     ? TRAVERSE_SHAPE_FLAT
               : block == BLOCK_ID_SNOW_LAYER ? TRAVERSE_SHAPE_LAYER
                                              : TRAVERSE_SHAPE_PLATE;
    case BLOCK_SHAPE_TORCH_WALL:
        return TRAVERSE_SHAPE_TORCH_WALL;
    case BLOCK_SHAPE_DOOR:
        return TRAVERSE_SHAPE_DOOR;
    case BLOCK_SHAPE_NONE:
    case BLOCK_SHAPE_CUBE:
    case BLOCK_SHAPE_FLUID:
        break;
    }
    return TRAVERSE_SHAPE_CUBE;
}

/* What stops the far field's first trace behind a see-through surface: the blocks no ray of shade goes on through, as no face has
 * anything to see through, or a face has no texture and shade draws it as missing; and water, which shade walks by a rule of its own
 * that the far field then follows. */
static void frame_lod_stops(const shade_texture_t *textures, const uint16_t *faces, traverse_stops_t *out) {
    static_assert(BLOCK_COUNT <= TRAVERSE_STOPS_MATERIALS);
    memset(out, 0, sizeof(*out));
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        bool stops = true;
        for (uint32_t face = 0u; face < SHADE_FACES; face++) {
            const uint16_t texture = faces[block * SHADE_FACES + face];
            stops = stops && (texture == SHADE_NO_TEXTURE || textures[texture].mean_rgba[3] == SHADE_ALPHA_OPAQUE);
        }
        stops = stops || block == BLOCK_ID_WATER;
        out->words[block / 32u] |= stops ? 1u << (block % 32u) : 0u;
    }
}

static gpu_status_t frame_send_tables(render_frame_t *frame, device_memory_t *memory, arena_t *arena,
                                      const texture_t *texture, const render_frame_blocks_t *blocks) {
    shade_texture_t *const textures = arena_alloc(arena, render_frame_texture_table_bytes(), alignof(shade_texture_t));
    uint16_t *const faces = arena_alloc(arena, render_frame_face_table_bytes(), alignof(traverse_stops_t));
    uint8_t *const shapes = arena_alloc(arena, render_frame_shape_table_bytes(), alignof(uint8_t));
    if (!frame_texture_table(texture, textures)) {
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    memset(faces, 0, render_frame_face_table_bytes());
    frame_face_table(faces);
    for (uint32_t block = 0u; block < BLOCK_COUNT; block++) {
        shapes[block] = frame_shape_of(block);
    }
    const uint64_t stops_at = lod_stops_offset((uint64_t)BLOCK_COUNT * SHADE_FACES * sizeof(uint16_t));
    frame_lod_stops(textures, faces, (traverse_stops_t *)(void *)((uint8_t *)faces + stops_at));
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
                               const render_frame_blocks_t *blocks, uint32_t width, uint32_t height, float32_t fog_end,
                               uint64_t staging_bytes) {
    memset(frame, 0, sizeof(*frame));
    render_exchange_start(&frame->pictures);
    frame->borrowed_memory = memory;
    frame->width = width;
    frame->height = height;
    frame->borrowed_xrgb_second = blocks->borrowed_xrgb_second;
    frame->fog_end = fog_end;
    frame->scale_percent = (uint8_t)POST_SCALE_WHOLE;
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
    /* Plan 008 phase 03: the frames on their way, and the areas their copies to the card go through, all pinned by frame_gpu_init. */
    void *const flight = arena_alloc(arena, frame_flight_bytes(), FRAME_FLIGHT_ALIGNMENT);
    const uint64_t area_bytes = frame_whole_pages(staging_bytes);
    uint8_t *const staging =
        area_bytes != 0u ? arena_alloc(arena, 2u * area_bytes, RENDER_FRAME_PICTURE_ALIGNMENT) : nullptr;
    gpu_status_t status = frame_gpu_init(&frame->gpu, flight, staging, area_bytes);
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
    frame_ao_cache_take(frame, arena, memory);
    const uint64_t picture_bytes = post_xrgb_bytes(width, height);
    for (uint32_t i = 0u; i < RENDER_EXCHANGE_SLOTS; i++) {
        frame->borrowed_pictures[i] =
            arena_alloc(arena, frame_whole_pages(picture_bytes), RENDER_FRAME_PICTURE_ALIGNMENT);
        status = frame_host_pin(&frame->gpu, frame->borrowed_pictures[i], picture_bytes);
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
    /* A frame still on its way writes a picture: it is waited for first (frame_gpu_deinit would too, but after the pictures' unpinning). */
    while (frame->on_way != 0u && render_frame_finish(frame) == GPU_STATUS_OK) {
    }
    if (frame->temporal.borrowed_ao_cache != nullptr && frame->borrowed_memory != nullptr) {
        /* Its handle is in the frame arena, which outlives this: see frame_ao_cache_take. */
        if (device_memory_free(frame->borrowed_memory, frame->temporal.borrowed_ao_cache) != GPU_STATUS_OK) {
            gpu_error_log(&frame->borrowed_memory->last_error);
        }
    }
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
    render_sky_fill(sun_direction, height, post, sun);
}

void render_sky_fill(const float32_t sun_direction[3], float32_t height, post_settings_t *post, shade_sun_t *sun) {
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
    memcpy(post->sun_direction, sun_direction, sizeof(post->sun_direction));
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
        if (frame->sun_from_outside != 0u) {
            render_sky_fill(frame->sun_given, frame->sun_given[2], &frame->post, &frame->scene.sun);
        } else {
            render_day_fill(frame->day_fraction, &frame->post, &frame->scene.sun);
        }
    } else {
        frame_day_plain(&frame->post, &frame->scene.sun);
    }
    frame_water_apply(frame);
}

void render_frame_features_set(render_frame_t *frame, uint32_t features) {
    frame->scene.features = features & SHADE_FEATURES_EVERY;
    frame->temporal.sun_apart = (features & SHADE_FEATURE_SUN_APART) != 0u ? 1u : 0u;
    frame->temporal.flags =
        (uint8_t)((frame->temporal.flags & ~(uint32_t)POST_TEMPORAL_FIRST_GUESS) |
                  ((features & SHADE_FEATURE_AO_FIRST_GUESS) != 0u ? POST_TEMPORAL_FIRST_GUESS : 0u));
    frame_settings_apply(frame);
}

void render_frame_ao_fresh_one_set(render_frame_t *frame, bool one) {
    frame->temporal.flags = (uint8_t)((frame->temporal.flags & ~(uint32_t)POST_TEMPORAL_FRESH_ONE) |
                                      (one ? (uint32_t)POST_TEMPORAL_FRESH_ONE : 0u));
}

void render_frame_fog_set(render_frame_t *frame, float32_t fog_end) {
    frame->fog_end = fog_end;
    frame_settings_apply(frame);
}

void render_frame_time_set(render_frame_t *frame, float32_t day_fraction) {
    frame->sun_from_outside = 0u;
    frame->day_fraction = day_fraction - floorf(day_fraction);
    frame_settings_apply(frame);
}

void render_frame_sun_set(render_frame_t *frame, const float32_t direction[3]) {
    memcpy(frame->sun_given, direction, sizeof(frame->sun_given));
    frame->sun_from_outside = 1u;
    frame_settings_apply(frame);
}

void render_frame_under_water_set(render_frame_t *frame, bool under_water) {
    frame->eyes_under_water = under_water ? 1u : 0u;
    frame_water_apply(frame);
}

/* ---- Weather ------------------------------------------------------------------------------------------------------ */

float32_t render_weather_visibility(float32_t fog_end, float32_t fog, float32_t dust, float32_t rain_mm_h) {
    const float32_t thick = fog > dust ? fog : dust;
    const float32_t rain = rain_mm_h > 0.0f32 ? rain_mm_h : 0.0f32;
    const float32_t seen = fog_end * (1.0f32 - RENDER_FOG_WEATHER_CUT * thick) / (1.0f32 + rain / RENDER_FOG_RAIN_MM_H);
    return seen > RENDER_FOG_VISIBILITY_MIN ? seen : RENDER_FOG_VISIBILITY_MIN;
}

float32_t render_weather_flicker(float32_t age) {
    if (!(age >= 0.0f32) || age >= RENDER_WEATHER_BOLT_SECONDS) {
        return 0.0f32;
    }
    const float32_t third = RENDER_WEATHER_BOLT_SECONDS / 3.0f32;
    return age >= third && age < 2.0f32 * third ? 0.5f32 : 1.0f32;
}

/* Drops, by rain rate: none without rain, and between the two limits with it. */
static float32_t frame_weather_density(float32_t rain_mm_h) {
    if (!(rain_mm_h > 0.0f32)) {
        return 0.0f32;
    }
    const float32_t density = rain_mm_h / POST_RAIN_DENSITY_MM_H;
    return density < POST_WEATHER_DENSITY_MIN
               ? POST_WEATHER_DENSITY_MIN
               : (density > POST_WEATHER_DENSITY_MAX ? POST_WEATHER_DENSITY_MAX : density);
}

static float32_t frame_ease(float32_t from, float32_t to, float32_t share) {
    const float32_t eased = from + (to - from) * share;
    /* Close enough is there, so weather that has gone is the clear day's bytes again. */
    return fabsf(to - eased) < 1e-3f32 ? to : eased;
}

gpu_status_t render_weather_take(render_weather_t *weather, const render_weather_slot_t *slot, int64_t now_ns,
                                 device_memory_t *memory, const device_block_t *map_block) {
    return render_weather_take_staged(weather, slot, now_ns, memory, nullptr, map_block);
}

gpu_status_t render_weather_take_staged(render_weather_t *weather, const render_weather_slot_t *slot, int64_t now_ns,
                                        device_memory_t *memory, device_staging_t *staging,
                                        const device_block_t *map_block) {
    if (slot != nullptr) {
        if (slot->sent == 0u) {
            memset(weather, 0, sizeof(*weather));
            return GPU_STATUS_OK;
        }
        /* By the ticks between, so the same slots give the same numbers however often frames are drawn. */
        const uint64_t ticks = slot->world_ticks > weather->world_ticks ? slot->world_ticks - weather->world_ticks : 0u;
        const float32_t seconds = (float32_t)(ticks < 72000u ? ticks : 72000u) / (float32_t)MC_GAME_TICK_HZ;
        const bool at_once = weather->active == 0u || slot->forced != 0u;
        const float32_t share = at_once ? 1.0f32 : 1.0f32 - expf(-seconds / RENDER_WEATHER_EASE_SECONDS);
        weather->density = frame_ease(weather->density, frame_weather_density(slot->rain_mm_h), share);
        weather->cloud = frame_ease(weather->cloud, slot->cloud, share);
        weather->fog = frame_ease(weather->fog, slot->fog, share);
        weather->dust = frame_ease(weather->dust, slot->dust, share);
        weather->rain_mm_h = frame_ease(weather->rain_mm_h, slot->rain_mm_h, share);
        weather->snow_share = slot->snow_share;
        memcpy(weather->wind, slot->wind, sizeof(weather->wind));
        weather->flash = slot->flash;
        weather->bolt_age = slot->bolt_age;
        weather->bolt_distance = slot->bolt_distance;
        memcpy(weather->bolt, slot->bolt, sizeof(weather->bolt));
        memcpy(weather->bolt_origin, slot->bolt_origin, sizeof(weather->bolt_origin));
        memcpy(weather->map_origin, slot->map_origin, sizeof(weather->map_origin));
        weather->world_ticks = slot->world_ticks;
        weather->tick_ns = slot->tick_ns;
        weather->forced = slot->forced;
        weather->active = 1u;
        if (map_block != nullptr && (weather->map_sent == 0u || weather->map_revision != slot->map_revision)) {
            const gpu_status_t status =
                device_memory_write_async(memory, staging, map_block, 0u, slot->map, sizeof(slot->map));
            if (status != GPU_STATUS_OK) {
                return status;
            }
            weather->map_revision = slot->map_revision;
            weather->map_sent = 1u;
        }
    }
    if (weather->active == 0u) {
        return GPU_STATUS_OK;
    }
    /* Between ticks with threads, the drops go on falling; with none, one slot is one picture. */
    float32_t ahead = 0.0f32;
    if (now_ns != 0 && weather->tick_ns != 0 && now_ns > weather->tick_ns) {
        const float32_t past = (float32_t)(now_ns - weather->tick_ns) * 1e-9f32;
        ahead = past < RENDER_WEATHER_TIME_AHEAD ? past : RENDER_WEATHER_TIME_AHEAD;
    }
    weather->time_s =
        (float32_t)(weather->world_ticks % RENDER_WEATHER_WRAP_TICKS) / (float32_t)MC_GAME_TICK_HZ + ahead;
    weather->age_now = weather->bolt_age >= 0.0f32 ? weather->bolt_age + ahead : -1.0f32;
    return GPU_STATUS_OK;
}

void render_weather_apply(const render_weather_t *weather, const int32_t eye_cell[3], bool fog_on, float32_t fog_end,
                          post_settings_t *post, shade_sun_t *sun, post_weather_t *out) {
    memset(out, 0, sizeof(*out));
    const float32_t cloud = weather->cloud;
    if (cloud > 0.0f32) {
        /* Toward the sky's own brightness, a little blue: an overcast sky is grey, not black. */
        static const float32_t grey_tint[3] = {0.92f32, 0.95f32, 1.0f32};
        float32_t *const skies[2] = {post->sky_zenith, post->sky_horizon};
        for (uint32_t s = 0u; s < 2u; s++) {
            float32_t *const sky = skies[s];
            const float32_t brightness = 0.3f32 * sky[0] + 0.59f32 * sky[1] + 0.11f32 * sky[2];
            for (uint32_t i = 0u; i < 3u; i++) {
                sky[i] += (brightness * grey_tint[i] - sky[i]) * RENDER_CLOUD_SKY_GREY * cloud;
            }
        }
        for (uint32_t i = 0u; i < 3u; i++) {
            post->sun_glow[i] *= 1.0f32 - cloud;
            post->sun_disc[i] *= 1.0f32 - RENDER_CLOUD_DISC_CUT * cloud;
            post->moon_disc[i] *= 1.0f32 - RENDER_CLOUD_DISC_CUT * cloud;
            sun->direct[i] *= 1.0f32 - RENDER_CLOUD_DIRECT_CUT * cloud;
            sun->ambient[i] *= 1.0f32 - RENDER_CLOUD_AMBIENT_CUT * cloud;
        }
    }
    const float32_t flash = weather->flash * render_weather_flicker(weather->age_now);
    if (flash > 0.0f32) {
        for (uint32_t i = 0u; i < 3u; i++) {
            post->sky_zenith[i] += POST_FLASH_SKY * flash;
            post->sky_horizon[i] += POST_FLASH_SKY * flash;
            sun->ambient[i] += POST_FLASH_AMBIENT * flash;
        }
    }
    if (fog_on) {
        const float32_t seen = render_weather_visibility(fog_end, weather->fog, weather->dust, weather->rain_mm_h);
        if (seen < fog_end) {
            post->fog_end = seen;
            post->fog_start = seen * RENDER_FOG_WEATHER_START;
        }
    }
    /* Fog tends to grey under cloud and in fog, by as much daylight as there is, and to brown in dust. */
    const float32_t daylight = sun->ambient[1] >= 0.62f32 ? 1.0f32 : sun->ambient[1] / 0.62f32;
    const float32_t grey_share = cloud > weather->fog ? cloud : weather->fog;
    static const float32_t grey[3] = {0.55f32, 0.57f32, 0.60f32}, dust[3] = {0.62f32, 0.47f32, 0.30f32};
    if (weather->dust > grey_share) {
        for (uint32_t i = 0u; i < 3u; i++) {
            out->fog_rgb[i] = dust[i] * daylight;
        }
        out->fog_mix = weather->dust;
    } else if (grey_share > 0.0f32) {
        for (uint32_t i = 0u; i < 3u; i++) {
            out->fog_rgb[i] = grey[i] * daylight;
        }
        out->fog_mix = grey_share;
    }
    out->density = weather->density;
    out->snow_share = weather->snow_share;
    out->wind[0] = weather->wind[0];
    out->wind[1] = weather->wind[1];
    out->time_s = weather->time_s;
    out->flash = flash;
    memcpy(out->drop_rgb, sun->ambient, sizeof(out->drop_rgb));
    out->map_origin[0] = weather->map_origin[0] - eye_cell[0];
    out->map_origin[1] = weather->map_origin[1] - eye_cell[1];
    const float32_t flicker = render_weather_flicker(weather->age_now);
    if (flicker > 0.0f32) {
        out->bolt_alpha = flicker;
        out->bolt_width = POST_BOLT_WIDTH + POST_BOLT_WIDTH_PER_BLOCK * weather->bolt_distance;
        for (uint32_t j = 0u; j <= POST_BOLT_SEGMENTS; j++) {
            for (uint32_t i = 0u; i < 3u; i++) {
                /* The whole blocks first: their difference is exact in an integer, and small enough for a float. */
                out->bolt[j][i] = (float32_t)(weather->bolt_origin[i] - eye_cell[i]) + weather->bolt[j][i];
            }
        }
    }
}

gpu_status_t render_frame_draw(render_frame_t *frame, const world_buffers_t *buffers, const render_camera_t *camera) {
    return render_frame_draw_weather(frame, buffers, camera, nullptr, nullptr);
}

/* The oldest frame on its way, if its copy home is not queued yet, sent home into the exchange's back slot. */
static gpu_status_t frame_home_oldest(render_frame_t *frame) {
    const frame_flight_t *const flight = frame->gpu.borrowed_flight;
    if (frame->on_way == 0u || flight == nullptr ||
        flight->slots[frame->oldest_slot].state != (uint8_t)FRAME_SLOT_QUEUED) {
        return GPU_STATUS_OK;
    }
    return frame_gpu_home(&frame->gpu, frame->oldest_slot, frame->borrowed_pictures[frame->pictures.back]);
}

void render_frame_scale_set(render_frame_t *frame, uint32_t percent) {
    frame->scale_percent = (uint8_t)(percent == 0u || percent > POST_SCALE_WHOLE ? POST_SCALE_WHOLE : percent);
}

void render_frame_drawn_size(const render_frame_t *frame, uint32_t percent, uint32_t *out_width, uint32_t *out_height) {
    *out_width = post_scaled_side(frame->width, percent);
    *out_height = post_scaled_side(frame->height, percent);
}

/* Plan 008 phase 05: an entity overlay's rectangle, worked out in the drawn camera's pixels, in the output's: wide enough, as the kernel tests
 * every pixel in it against the box's edges. */
static void frame_rectangle_up(uint16_t min[2], uint16_t end[2], const traverse_camera_t *drawn,
                               const traverse_camera_t *output) {
    const uint32_t from[2] = {drawn->width, drawn->height}, to[2] = {output->width, output->height};
    if (min[0] >= end[0] || min[1] >= end[1]) {
        return; /* nothing to draw stays nothing */
    }
    for (uint32_t i = 0u; i < 2u; i++) {
        const uint64_t low = (uint64_t)min[i] * to[i] / from[i];
        const uint64_t high = ((uint64_t)end[i] * to[i] + from[i] - 1u) / from[i] + 1u;
        min[i] = (uint16_t)(low > 0u ? low - 1u : 0u);
        end[i] = (uint16_t)(high < to[i] ? high : to[i]);
    }
}

gpu_status_t render_frame_queue(render_frame_t *frame, const world_buffers_t *buffers, const render_camera_t *camera,
                                const render_weather_t *weather, const device_block_t *map_block) {
    /* Plan 008 phase 05: drawn at the rung, shown at the output's size. */
    uint32_t inner_width = 0u, inner_height = 0u;
    render_frame_drawn_size(frame, frame->scale_percent, &inner_width, &inner_height);
    const bool scaled = inner_width != frame->width || inner_height != frame->height;
    traverse_camera_t output_view;
    render_camera_to_traverse(camera, frame->width, frame->height, &output_view);
    traverse_camera_t view;
    render_camera_to_traverse_drawn(camera, frame->width, frame->height, inner_width, inner_height, &view);
    /* What the history is reprojected with has no jitter: otherwise a still camera would see its own picture move. */
    const traverse_camera_t steady = view;
    const uint32_t features = frame->scene.features;
    post_temporal_t *const temporal = &frame->temporal;
    /* Queued before, finished or not: the history is the last frame queued's, as the card draws them in order. */
    const bool drawn_before = frame->stats.frames + frame->on_way != 0u;
    temporal->ao_valid = drawn_before && (frame->drawn_features & SHADE_FEATURE_AO) != 0u ? 1u : 0u;
    temporal->history_valid = drawn_before && (frame->drawn_features & SHADE_FEATURE_TAA) != 0u ? 1u : 0u;
    /* Plan 008 phase 04: whether the camera moved since the frame before, and turned fast, for AO's first guess and TAA's history read. */
    const bool moved = drawn_before && memcmp(&steady, &temporal->previous, sizeof(steady)) != 0;
    const bool fast = moved && post_turn_pixels(&temporal->previous, &steady) > (float64_t)POST_TAA_FAST_PIXELS;
    uint32_t flags = temporal->flags & (uint32_t)(POST_TEMPORAL_FIRST_GUESS | POST_TEMPORAL_FRESH_ONE);
    flags |= moved ? (uint32_t)POST_TEMPORAL_MOVED : 0u;
    flags |= fast ? (uint32_t)POST_TEMPORAL_FAST : 0u;
    if ((features & SHADE_FEATURE_TAA) != 0u) {
        flags |= POST_TEMPORAL_JITTER;
        float32_t jitter[2];
        post_jitter(temporal->frame, jitter);
        post_camera_jitter(&view, jitter);
    }
    temporal->flags = (uint8_t)flags;
    /* This frame's sky, light and fog: the clear day's, and the weather over them on copies, so nothing of it stays. */
    shade_scene_t scene = frame->scene;
    post_settings_t post = frame->post;
    post_weather_t kernel_weather = {};
    const bool weathered = weather != nullptr && weather->active != 0u && (features & SHADE_FEATURE_WEATHER) != 0u;
    if (weathered) {
        render_weather_apply(weather, view.eye_cell, (features & SHADE_FEATURE_FOG) != 0u, frame->fog_end, &post,
                             &scene.sun, &kernel_weather);
    }
    for (uint32_t i = 0u; i < 3u; i++) {
        temporal->shaft_colour[i] = scene.sun.direct[i] * SHADE_SHAFT_STRENGTH;
    }
    const bool far = frame->borrowed_lod != nullptr && (features & SHADE_FEATURE_LOD) != 0u;
    /* Plan 009 phase 04: with a far field, the blocks end at each pixel's d_1, from this frame's own field of view and rows; without one, at
     * their edge. */
    const float32_t band_base =
        far ? render_lod_band_base(frame->borrowed_lod->pixel_error, camera->half_fov_tangent, frame->height) : 0.0f32;
    const traverse_world_t world = {
        .borrowed_table = buffers->borrowed_table,
        .borrowed_pool = buffers->borrowed_pool,
        .window = buffers->window,
        .borrowed_shapes = (frame->scene.features & SHADE_FEATURE_SHAPES) != 0u ? frame->borrowed_shapes : nullptr,
        .shape_count = BLOCK_COUNT,
        .top_limit = buffers->top_limit,
        .band_base = band_base,
        .band_dither = far ? frame->borrowed_lod->band_dither : 0.0f32,
        .band_seed = (features & SHADE_FEATURE_TAA) != 0u ? temporal->frame + 1u : 0u,
    };
    entity_rays_scene_t entities;
    if (frame->borrowed_entities != nullptr) {
        /* Past the fog a box is sky, and with no fog nothing is past anything. */
        const float32_t fog_far = (features & SHADE_FEATURE_FOG) != 0u ? frame->fog_end : 0.0f32;
        const gpu_status_t sent = render_entities_frame_staged(frame->borrowed_entities, &frame->gpu.staging, &steady,
                                                               features, fog_far, &entities);
        if (sent != GPU_STATUS_OK) {
            frame->gpu.last_error = frame->borrowed_memory->last_error;
            return sent;
        }
        if (scaled) {
            frame_rectangle_up(entities.mark.pixel_min, entities.mark.pixel_end, &steady, &output_view);
            frame_rectangle_up(entities.preview.pixel_min, entities.preview.pixel_end, &steady, &output_view);
        }
    }
    lod_scene_t far_scene;
    if (far) {
        render_lod_scene(frame->borrowed_lod, &far_scene);
        frame->borrowed_lod->band_base = band_base;
    }
    const shade_sun_t *const sun = &scene.sun;
    /* With no sun to speak of, nothing is lost by no rays towards it. */
    const bool sun_shines = sun->direct[0] + sun->direct[1] + sun->direct[2] > 0.0f32;
    const lod_light_t far_light = {
        .borrowed_textures = frame->scene.borrowed_textures,
        .borrowed_faces = frame->scene.borrowed_faces,
        .borrowed_pixels = frame->scene.borrowed_pixels,
        .sun_direction = {sun->direction[0], sun->direction[1], sun->direction[2]},
        .material_count = frame->scene.material_count,
        .shadows = (features & SHADE_FEATURE_SHADOW) != 0u && sun_shines ? 1u : 0u,
        .water_material = frame->scene.water_material,
        .water_tint = (features & SHADE_FEATURE_WATER_TINT) != 0u ? 1u : 0u,
        .stops_after_faces = 1u,
    };
    frame->scene.borrowed_lod_hits = far ? far_scene.borrowed_hits : nullptr;
    scene.borrowed_lod_hits = frame->scene.borrowed_lod_hits;
    /* Plan 008 phase 03: the device picture of this frame's flight slot, so the copy home of one frame and the post of the next never
     * meet. */
    const uint32_t next_slot = frame->gpu.borrowed_flight != nullptr ? frame->gpu.borrowed_flight->next : 0u;
    frame_targets_t targets = frame->targets;
    if (next_slot == 1u && frame->borrowed_xrgb_second != nullptr) {
        targets.borrowed_xrgb = frame->borrowed_xrgb_second;
    }
    const frame_args_t args = {
        .borrowed_lod = far ? &far_scene : nullptr,
        .borrowed_lod_light = far ? &far_light : nullptr,
        .borrowed_world = &world,
        .borrowed_scene = &scene,
        .borrowed_post = &post,
        .borrowed_weather = weathered ? &kernel_weather : nullptr,
        .borrowed_weather_map = weathered && weather->map_sent != 0u ? map_block : nullptr,
        .borrowed_camera = &view,
        .borrowed_targets = &targets,
        .borrowed_host_xrgb = nullptr,
        .borrowed_entities = frame->borrowed_entities != nullptr ? &entities : nullptr,
        .borrowed_output_camera = scaled ? &output_view : nullptr,
    };
    uint32_t slot = 0u;
    gpu_status_t status = frame_gpu_queue(&frame->gpu, &args, &slot);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    /* What this frame writes is what the next one reads. */
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
    frame->on_way++;
    if (frame->on_way == 1u) {
        frame->oldest_slot = (uint8_t)slot;
    }
    /* The oldest frame's picture goes home into the exchange's back slot, free since the last publish: at once for a frame queued alone, and
     * for the one before only now, behind this frame's copies to the card, so those are never queued after a copy home still waiting for its
     * picture (measured: the card then waited the whole readback before this frame's first kernel). A second frame goes home at the first's
     * finish. */
    return frame_home_oldest(frame);
}

gpu_status_t render_frame_finish(render_frame_t *frame) {
    if (frame->on_way == 0u) {
        frame->gpu.last_error =
            (gpu_error_t){"frame: nothing on its way to finish", nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    gpu_status_t status = frame_home_oldest(frame);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    frame_timings_t timings;
    const uint32_t slot = frame->oldest_slot;
    frame->on_way--;
    frame->oldest_slot = (uint8_t)((slot + 1u) % FRAME_FLIGHT_SLOTS);
    status = frame_gpu_finish(&frame->gpu, slot, &timings);
    if (status != GPU_STATUS_OK) {
        return status;
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
    stats->readback_bytes += post_xrgb_bytes(frame->width, frame->height);
    render_exchange_publish(&frame->pictures);
    return GPU_STATUS_OK;
}

bool render_frame_oldest_done(render_frame_t *frame) {
    return frame->on_way != 0u && frame_gpu_done(&frame->gpu, frame->oldest_slot);
}

bool render_frame_oldest_whole(render_frame_t *frame) {
    return frame->on_way != 0u && frame_gpu_whole(&frame->gpu, frame->oldest_slot);
}

gpu_status_t render_frame_draw_weather(render_frame_t *frame, const world_buffers_t *buffers,
                                       const render_camera_t *camera, const render_weather_t *weather,
                                       const device_block_t *map_block) {
    /* A frame on its way is handed over first, so this one's picture is the newest when it returns. */
    while (frame->on_way != 0u) {
        const gpu_status_t status = render_frame_finish(frame);
        if (status != GPU_STATUS_OK) {
            return status;
        }
    }
    const gpu_status_t status = render_frame_queue(frame, buffers, camera, weather, map_block);
    return status == GPU_STATUS_OK ? render_frame_finish(frame) : status;
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
