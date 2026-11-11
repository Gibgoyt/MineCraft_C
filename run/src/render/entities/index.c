/* See README.md. Boxes from the sim to the gpu thread, each frame's lists of which boxes each tile of the picture may see, and the
 * textures for what is drawn with no block's faces. */
#include "render/entities/index.h"

#include "log/index.h"
#include "tables/blocks/index.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static_assert((uint32_t)BLOCK_DROP_ITEM_NO_BLOCK_COUNT + 1u + MOB_COUNT <= (uint32_t)ENTITY_RAYS_TEXTURES_MAX);
static_assert((uint32_t)TRAVERSE_TILE_PIXELS == 8u);
static_assert((uint32_t)ENTITY_RAYS_BOXES_MAX < (uint32_t)ENTITY_RAYS_NO_BOX);
static_assert((uint32_t)ENTITY_RAYS_NO_MATERIAL == (uint32_t)SHADE_NO_MATERIAL);

/* An item on the ground goes round once in this many ticks: a radian a second, at 20 ticks a second, as Minecraft's published rate. */
#define RENDER_ENTITIES_YAW_PER_TICK 0.05f32

enum : uint32_t {
    RENDER_ENTITIES_NAME_BYTES = 64u,
    /* The near boxes, and then the rest. */
    RENDER_ENTITIES_PASSES = 2u,
};

uint64_t render_entities_lists_bytes(uint32_t width, uint32_t height) {
    return entity_rays_textures_offset(width, height) + (uint64_t)ENTITY_RAYS_TEXTURES_MAX * sizeof(shade_texture_t);
}

static uint64_t entities_tiles(uint32_t width, uint32_t height) {
    return (uint64_t)traverse_tiles_across(width) * traverse_tiles_across(height);
}

uint64_t render_entities_arena_bytes(uint32_t width, uint32_t height) {
    /* Each allocation can waste its alignment. */
    return sizeof(render_entities_t) + MC_CPU_CACHE_LINE_BYTES +
           (uint64_t)RENDER_EXCHANGE_SLOTS * ((uint64_t)ENTITY_RAYS_BOXES_MAX * sizeof(render_entities_item_t) + 8u) +
           (uint64_t)ENTITY_RAYS_BOXES_MAX * sizeof(entity_rays_box_t) + 8u +
           (entities_tiles(width, height) + 1u) * sizeof(uint32_t) + 8u +
           (uint64_t)ENTITY_RAYS_LINKS_MAX * sizeof(uint16_t) + 8u +
           (uint64_t)ENTITY_RAYS_BOXES_MAX * 4u * sizeof(uint16_t) + 8u +
           (uint64_t)BLOCK_DROP_ITEM_COUNT * sizeof(uint16_t) + 8u +
           (uint64_t)ENTITY_RAYS_TEXTURES_MAX * sizeof(shade_texture_t) + 8u;
}

static bool entities_texture_row(const texture_t *texture, const char *name, shade_texture_t *out) {
    uint32_t id = 0u;
    texture_info_t info;
    if (!texture_find(texture, name, &id) || !texture_info_get(texture, id, &info)) {
        return false;
    }
    const texture_blob_entry_t *const entry = info.borrowed_entry;
    *out = (shade_texture_t){
        .pixels_offset = entry->pixels_offset,
        .width = (uint16_t)entry->width,
        .height = (uint16_t)entry->height,
        .level_count = entry->level_count,
    };
    memcpy(out->level_offset, entry->level_offset, sizeof(out->level_offset));
    memcpy(out->mean_rgba, entry->mean_rgba, sizeof(out->mean_rgba));
    return true;
}

