/* See README.md. */
#include "game/view/index.h"

#include <math.h>
#include <string.h>

enum : uint32_t {
    GAME_VIEW_FOV_DEFAULT = 70u,          /* what RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT is the tangent of half of */
    GAME_VIEW_FOV_MIN = SETTINGS_FOV_MIN, /* the narrowest a person may choose, which the table is sized for */
    GAME_VIEW_PERCENT = 100u,
    GAME_VIEW_BYTES_PER_PIXEL = 4u,
};

#define GAME_VIEW_DEGREES_TO_HALF_RADIANS (3.14159265358979f32 / 360.0f32)

static uint32_t game_view_scaled(uint32_t size, uint32_t percent);

/* The bands for a config at a field of view, on a picture of this many rows. */
static render_lod_bands_t game_view_bands(uint32_t fov_degrees, uint32_t height) {
    return render_lod_bands((float64_t)RENDER_LOD_PIXEL_ERROR, RENDER_LOD_NEAR_GUARANTEE,
                            (float64_t)game_view_half_fov_tangent(fov_degrees), height);
}

uint32_t game_view_table_chunks(const game_config_t *config) {
    /* The narrowest field of view wants the most level 0: d_1 grows with tan(fov / 2) going down. */
    const render_lod_bands_t narrowest =
        game_view_bands(GAME_VIEW_FOV_MIN, game_view_scaled(config->height, config->render_scale_percent));
    uint32_t wanted = render_lod_block_radius(&narrowest, config->render_distance);
    wanted = config->block_radius_given > wanted ? config->block_radius_given : wanted;
    wanted = wanted > config->render_distance ? config->render_distance : wanted;
    return config->table_chunks > wanted ? config->table_chunks : wanted;
}

float32_t game_view_fog_end(uint32_t reach_chunks) {
    const uint32_t fog_chunks = reach_chunks > 1u ? reach_chunks - 1u : 1u;
    return (float32_t)(fog_chunks * COLUMN_SIZE);
}

static uint32_t game_view_scaled(uint32_t size, uint32_t percent) {
    if (percent == 0u || percent >= GAME_VIEW_PERCENT) {
        return size;
    }
    const uint32_t scaled = (uint32_t)((uint64_t)size * percent / GAME_VIEW_PERCENT);
    return scaled != 0u ? scaled : 1u;
}

void game_view_work_out(const game_config_t *config, uint32_t table_chunks, game_view_t *out) {
    memset(out, 0, sizeof(*out));
    out->width = game_view_scaled(config->width, config->render_scale_percent);
    out->height = game_view_scaled(config->height, config->render_scale_percent);
    /* Plan 009 phase 04: level 0 as far as d_1 and a margin, by the pixel error, the field of view and the picture's rows, within the
     * table; the levels past it by the same bands. */
    const render_lod_bands_t bands = game_view_bands(config->fov_degrees, out->height);
    uint32_t wanted = config->block_radius_given != 0u ? config->block_radius_given
                                                       : render_lod_block_radius(&bands, config->render_distance);
    wanted = wanted > config->render_distance ? config->render_distance : wanted;
    out->block_radius = wanted < table_chunks ? wanted : table_chunks;
    const bool far_field =
        config->render_distance > out->block_radius && (config->features_off & SHADE_FEATURE_LOD) == 0u;
    if (far_field) {
        out->lod = render_lod_plan_by_bands(&bands, config->render_distance);
    }
    out->fog_end = game_view_fog_end(far_field ? config->render_distance : out->block_radius);
    const uint32_t optional = config->god_rays != 0u ? (uint32_t)SHADE_FEATURE_GOD_RAYS : 0u;
    out->features = (SHADE_FEATURES_PLAY | optional) & ~config->features_off;
}

float32_t game_view_half_fov_tangent(uint32_t fov_degrees) {
    if (fov_degrees == 0u || fov_degrees == GAME_VIEW_FOV_DEFAULT) {
        return RENDER_CAMERA_HALF_FOV_TANGENT_DEFAULT;
    }
    return tanf((float32_t)fov_degrees * GAME_VIEW_DEGREES_TO_HALF_RADIANS);
}

int64_t game_view_look_scaled(int64_t counts, uint32_t percent, float64_t *rest) {
    if (percent == 0u || percent == GAME_VIEW_PERCENT) {
        return counts;
    }
    const float64_t wanted = (float64_t)counts * (float64_t)percent / (float64_t)GAME_VIEW_PERCENT + *rest;
    const float64_t whole = trunc(wanted);
    *rest = wanted - whole;
    return (int64_t)whole;
}

void game_view_bring_up(const uint8_t *from, uint32_t from_width, uint32_t from_height, uint8_t *to, uint32_t to_width,
                        uint32_t to_height) {
    const uint64_t from_row_bytes = (uint64_t)from_width * GAME_VIEW_BYTES_PER_PIXEL;
    const uint64_t to_row_bytes = (uint64_t)to_width * GAME_VIEW_BYTES_PER_PIXEL;
    uint32_t last_row = UINT32_MAX;
    for (uint32_t y = 0u; y < to_height; y++) {
        const uint32_t row = (uint32_t)((uint64_t)y * from_height / to_height);
        uint8_t *const out = to + (uint64_t)y * to_row_bytes;
        if (row == last_row) {
            memcpy(out, out - to_row_bytes, to_row_bytes);
            continue;
        }
        last_row = row;
        const uint32_t *const source = (const uint32_t *)(const void *)(from + (uint64_t)row * from_row_bytes);
        uint32_t *const target = (uint32_t *)(void *)out;
        for (uint32_t x = 0u; x < to_width; x++) {
            target[x] = source[(uint64_t)x * from_width / to_width];
        }
    }
}