gpu_status_t render_entities_init(render_entities_t *entities, arena_t *arena, device_memory_t *memory,
                                  const texture_t *texture, const device_block_t *lists, const device_block_t *hits,
                                  uint32_t width, uint32_t height) {
    memset(entities, 0, sizeof(*entities));
    render_exchange_start(&entities->exchange);
    entities->reading = entities->exchange.front;
    entities->borrowed_memory = memory;
    entities->borrowed_lists = lists;
    entities->borrowed_hits = hits;
    entities->width = width;
    entities->height = height;
    for (uint32_t i = 0u; i < RENDER_EXCHANGE_SLOTS; i++) {
        entities->slots[i].borrowed_items = arena_alloc_array(
            arena, ENTITY_RAYS_BOXES_MAX, sizeof(render_entities_item_t), alignof(render_entities_item_t));
    }
    entities->borrowed_boxes =
        arena_alloc_array(arena, ENTITY_RAYS_BOXES_MAX, sizeof(entity_rays_box_t), alignof(entity_rays_box_t));
    entities->borrowed_starts =
        arena_alloc_array(arena, entities_tiles(width, height) + 1u, sizeof(uint32_t), alignof(uint32_t));
    entities->borrowed_links = arena_alloc_array(arena, ENTITY_RAYS_LINKS_MAX, sizeof(uint16_t), alignof(uint16_t));
    entities->borrowed_rects =
        arena_alloc_array(arena, ENTITY_RAYS_BOXES_MAX * 4u, sizeof(uint16_t), alignof(uint16_t));
    entities->borrowed_item_textures =
        arena_alloc_array(arena, BLOCK_DROP_ITEM_COUNT, sizeof(uint16_t), alignof(uint16_t));
    shade_texture_t *const table =
        arena_alloc_array(arena, ENTITY_RAYS_TEXTURES_MAX, sizeof(shade_texture_t), alignof(shade_texture_t));
    memset(table, 0, (uint64_t)ENTITY_RAYS_TEXTURES_MAX * sizeof(shade_texture_t));
    if (!entities_texture_row(texture, "blocks/crack", &table[ENTITY_RAYS_TEXTURE_CRACK])) {
        LOG_ERROR("render: the blob has no blocks/crack, which a block being mined draws with");
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    uint32_t rows = ENTITY_RAYS_TEXTURE_CRACK + 1u;
    for (uint32_t item = 0u; item < BLOCK_DROP_ITEM_COUNT; item++) {
        entities->borrowed_item_textures[item] = ENTITY_RAYS_NO_TEXTURE;
        if (block_drop_items[item].block != BLOCK_NO_ID) {
            continue;
        }
        char name[RENDER_ENTITIES_NAME_BYTES];
        (void)snprintf(name, sizeof(name), "items/%s", block_drop_items[item].name);
        /* An item with none draws in the colour that says so: the table of drops is config, and this should show. */
        if (entities_texture_row(texture, name, &table[rows])) {
            entities->borrowed_item_textures[item] = (uint16_t)rows;
            rows++;
        } else {
            LOG_WARN("render: no texture %s, so that item draws as missing", name);
        }
    }
    for (uint32_t mob = 0u; mob < MOB_COUNT; mob++) {
        entities->mob_textures[mob] = ENTITY_RAYS_NO_TEXTURE;
        if (mob_defs[mob].texture == nullptr) {
            continue;
        }
        /* As with an item: a mob with none draws in the colour that says so. */
        if (entities_texture_row(texture, mob_defs[mob].texture, &table[rows])) {
            entities->mob_textures[mob] = (uint16_t)rows;
            rows++;
        } else {
            LOG_WARN("render: no texture %s, so that mob draws as missing", mob_defs[mob].texture);
        }
    }
    const gpu_status_t status = device_memory_write(memory, lists, entity_rays_textures_offset(width, height), table,
                                                    (uint64_t)rows * sizeof(shade_texture_t));
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
    }
    return status;
}

void render_entities_deinit(render_entities_t *entities) {
    memset(entities, 0, sizeof(*entities));
}

/* ---- The sim's side ------------------------------------------------------------------------------------------------ */

void render_entities_begin(render_entities_t *entities) {
    render_entities_slot_t *const slot = &entities->slots[entities->exchange.back];
    slot->count = 0u;
    memset(&slot->target, 0, sizeof(slot->target));
    memset(&slot->preview, 0, sizeof(slot->preview));
}

void render_entities_add(render_entities_t *entities, const render_entities_item_t *item) {
    render_entities_slot_t *const slot = &entities->slots[entities->exchange.back];
    if (slot->count >= ENTITY_RAYS_BOXES_MAX) {
        entities->stats.out_of_snapshots++;
        return;
    }
    slot->borrowed_items[slot->count] = *item;
    slot->count++;
}

void render_entities_add_item_drop(render_entities_t *entities, uint16_t item, const float64_t position[3],
                                   float32_t size, uint32_t age, uint8_t stored) {
    const float32_t half = size * 0.5f32;
    const uint16_t block = block_drop_items[item].block;
    /* On a float64_t and only then wrapped, so that a very old item's yaw is still exact. */
    const float64_t turns = (float64_t)age * (float64_t)RENDER_ENTITIES_YAW_PER_TICK;
    const render_entities_item_t row = {
        .center = {position[0], position[1], position[2]},
        .half = {half, half, half},
        .yaw = (float32_t)fmod(turns, 6.283185307179586f64),
        .material = block != BLOCK_NO_ID ? block : (uint16_t)ENTITY_RAYS_NO_MATERIAL,
        .texture = block != BLOCK_NO_ID ? (uint16_t)ENTITY_RAYS_NO_TEXTURE : entities->borrowed_item_textures[item],
        .stored = stored,
    };
    render_entities_add(entities, &row);
}

void render_entities_add_mob_part(render_entities_t *entities, uint16_t mob, const float64_t center[3],
                                  const float32_t half[3], float32_t yaw, float32_t pitch, uint16_t skin_u,
                                  uint16_t skin_v, const float32_t size[3], uint8_t stored) {
    const render_entities_item_t row = {
        .center = {center[0], center[1], center[2]},
        .half = {half[0], half[1], half[2]},
        .yaw = yaw,
        .pitch = pitch,
        .material = ENTITY_RAYS_NO_MATERIAL,
        .texture = mob < MOB_COUNT ? entities->mob_textures[mob] : (uint16_t)ENTITY_RAYS_NO_TEXTURE,
        .skin_u = skin_u,
        .skin_v = skin_v,
        .stored = stored,
        .skin_size = {(uint8_t)size[0], (uint8_t)size[1], (uint8_t)size[2]},
    };
    render_entities_add(entities, &row);
}

void render_entities_target_set(render_entities_t *entities, const render_entities_target_t *target) {
    entities->slots[entities->exchange.back].target = *target;
}

void render_entities_preview_set(render_entities_t *entities, const render_entities_preview_t *preview) {
    render_entities_preview_t *const kept = &entities->slots[entities->exchange.back].preview;
    *kept = *preview;
    kept->count = preview->count < ENTITY_RAYS_PREVIEW_CELLS ? preview->count : (uint8_t)ENTITY_RAYS_PREVIEW_CELLS;
}

void render_entities_preview_show(render_entities_t *entities, bool shown) {
    atomic_store_explicit(&entities->preview_hidden, (uint8_t)(shown ? 0u : 1u), memory_order_relaxed);
}

void render_entities_publish(render_entities_t *entities) {
    (void)render_exchange_publish(&entities->exchange);
}

uint32_t render_entities_crack_frame(float32_t progress) {
    if (!(progress > 0.0f32)) {
        return 0u;
    }
    const uint32_t frame = (uint32_t)(progress * (float32_t)ENTITY_RAYS_CRACK_FRAMES);
    return (frame >= ENTITY_RAYS_CRACK_FRAMES ? ENTITY_RAYS_CRACK_FRAMES - 1u : frame) + 1u;
}

/* ---- A frame ------------------------------------------------------------------------------------------------------- */

static float32_t entities_dot(const float32_t a[3], const float32_t b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

typedef struct entities_screen_t {
    float32_t right_squared; /* 0 */
    float32_t up_squared;    /* 4 */
} entities_screen_t;
static_assert(sizeof(entities_screen_t) == 8);

/* Where a point from the eye lands on the picture, by how far ahead it is. Past its edges, for what is off it. */
static void entities_project(const traverse_camera_t *camera, const entities_screen_t *screen, const float32_t point[3],
                             float32_t ahead, float32_t *out_x, float32_t *out_y) {
    const float32_t u = entities_dot(point, camera->right) / (ahead * screen->right_squared);
    const float32_t v = entities_dot(point, camera->up) / (ahead * screen->up_squared);
    *out_x = (u + 1.0f32) * 0.5f32 * (float32_t)camera->width;
    *out_y = (1.0f32 - v) * 0.5f32 * (float32_t)camera->height;
}

bool render_entities_pixels(const traverse_camera_t *camera, const float32_t center[3], const float32_t half[3],
                            float32_t cos_yaw, float32_t sin_yaw, float32_t margin, uint32_t out[4]) {
    /* What a turned box reaches along the world's axes. */
    const float32_t reach[3] = {fabsf(cos_yaw) * half[0] + fabsf(sin_yaw) * half[1],
                                fabsf(sin_yaw) * half[0] + fabsf(cos_yaw) * half[1], half[2]};
    float32_t corners[8][3];
    float32_t ahead[8];
    uint32_t in_front = 0u;
    for (uint32_t corner = 0u; corner < 8u; corner++) {
        corners[corner][0] = center[0] + ((corner & 1u) != 0u ? reach[0] : -reach[0]);
        corners[corner][1] = center[1] + ((corner & 2u) != 0u ? reach[1] : -reach[1]);
        corners[corner][2] = center[2] + ((corner & 4u) != 0u ? reach[2] : -reach[2]);
        ahead[corner] = entities_dot(corners[corner], camera->forward);
        in_front += ahead[corner] >= RENDER_ENTITIES_NEAR ? 1u : 0u;
    }
    if (in_front == 0u) {
        return false;
    }
    /* What is in front of the eye is the corners there, and where each edge from them crosses to behind it. */
    float32_t low_x = TRAVERSE_COARSE_SKY, high_x = -TRAVERSE_COARSE_SKY, low_y = TRAVERSE_COARSE_SKY,
              high_y = -TRAVERSE_COARSE_SKY;
    const entities_screen_t screen = {.right_squared = entities_dot(camera->right, camera->right),
                                      .up_squared = entities_dot(camera->up, camera->up)};
    for (uint32_t corner = 0u; corner < 8u; corner++) {
        if (ahead[corner] < RENDER_ENTITIES_NEAR) {
            continue;
        }
        float32_t x = 0.0f32, y = 0.0f32;
        entities_project(camera, &screen, corners[corner], ahead[corner], &x, &y);
        low_x = x < low_x ? x : low_x;
        high_x = x > high_x ? x : high_x;
        low_y = y < low_y ? y : low_y;
        high_y = y > high_y ? y : high_y;
        for (uint32_t axis = 0u; axis < 3u; axis++) {
            const uint32_t other = corner ^ (1u << axis);
            if (ahead[other] >= RENDER_ENTITIES_NEAR) {
                continue;
            }
            const float32_t cut = (ahead[corner] - RENDER_ENTITIES_NEAR) / (ahead[corner] - ahead[other]);
            const float32_t crossing[3] = {corners[corner][0] + (corners[other][0] - corners[corner][0]) * cut,
                                           corners[corner][1] + (corners[other][1] - corners[corner][1]) * cut,
                                           corners[corner][2] + (corners[other][2] - corners[corner][2]) * cut};
            entities_project(camera, &screen, crossing, RENDER_ENTITIES_NEAR, &x, &y);
            low_x = x < low_x ? x : low_x;
            high_x = x > high_x ? x : high_x;
            low_y = y < low_y ? y : low_y;
            high_y = y > high_y ? y : high_y;
        }
    }
    low_x = floorf(low_x - margin);
    low_y = floorf(low_y - margin);
    high_x = ceilf(high_x + margin);
    high_y = ceilf(high_y + margin);
    if (high_x <= 0.0f32 || high_y <= 0.0f32 || low_x >= (float32_t)camera->width ||
        low_y >= (float32_t)camera->height) {
        return false;
    }
    out[0] = low_x < 0.0f32 ? 0u : (uint32_t)low_x;
    out[1] = low_y < 0.0f32 ? 0u : (uint32_t)low_y;
    out[2] = high_x >= (float32_t)camera->width ? camera->width : (uint32_t)high_x;
    out[3] = high_y >= (float32_t)camera->height ? camera->height : (uint32_t)high_y;
    return true;
}

/* Where a world position is from the eye. In float64_t all the way, as it is a difference of two things that may both be large. */
static void entities_from_eye(const traverse_camera_t *camera, const float64_t point[3], float32_t out[3]) {
    for (uint32_t i = 0u; i < 3u; i++) {
        out[i] = (float32_t)(point[i] - (float64_t)camera->eye_cell[i] - (float64_t)camera->eye_frac[i]);
    }
}

/* Keeps what can be seen of the snapshot, nearest first, as far as the links go. Returns how many links it wants. */
static uint32_t entities_keep(render_entities_t *entities, const render_entities_slot_t *slot,
                              const traverse_camera_t *camera, float32_t far, uint32_t *out_boxes) {
    uint32_t kept = 0u;
    uint64_t links = 0u;
    for (uint32_t pass = 0u; pass < RENDER_ENTITIES_PASSES; pass++) {
        for (uint32_t i = 0u; i < slot->count; i++) {
            const render_entities_item_t *const item = &slot->borrowed_items[i];
            float32_t center[3];
            entities_from_eye(camera, item->center, center);
            const float32_t distance = sqrtf(entities_dot(center, center));
            const bool near_one = (float64_t)distance < RENDER_ENTITIES_NEAR_BLOCKS;
            if (near_one != (pass == 0u)) {
                continue;
            }
            const float32_t cos_yaw = cosf(item->yaw), sin_yaw = sinf(item->yaw);
            uint32_t pixels[4];
            const float32_t size = sqrtf(entities_dot(item->half, item->half));
            const float32_t cos_pitch = cosf(item->pitch), sin_pitch = sinf(item->pitch);
            /* What it reaches once tilted: a box with no pitch this big holds all of it, and that is what the corners take. */
            const float32_t reach[3] = {fabsf(cos_pitch) * item->half[0] + fabsf(sin_pitch) * item->half[2],
                                        item->half[1],
                                        fabsf(sin_pitch) * item->half[0] + fabsf(cos_pitch) * item->half[2]};
            if ((far > 0.0f32 && post_fog_way(far * POST_FOG_START_SHARE, distance, center[2]) - size >= far) ||
                !render_entities_pixels(camera, center, reach, cos_yaw, sin_yaw, RENDER_ENTITIES_MARGIN_PIXELS,
                                        pixels)) {
                entities->stats.out_of_sight++;
                continue;
            }
            const uint32_t tile_x = pixels[0] / TRAVERSE_TILE_PIXELS, tile_y = pixels[1] / TRAVERSE_TILE_PIXELS;
            const uint32_t end_x = traverse_tiles_across(pixels[2]), end_y = traverse_tiles_across(pixels[3]);
            const uint64_t area = (uint64_t)(end_x - tile_x) * (end_y - tile_y);
            if (links + area > ENTITY_RAYS_LINKS_MAX) {
                entities->stats.out_of_links++;
                continue;
            }
            links += area;
            entities->borrowed_boxes[kept] = (entity_rays_box_t){
                .center = {center[0], center[1], center[2]},
                .half = {item->half[0], item->half[1], item->half[2]},
                .cos_yaw = cos_yaw,
                .sin_yaw = sin_yaw,
                .cos_pitch = cos_pitch,
                .sin_pitch = sin_pitch,
                .material = item->material,
                .texture = item->texture,
                .skin_u = item->skin_u,
                .skin_v = item->skin_v,
                .stored = item->stored,
                .skin_size = {item->skin_size[0], item->skin_size[1], item->skin_size[2]},
            };
            uint16_t *const rect = &entities->borrowed_rects[(uint64_t)kept * 4u];
            rect[0] = (uint16_t)tile_x;
            rect[1] = (uint16_t)tile_y;
            rect[2] = (uint16_t)end_x;
            rect[3] = (uint16_t)end_y;
            kept++;
        }
    }
    *out_boxes = kept;
    return (uint32_t)links;
}

/* Each tile's list, in the one array: how many each tile has, where its list would end, and then each link put in from the end. */
static void entities_lists_fill(render_entities_t *entities, const traverse_camera_t *camera, uint32_t boxes) {
    const uint32_t across = traverse_tiles_across(camera->width);
    const uint64_t tiles = (uint64_t)across * traverse_tiles_across(camera->height);
    uint32_t *const starts = entities->borrowed_starts;
    memset(starts, 0, (tiles + 1u) * sizeof(uint32_t));
    for (uint32_t box = 0u; box < boxes; box++) {
        const uint16_t *const rect = &entities->borrowed_rects[(uint64_t)box * 4u];
        for (uint32_t y = rect[1]; y < rect[3]; y++) {
            for (uint32_t x = rect[0]; x < rect[2]; x++) {
                starts[(uint64_t)y * across + x]++;
            }
        }
    }
    uint32_t running = 0u;
    for (uint64_t tile = 0u; tile < tiles; tile++) {
        running += starts[tile];
        starts[tile] = running;
    }
    starts[tiles] = running;
    for (uint32_t box = boxes; box-- > 0u;) {
        const uint16_t *const rect = &entities->borrowed_rects[(uint64_t)box * 4u];
        for (uint32_t y = rect[1]; y < rect[3]; y++) {
            for (uint32_t x = rect[0]; x < rect[2]; x++) {
                const uint64_t tile = (uint64_t)y * across + x;
                starts[tile]--;
                entities->borrowed_links[starts[tile]] = (uint16_t)box;
            }
        }
    }
}

static void entities_mark_fill(const render_entities_target_t *target, const traverse_camera_t *camera,
                               entity_rays_mark_t *out) {
    memset(out, 0, sizeof(*out));
    if (target->crack_frame != 0u) {
        out->crack_frame = target->crack_frame;
        for (uint32_t i = 0u; i < 3u; i++) {
            out->crack_cell[i] = target->crack_cell[i] - camera->eye_cell[i];
        }
    }
    if (target->outline == 0u) {
        return;
    }
    float32_t center[3], half[3];
    entities_from_eye(camera, target->box_min, out->box_min);
    entities_from_eye(camera, target->box_max, out->box_max);
    for (uint32_t i = 0u; i < 3u; i++) {
        center[i] = (out->box_min[i] + out->box_max[i]) * 0.5f32;
        half[i] = (out->box_max[i] - out->box_min[i]) * 0.5f32;
    }
    uint32_t pixels[4];
    const float32_t margin = (float32_t)entity_rays_outline_pixels(camera->height) + RENDER_ENTITIES_MARGIN_PIXELS;
    if (render_entities_pixels(camera, center, half, 1.0f32, 0.0f32, margin, pixels)) {
        out->outline = 1u;
        out->pixel_min[0] = (uint16_t)pixels[0];
        out->pixel_min[1] = (uint16_t)pixels[1];
        out->pixel_end[0] = (uint16_t)pixels[2];
        out->pixel_end[1] = (uint16_t)pixels[3];
    }
}

/* The preview's cells from the eye's cell, its colour, and the pixels a box around all of them can touch. A whole cell each, as no shape
 * reaches out of its cell. */
static void entities_preview_fill(const render_entities_preview_t *preview, const traverse_camera_t *camera,
                                  entity_rays_preview_t *out) {
    memset(out, 0, sizeof(*out));
    if (preview->count == 0u) {
        return;
    }
    float64_t low[3], high[3];
    for (uint32_t i = 0u; i < 3u; i++) {
        low[i] = (float64_t)preview->cells[0][i];
        high[i] = low[i] + 1.0f64;
    }
    for (uint32_t c = 0u; c < preview->count; c++) {
        out->voxels[c] = preview->voxels[c];
        for (uint32_t i = 0u; i < 3u; i++) {
            out->cells[c][i] = preview->cells[c][i] - camera->eye_cell[i];
            low[i] = fmin(low[i], (float64_t)preview->cells[c][i]);
            high[i] = fmax(high[i], (float64_t)preview->cells[c][i] + 1.0f64);
        }
    }
    float32_t box_min[3], box_max[3], center[3], half[3];
    entities_from_eye(camera, low, box_min);
    entities_from_eye(camera, high, box_max);
    for (uint32_t i = 0u; i < 3u; i++) {
        center[i] = (box_min[i] + box_max[i]) * 0.5f32;
        half[i] = (box_max[i] - box_min[i]) * 0.5f32;
    }
    uint32_t pixels[4];
    if (render_entities_pixels(camera, center, half, 1.0f32, 0.0f32, RENDER_ENTITIES_MARGIN_PIXELS, pixels)) {
        out->count = preview->count;
        out->xrgb = preview->blocked != 0u ? ENTITY_RAYS_PREVIEW_BLOCKED_XRGB : ENTITY_RAYS_PREVIEW_OK_XRGB;
        out->pixel_min[0] = (uint16_t)pixels[0];
        out->pixel_min[1] = (uint16_t)pixels[1];
        out->pixel_end[0] = (uint16_t)pixels[2];
        out->pixel_end[1] = (uint16_t)pixels[3];
    }
}

gpu_status_t render_entities_frame(render_entities_t *entities, const traverse_camera_t *camera, uint32_t features,
                                   float32_t far, entity_rays_scene_t *out) {
    uint32_t slot_number = 0u;
    if (render_exchange_take(&entities->exchange, &slot_number)) {
        entities->reading = slot_number;
    }
    const render_entities_slot_t *const slot = &entities->slots[entities->reading];
    memset(out, 0, sizeof(*out));
    out->borrowed_lists = entities->borrowed_lists;
    out->borrowed_hits = entities->borrowed_hits;
    if ((features & SHADE_FEATURE_OUTLINE) != 0u) {
        entities_mark_fill(&slot->target, camera, &out->mark);
    }
    if (atomic_load_explicit(&entities->preview_hidden, memory_order_relaxed) == 0u) {
        entities_preview_fill(&slot->preview, camera, &out->preview);
    }
    if ((features & SHADE_FEATURE_ENTITIES) == 0u || slot->count == 0u) {
        return GPU_STATUS_OK;
    }
    uint32_t boxes = 0u;
    const uint32_t links = entities_keep(entities, slot, camera, far, &boxes);
    if (boxes == 0u) {
        return GPU_STATUS_OK;
    }
    entities_lists_fill(entities, camera, boxes);
    const uint64_t tiles = entities_tiles(camera->width, camera->height);
    device_memory_t *const memory = entities->borrowed_memory;
    const device_block_t *const lists = entities->borrowed_lists;
    gpu_status_t status =
        device_memory_write(memory, lists, 0u, entities->borrowed_boxes, (uint64_t)boxes * sizeof(entity_rays_box_t));
    if (status == GPU_STATUS_OK) {
        status = device_memory_write(memory, lists, entity_rays_starts_offset(), entities->borrowed_starts,
                                     (tiles + 1u) * sizeof(uint32_t));
    }
    if (status == GPU_STATUS_OK) {
        status = device_memory_write(memory, lists, entity_rays_links_offset(camera->width, camera->height),
                                     entities->borrowed_links, (uint64_t)links * sizeof(uint16_t));
    }
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&memory->last_error);
        return status;
    }
    out->box_count = boxes;
    out->link_count = links;
    entities->stats.frames++;
    entities->stats.boxes += boxes;
    entities->stats.links += links;
    return GPU_STATUS_OK;
}
