/* bench_world_slope: plan 009 phase 01, where a picture's steps go, at Ahmed's spot of ~/Pictures/5.png. See README.md. */
#include "calendar/index.h"
#include "climate/sun/index.h"
#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "game/watch/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "log/index.h"
#include "main/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SL_PI 3.14159265358979f64
#define SL_DEGREES (SL_PI / 180.0f64)
#define SL_EYE_ABOVE_FEET 1.62f64 /* --at is the block the feet are in, as F3 shows it */
/* 5.png's view, fitted by eye to the picture over five shots (Q2): east, a little north, up the slope and 34 degrees up. */
#define SL_LOOK_YAW_DEG 8.0f64
#define SL_LOOK_PITCH_DEG 34.0f64
#define SL_SUN_NIGHT_DEG (-13.1f64) /* F3 of 5.png: "sun -13.1", so the light is the moon */
#define SL_SUN_DAY_DEG 40.0f64
#define SL_HEAT_MOST 512.0f64 /* the heatmap's top, in steps a pixel a frame, on a log scale */

enum : uint32_t {
    SL_WIDTH = 1920u,
    SL_HEIGHT = 1080u,
    SL_FOV_DEGREES = 70u, /* his settings.yaml */
    SL_WARM_FRAMES = 30u,
    SL_MEASURED_FRAMES = 240u,
    /* The pictures --steps counts, after the measured ones: two of AO's four-frame turns, so its rays are all in. */
    SL_COUNTED_FRAMES = 8u,
    SL_ROWS = 3u,
    SL_PATH_BYTES = 512u,
    SL_DAY_OF_YEAR = 92u, /* 5.png: "Year 1 - Day 92" */
    SL_LEGEND_HEIGHT = 48u,
    SL_DISTINCT_WANTED = 8u,
    SL_VOXELS_MIB = 3456u,
    SL_SUN_SEARCH_STEPS = 48u,
};
static_assert(MC_GPU_BUDGET_TEXTURES_MIB + MC_GPU_BUDGET_FRAMES_MIB + SL_VOXELS_MIB < 4096u);
/* Level 0 held past 32 chunks (--block-radius 48, the reference) does not fit in that: at 48 the table is 97 columns wide and its pool about
 * 5.9 GiB. Nor does the pyramid's far field beside it at render distance 64 (plan 009 phase 03). Those rows take the game's own voxel budget,
 * as Ahmed's game would (config/machine.yaml). */
enum : uint32_t { SL_VOXELS_WIDE_MIB = MC_GPU_BUDGET_VOXELS_MIB, SL_BLOCK_RADIUS_NARROW = 32u };
static const uint64_t SL_SEED_DEFAULT =
    14148492376765118422ull; /* saves/test_7/world.meta: the world he played at 5.png's time */
static const int32_t SL_AT_DEFAULT[3] = {-385, 347, 2}; /* F3 of 5.png */

/* One row: a render distance and a sun. */
typedef struct sl_row_t {
    const char *name;         /* 0: after "ahmed_slope/" */
    float64_t sun_deg;        /* 8: the sun's elevation, as F3 says it */
    uint32_t render_distance; /* 16 */
    uint8_t _pad0[4];         /* 20 */
} sl_row_t;
static_assert(sizeof(sl_row_t) == 24);

static const sl_row_t SL_ROW_LIST[SL_ROWS] = {
    {"night-64", SL_SUN_NIGHT_DEG, 64u, {0}},
    {"night-32", SL_SUN_NIGHT_DEG, 32u, {0}},
    {"day-64", SL_SUN_DAY_DEG, 64u, {0}},
};

/* What a row came to. */
typedef struct sl_result_t {
    float64_t card_mean_ms;                      /* 0 */
    float64_t card_p50_ms;                       /* 8 */
    float64_t card_p99_ms;                       /* 16 */
    float64_t card_mcycles;                      /* 24: ms x MHz / 1000, mean over pictures with a reading */
    float64_t card_mhz;                          /* 32: mean */
    float64_t card_mhz_low;                      /* 40 */
    float64_t card_mhz_high;                     /* 48 */
    float64_t pass_ms[FRAME_PASS_COUNT];         /* 56 */
    float64_t lod_ms;                            /* 96 */
    float64_t shade_ms[SHADE_SPAN_COUNT];        /* 104 */
    float64_t fresh_mean;                        /* 144 */
    float64_t sun_set_deg;                       /* 152: the elevation the clock was set to give */
    float64_t steps_frame[TRAVERSE_COUNT_KINDS]; /* 160: with --steps: each kind's steps a frame */
    float64_t calls_frame[TRAVERSE_COUNT_KINDS]; /* 224 */
    float64_t primary_per_pixel;                 /* 288: primary steps a pixel a frame */
    float64_t seconds;                           /* 296 */
    uint64_t picture_hash;                       /* 304 */
    uint64_t vram_voxels;                        /* 312: bytes live in the voxel pool */
    uint32_t block_radius;                       /* 320: level 0's, as the game worked it out or as asked */
    uint32_t render_distance;                    /* 324 */
    uint32_t width;                              /* 328: what was drawn */
    uint32_t height;                             /* 332 */
    uint8_t ran;                                 /* 336 */
    uint8_t sound;                               /* 337 */
    uint8_t counted;                             /* 338 */
    uint8_t _pad0[5];                            /* 339 */
} sl_result_t;
static_assert(sizeof(sl_result_t) == 344);

typedef struct sl_options_t {
    const char *only;      /* 0 */
    const char *record;    /* 8 */
    const char *label;     /* 16 */
    const char *shots;     /* 24 */
    const char *steps;     /* 32: the band table's path; <path>.png and the others beside it */
    uint64_t seed;         /* 40 */
    float64_t yaw_deg;     /* 48 */
    float64_t pitch_deg;   /* 56 */
    float64_t sun_deg;     /* 64: with has_sun, in place of the row's */
    int32_t at[3];         /* 72 */
    uint32_t features_off; /* 84 */
    uint32_t block_radius; /* 88: 0 for the game's own (plan 009 phase 04) */
    uint32_t render_scale; /* 92: percent, 0 for 100 */
    uint8_t has_sun;       /* 96 */
    uint8_t lod_reference; /* 97 */
    uint8_t list;          /* 98 */
    uint8_t _pad0[5];      /* 99 */
} sl_options_t;
static_assert(sizeof(sl_options_t) == 104);

/* One measured picture. */
typedef struct sl_sample_t {
    float32_t card_ms;                    /* 0 */
    float32_t mhz;                        /* 4 */
    float32_t pass_ms[FRAME_PASS_COUNT];  /* 8 */
    float32_t lod_ms;                     /* 28 */
    float32_t shade_ms[SHADE_SPAN_COUNT]; /* 32 */
    uint32_t fresh;                       /* 52 */
} sl_sample_t;
static_assert(sizeof(sl_sample_t) == 56);

typedef struct sl_t {
    heap_region_t owned_memory;    /* 0 */
    app_t *borrowed_app;           /* 16 */
    sl_sample_t *borrowed_samples; /* 24: SL_MEASURED_FRAMES */
    float32_t *borrowed_sort;      /* 32: SL_MEASURED_FRAMES */
    uint8_t *borrowed_picture;     /* 40 */
    uint8_t *borrowed_png;         /* 48 */
    uint8_t *borrowed_counts;      /* 56: traverse_counts_bytes of the whole picture, read back from the card */
    uint8_t *borrowed_heat;        /* 64: the heatmap, XRGB, the picture and its legend */
    uint64_t png_bytes;            /* 72 */
    uint32_t started;              /* 80 */
    uint8_t _pad0[4];              /* 84 */
} sl_t;
static_assert(sizeof(sl_t) == 88);

/* ---- Starting and stopping, as bench_world does ------------------------------------------------------------------- */

static bool sl_start(sl_t *sl, uint64_t seed, uint32_t voxels_mib) {
    app_t *const app = sl->borrowed_app;
    if (!math_init(&app->math)) {
        return false;
    }
    sl->started = 1u;
    if (!simd_init(&app->simd)) {
        return false;
    }
    sl->started = 2u;
    if (!texture_init(&app->texture)) {
        return false;
    }
    sl->started = 3u;
    if (!thread_topology_read(&app->topology) || !job_init(&app->job, &app->topology)) {
        return false;
    }
    sl->started = 4u;
    gpu_bridge_limits_t limits = gpu_bridge_limits_default();
    limits.device.pool_bytes[DEVICE_POOL_VOXELS] = (uint64_t)voxels_mib << 20;
    if (gpu_bridge_init(&app->gpu_bridge, limits) != GPU_STATUS_OK) {
        return false;
    }
    sl->started = 5u;
    if (!thread_pin_self(thread_cpu_for(&app->topology, THREAD_ROLE_MAIN, 0u)) ||
        !world_init(&app->world, world_limits_default())) {
        return false;
    }
    sl->started = 6u;
    if (!terrain_init(&app->terrain, seed)) {
        return false;
    }
    sl->started = 7u;
    if (!render_init(&app->render, &app->gpu_bridge.device_memory, &app->job, render_limits_default())) {
        return false;
    }
    sl->started = 8u;
    if (!ui_init(&app->ui, &app->texture)) {
        return false;
    }
    sl->started = 9u;
    return true;
}

static void sl_stop(sl_t *sl) {
    app_t *const app = sl->borrowed_app;
    const uint32_t started = sl->started;
    game_deinit(&app->game);
    if (started >= 9u) {
        ui_deinit(&app->ui);
    }
    if (started >= 8u) {
        render_deinit(&app->render);
    }
    if (started >= 7u) {
        terrain_deinit(&app->terrain);
    }
    if (started >= 6u) {
        world_deinit(&app->world);
    }
    if (started >= 5u) {
        gpu_bridge_deinit(&app->gpu_bridge);
    }
    if (started >= 4u) {
        job_deinit(&app->job);
    }
    if (started >= 3u) {
        texture_deinit(&app->texture);
    }
    if (started >= 2u) {
        simd_deinit(&app->simd);
    }
    if (started >= 1u) {
        math_deinit(&app->math);
    }
}

/* ---- Numbers ------------------------------------------------------------------------------------------------------- */

static int32_t sl_compare(const void *left, const void *right) {
    const float32_t a = *(const float32_t *)left, b = *(const float32_t *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

static uint64_t sl_hash(const uint8_t *bytes, uint64_t count) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint64_t i = 0u; i < count; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

/* At least SL_DISTINCT_WANTED colours on a grid of 64 by 36, as bench_world asks: an eye in a wall is very fast. */
static bool sl_picture_has_things(const uint8_t *picture) {
    uint32_t seen[SL_DISTINCT_WANTED];
    uint32_t count = 0u;
    for (uint32_t row = 0u; row < 36u && count < SL_DISTINCT_WANTED; row++) {
        const uint32_t y = (row * 2u + 1u) * SL_HEIGHT / 72u;
        for (uint32_t across = 0u; across < 64u && count < SL_DISTINCT_WANTED; across++) {
            const uint32_t x = (across * 2u + 1u) * SL_WIDTH / 128u;
            uint32_t pixel = 0u;
            memcpy(&pixel, picture + ((uint64_t)y * SL_WIDTH + x) * 4u, sizeof(pixel));
            pixel &= 0x00FFFFFFu;
            bool known = false;
            for (uint32_t i = 0u; i < count; i++) {
                known = known || seen[i] == pixel;
            }
            if (!known) {
                seen[count++] = pixel;
            }
        }
    }
    return count >= SL_DISTINCT_WANTED;
}

/* The size a render scale draws at, as game_view_work_out works it out. */
static uint32_t sl_scaled(uint32_t size, uint32_t percent) {
    if (percent == 0u || percent >= 100u) {
        return size;
    }
    const uint32_t scaled = (uint32_t)((uint64_t)size * percent / 100u);
    return scaled != 0u ? scaled : 1u;
}

/* The world clock's whole days that show as SL_DAY_OF_YEAR from 06:00 on, as the corner's date line counts them. */
static uint64_t sl_clock_days(uint32_t day_zero) {
    return ((uint64_t)SL_DAY_OF_YEAR - 1u + CALENDAR_DAYS_PER_YEAR - day_zero) % CALENDAR_DAYS_PER_YEAR;
}

/* The day's fraction on SL_DAY_OF_YEAR that puts the sun at elevation_deg at this y, as the world clock and the eye's latitude give it:
 * the morning for a sun above the horizon, the evening for one below, where the height only rises or only falls. Both are past
 * 06:00, so the date line says SL_DAY_OF_YEAR. */
static float64_t sl_day_fraction_for(uint32_t day_zero, float64_t y, float64_t elevation_deg) {
    const float64_t wanted = sin(elevation_deg * SL_DEGREES);
    const bool rising = elevation_deg >= 0.0f64;
    float64_t low = rising ? 0.25f64 : 0.5f64, high = rising ? 0.5f64 : 1.0f64;
    for (uint32_t i = 0u; i < SL_SUN_SEARCH_STEPS; i++) {
        const float64_t middle = 0.5f64 * (low + high);
        const calendar_clock_t clock = sl_clock_days(day_zero) << 32 | (uint64_t)(uint32_t)(middle * 4294967296.0f64);
        const float64_t height = climate_sun_height_of_clock(day_zero, clock, y);
        if ((height < wanted) == rising) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return 0.5f64 * (low + high);
}

/* ---- The heatmap ---------------------------------------------------------------------------------------------------- */

/* Dark blue to blue, cyan, yellow, red and white, at t from 0 to 1. */
static uint32_t sl_heat_colour(float64_t t) {
    static const uint8_t stops[6][3] = {{8, 8, 40},     {20, 60, 200}, {20, 200, 220},
                                        {240, 230, 40}, {230, 40, 20}, {255, 255, 255}};
    t = t < 0.0f64 ? 0.0f64 : (t > 1.0f64 ? 1.0f64 : t);
    const float64_t at = t * 5.0f64;
    const uint32_t i = at >= 5.0f64 ? 4u : (uint32_t)at;
    const float64_t f = at - (float64_t)i;
    uint32_t rgb = 0u;
    for (uint32_t c = 0u; c < 3u; c++) {
        const float64_t v = (float64_t)stops[i][c] + ((float64_t)stops[i + 1u][c] - (float64_t)stops[i][c]) * f;
        rgb |= (uint32_t)(v + 0.5f64) << (16u - 8u * c);
    }
    return rgb;
}

/* Steps a pixel a frame on the scale: log, 0 at 0 and 1 at SL_HEAT_MOST. */
static float64_t sl_heat_t(float64_t steps) {
    return log2(1.0f64 + steps) / log2(1.0f64 + SL_HEAT_MOST);
}

/* Three by five digits, a row a byte, for the legend's numbers. */
static const uint8_t SL_DIGITS[10][5] = {{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
                                         {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
                                         {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}};

static void sl_heat_number(uint32_t *pixels, uint32_t width, uint32_t x, uint32_t y, uint32_t value) {
    char text[12];
    const int32_t length = snprintf(text, sizeof(text), "%" PRIu32, value);
    const uint32_t scale = 3u;
    for (int32_t k = 0; k < length; k++) {
        const uint8_t *const digit = SL_DIGITS[text[k] - '0'];
        for (uint32_t row = 0u; row < 5u; row++) {
            for (uint32_t column = 0u; column < 3u; column++) {
                if (((digit[row] >> (2u - column)) & 1u) == 0u) {
                    continue;
                }
                for (uint32_t dy = 0u; dy < scale; dy++) {
                    for (uint32_t dx = 0u; dx < scale; dx++) {
                        const uint32_t px = x + ((uint32_t)k * 4u + column) * scale + dx;
                        const uint32_t py = y + row * scale + dy;
                        if (px < width) {
                            pixels[(uint64_t)py * width + px] = 0x00FFFFFFu;
                        }
                    }
                }
            }
        }
    }
}

/* One plane of steps (or the sum of planes from first to last), divided by frames, drawn to path with a legend strip under it: the
 * scale with ticks at 0, 1, 4, 16, 64, 256 and 512 steps a pixel a frame. */
static bool sl_heat_write(sl_t *sl, const uint16_t *planes, uint32_t pixels, uint32_t width, uint32_t height,
                          uint32_t first, uint32_t last, const char *path) {
    uint32_t *const out = (uint32_t *)(void *)sl->borrowed_heat;
    const uint32_t full = height + SL_LEGEND_HEIGHT;
    for (uint32_t i = 0u; i < pixels; i++) {
        uint32_t sum = 0u;
        for (uint32_t p = first; p <= last; p++) {
            sum += planes[(uint64_t)p * pixels + i];
        }
        out[i] = sl_heat_colour(sl_heat_t((float64_t)sum / (float64_t)SL_COUNTED_FRAMES));
    }
    memset(out + (uint64_t)width * height, 0, (uint64_t)width * SL_LEGEND_HEIGHT * 4u);
    const uint32_t margin = 16u, bar = 16u;
    const uint32_t span = width > 2u * margin ? width - 2u * margin : 1u;
    for (uint32_t x = 0u; x < span; x++) {
        const uint32_t colour = sl_heat_colour((float64_t)x / (float64_t)(span - 1u));
        for (uint32_t y = 0u; y < bar; y++) {
            out[(uint64_t)(height + 4u + y) * width + margin + x] = colour;
        }
    }
    static const uint32_t ticks[] = {0u, 1u, 4u, 16u, 64u, 256u, 512u};
    for (uint32_t k = 0u; k < sizeof(ticks) / sizeof(ticks[0]); k++) {
        const uint32_t x = margin + (uint32_t)(sl_heat_t((float64_t)ticks[k]) * (float64_t)(span - 1u));
        for (uint32_t y = 0u; y < 6u; y++) {
            out[(uint64_t)(height + 4u + bar + y) * width + x] = 0x00FFFFFFu;
        }
        const uint32_t label_x = x > 12u ? (x + 30u < width ? x - 4u : width - 46u) : x;
        sl_heat_number(out, width, label_x, height + 4u + bar + 8u, ticks[k]);
    }
    uint64_t bytes = 0u;
    if (!display_png_encode((const uint8_t *)out, width * 4u, width, full, sl->borrowed_png, sl->png_bytes, &bytes) ||
        file_write_atomic(path, sl->borrowed_png, bytes) != FILE_OK) {
        LOG_WARN("bench_world_slope: could not write %s", path);
        return false;
    }
    return true;
}

/* ---- The band table ------------------------------------------------------------------------------------------------- */

static const char *const SL_KIND_NAMES[TRAVERSE_COUNT_KINDS] = {"primary", "far",      "sun",     "ao",
                                                                "alpha",   "far_past", "far_sun", "reflect"};

/* The least count of steps a share q of the calls in a histogram row took at most. */
static uint32_t sl_quantile(const uint32_t *row, float64_t q) {
    uint64_t total = 0u;
    for (uint32_t k = 0u; k < TRAVERSE_COUNT_BUCKETS; k++) {
        total += row[k];
    }
    if (total == 0u) {
        return 0u;
    }
    const uint64_t wanted = (uint64_t)ceil(q * (float64_t)total);
    uint64_t seen = 0u;
    for (uint32_t k = 0u; k < TRAVERSE_COUNT_BUCKETS; k++) {
        seen += row[k];
        if (seen >= wanted) {
            return traverse_count_bucket_low(k);
        }
    }
    return traverse_count_bucket_low(TRAVERSE_COUNT_BUCKETS - 1u);
}

static void sl_band_line(FILE *file, const char *kind, const char *band, uint64_t calls, uint64_t steps,
                         const uint32_t *histogram) {
    const float64_t frames = (float64_t)SL_COUNTED_FRAMES;
    fprintf(file, "%s\t%s\t%.0f\t%.0f\t%.2f\t%" PRIu32 "\t%" PRIu32 "\n", kind, band,
            (libc_double_t)((float64_t)calls / frames), (libc_double_t)((float64_t)steps / frames),
            (libc_double_t)(calls != 0u ? (float64_t)steps / (float64_t)calls : 0.0f64),
            histogram != nullptr ? sl_quantile(histogram, 0.5f64) : 0u,
            histogram != nullptr ? sl_quantile(histogram, 0.99f64) : 0u);
}

/* The table: per kind, per band, the calls with a step in it and their steps, a frame each, the mean, p50 and p99 of a call's steps in
 * it; a row "all" of whole calls; and the far kinds by level. */
static bool sl_steps_write(const traverse_counts_t *c, const char *path) {
    FILE *const file = fopen(path, "w");
    if (file == nullptr) {
        LOG_ERROR("bench_world_slope: could not write %s", path);
        return false;
    }
    fprintf(file, "kind\tband\tcalls_a_frame\tsteps_a_frame\tmean\tp50\tp99\n");
    for (uint32_t kind = 0u; kind < TRAVERSE_COUNT_KINDS; kind++) {
        uint64_t steps = 0u;
        for (uint32_t b = 0u; b < TRAVERSE_COUNT_BANDS; b++) {
            char band[24];
            if (b + 1u < TRAVERSE_COUNT_BANDS) {
                (void)snprintf(band, sizeof(band), "%" PRIu32 "-%" PRIu32, traverse_count_band_low(b),
                               traverse_count_band_low(b + 1u));
            } else {
                (void)snprintf(band, sizeof(band), "%" PRIu32 "+", traverse_count_band_low(b));
            }
            sl_band_line(file, SL_KIND_NAMES[kind], band, c->rays[kind][b], c->steps[kind][b], c->histogram[kind][b]);
            steps += c->steps[kind][b];
        }
        sl_band_line(file, SL_KIND_NAMES[kind], "all", c->calls[kind], steps, c->histogram[kind][TRAVERSE_COUNT_BANDS]);
        for (uint32_t level = 0u; level < TRAVERSE_COUNT_LEVELS; level++) {
            if (c->level_calls[kind][level] == 0u) {
                continue;
            }
            char band[24];
            (void)snprintf(band, sizeof(band), "level%" PRIu32, level);
            sl_band_line(file, SL_KIND_NAMES[kind], band, c->level_calls[kind][level], c->level_steps[kind][level],
                         nullptr);
        }
    }
    const bool closed = fclose(file) == 0;
    return closed;
}

/* The heatmaps beside the table: <path>.png primary, and the far field, sun rays, the rest of shade, and all of it. */
static bool sl_heat_all(sl_t *sl, const uint16_t *planes, uint32_t width, uint32_t height, const char *path) {
    static const char *const suffixes[] = {"", ".far", ".sun", ".shade", ".all"};
    static const uint32_t firsts[] = {0u, 1u, 2u, 3u, 0u};
    static const uint32_t lasts[] = {0u, 1u, 2u, 3u, TRAVERSE_COUNT_PLANES - 1u};
    bool ok = true;
    for (uint32_t i = 0u; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        char name[SL_PATH_BYTES];
        (void)snprintf(name, sizeof(name), "%s%s.png", path, suffixes[i]);
        ok = sl_heat_write(sl, planes, width * height, width, height, firsts[i], lasts[i], name) && ok;
    }
    return ok;
}

/* ---- A row --------------------------------------------------------------------------------------------------------- */

/* One settled picture, as the game draws one with no threads: a tick, then the draw. With clock, the card's speed is read first. */
static bool sl_draw(sl_t *sl, const display_headless_scene_t *scene, uint32_t frame, bool clock) {
    if (clock) {
        game_watch_of(&sl->borrowed_app->game)->clock_asked_ns = 0;
    }
    display_buffer_t target = {.owned_map = sl->borrowed_picture,
                               .map_bytes = (uint64_t)SL_WIDTH * SL_HEIGHT * 4u,
                               .pitch = SL_WIDTH * 4u,
                               .width = SL_WIDTH,
                               .height = SL_HEIGHT};
    display_script_state_t state;
    memset(&state, 0, sizeof(state));
    state.tick = frame;
    display_headless_tick_t tick = {.events = nullptr, .state = &state, .event_count = 0u, .tick = frame};
    return scene->tick(scene->borrowed_context, &tick) && scene->draw(scene->borrowed_context, &tick, &target);
}

static void sl_sample(const sl_t *sl, sl_sample_t *out) {
    const app_t *const app = sl->borrowed_app;
    const frame_timings_t *const timings = &app->render.frame.last_timings;
    const frame_marks_t *const marks = game_watch_of(&app->game)->borrowed_marks;
    memset(out, 0, sizeof(*out));
    out->card_ms = timings->total_ms;
    for (uint32_t k = 0u; k < FRAME_PASS_COUNT; k++) {
        out->pass_ms[k] = timings->pass_ms[k];
    }
    out->lod_ms = timings->kernel_ms[FRAME_KERNEL_LOD];
    if (marks != nullptr) {
        for (uint32_t k = 0u; k < SHADE_SPAN_COUNT; k++) {
            out->shade_ms[k] = marks->shade_ms[k];
        }
        out->fresh = marks->fresh_count;
        out->mhz = marks->clock_read != 0u ? marks->card_mhz : 0.0f32;
    }
}

static void sl_sum(sl_t *sl, sl_result_t *r) {
    const sl_sample_t *const s = sl->borrowed_samples;
    float64_t card = 0.0f64, mhz = 0.0f64, cycles = 0.0f64, fresh = 0.0f64, lod = 0.0f64;
    uint32_t clocked = 0u;
    for (uint32_t i = 0u; i < SL_MEASURED_FRAMES; i++) {
        card += (float64_t)s[i].card_ms;
        lod += (float64_t)s[i].lod_ms;
        fresh += (float64_t)s[i].fresh;
        sl->borrowed_sort[i] = s[i].card_ms;
        for (uint32_t k = 0u; k < FRAME_PASS_COUNT; k++) {
            r->pass_ms[k] += (float64_t)s[i].pass_ms[k] / (float64_t)SL_MEASURED_FRAMES;
        }
        for (uint32_t k = 0u; k < SHADE_SPAN_COUNT; k++) {
            r->shade_ms[k] += (float64_t)s[i].shade_ms[k] / (float64_t)SL_MEASURED_FRAMES;
        }
        if (s[i].mhz > 0.0f32) {
            const float64_t m = (float64_t)s[i].mhz;
            mhz += m;
            cycles += (float64_t)s[i].card_ms * m / 1000.0f64;
            r->card_mhz_low = clocked == 0u || m < r->card_mhz_low ? m : r->card_mhz_low;
            r->card_mhz_high = m > r->card_mhz_high ? m : r->card_mhz_high;
            clocked++;
        }
    }
    qsort(sl->borrowed_sort, SL_MEASURED_FRAMES, sizeof(float32_t), sl_compare);
    r->card_mean_ms = card / (float64_t)SL_MEASURED_FRAMES;
    r->card_p50_ms = (float64_t)sl->borrowed_sort[SL_MEASURED_FRAMES / 2u];
    r->card_p99_ms = (float64_t)sl->borrowed_sort[SL_MEASURED_FRAMES * 99u / 100u];
    r->lod_ms = lod / (float64_t)SL_MEASURED_FRAMES;
    r->fresh_mean = fresh / (float64_t)SL_MEASURED_FRAMES;
    r->card_mhz = clocked != 0u ? mhz / (float64_t)clocked : 0.0f64;
    r->card_mcycles = clocked != 0u ? cycles / (float64_t)clocked : 0.0f64;
}

/* SL_COUNTED_FRAMES more pictures at the same place with a counts block on the card, read back, and written as the table and heatmaps.
 * Counting is after the measured pictures, so it never touches their times. */
static bool sl_count(sl_t *sl, const display_headless_scene_t *scene, const sl_options_t *options, sl_result_t *r) {
    app_t *const app = sl->borrowed_app;
    device_memory_t *const memory = &app->gpu_bridge.device_memory;
    frame_marks_t *const marks = game_watch_of(&app->game)->borrowed_marks;
    if (marks == nullptr) {
        LOG_ERROR("bench_world_slope: the frame has no marks to count with");
        return false;
    }
    const uint32_t pixels = r->width * r->height;
    const uint64_t bytes = traverse_counts_bytes(pixels);
    device_block_t block = {0};
    bool ok = device_memory_alloc(memory, DEVICE_POOL_FRAMES, bytes, "bench counts", &block) == GPU_STATUS_OK &&
              device_memory_zero(memory, &block, 0u, bytes) == GPU_STATUS_OK &&
              device_memory_write(memory, &block, offsetof(traverse_counts_t, pixels), &pixels, sizeof(pixels)) ==
                  GPU_STATUS_OK;
    if (ok) {
        marks->borrowed_counts = &block;
        for (uint32_t frame = 0u; frame < SL_COUNTED_FRAMES && ok; frame++) {
            ok = sl_draw(sl, scene, SL_WARM_FRAMES + SL_MEASURED_FRAMES + frame, false);
        }
        marks->borrowed_counts = nullptr;
    }
    ok = ok && device_memory_read(memory, &block, 0u, sl->borrowed_counts, bytes) == GPU_STATUS_OK;
    if (block.owned_address != 0u) {
        ok = device_memory_free(memory, &block) == GPU_STATUS_OK && ok;
    }
    if (!ok) {
        LOG_ERROR("bench_world_slope: counting steps failed");
        return false;
    }
    const traverse_counts_t *const c = (const traverse_counts_t *)(const void *)sl->borrowed_counts;
    for (uint32_t kind = 0u; kind < TRAVERSE_COUNT_KINDS; kind++) {
        uint64_t steps = 0u;
        for (uint32_t b = 0u; b < TRAVERSE_COUNT_BANDS; b++) {
            steps += c->steps[kind][b];
        }
        r->steps_frame[kind] = (float64_t)steps / (float64_t)SL_COUNTED_FRAMES;
        r->calls_frame[kind] = (float64_t)c->calls[kind] / (float64_t)SL_COUNTED_FRAMES;
    }
    r->primary_per_pixel = r->steps_frame[TRAVERSE_COUNT_PRIMARY] / (float64_t)pixels;
    r->counted = 1u;
    const uint16_t *const planes = (const uint16_t *)(const void *)(sl->borrowed_counts + sizeof(traverse_counts_t));
    return sl_steps_write(c, options->steps) && sl_heat_all(sl, planes, r->width, r->height, options->steps);
}

static bool sl_row(sl_t *sl, const sl_options_t *options, sl_result_t *r) {
    game_t *const game = &sl->borrowed_app->game;
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    const int64_t started_ns = time_now_ns();
    for (uint32_t frame = 0u; frame < SL_WARM_FRAMES + SL_MEASURED_FRAMES; frame++) {
        if (!sl_draw(sl, &scene, frame, frame >= SL_WARM_FRAMES)) {
            return false;
        }
        if (frame >= SL_WARM_FRAMES) {
            sl_sample(sl, &sl->borrowed_samples[frame - SL_WARM_FRAMES]);
        }
    }
    r->seconds = time_ns_to_ms(time_now_ns() - started_ns) / 1000.0f64;
    sl_sum(sl, r);
    /* The measured row's last picture is what its hash and shot say: the counted ones after it are the same work, not the same frame. */
    r->picture_hash = sl_hash(sl->borrowed_picture, (uint64_t)SL_WIDTH * SL_HEIGHT * 4u);
    r->sound = sl_picture_has_things(sl->borrowed_picture) ? 1u : 0u;
    return options->steps == nullptr || sl_count(sl, &scene, options, r);
}

static bool sl_png_write(sl_t *sl, const char *path) {
    uint64_t bytes = 0u;
    if (!display_png_encode(sl->borrowed_picture, SL_WIDTH * 4u, SL_WIDTH, SL_HEIGHT, sl->borrowed_png, sl->png_bytes,
                            &bytes) ||
        file_write_atomic(path, sl->borrowed_png, bytes) != FILE_OK) {
        LOG_WARN("bench_world_slope: could not write %s", path);
        return false;
    }
    return true;
}

/* Opens the world at the spot with the row's settings, settles it, and runs the row. */
static bool sl_run(sl_t *sl, const sl_row_t *row, const sl_options_t *options, sl_result_t *r) {
    app_t *const app = sl->borrowed_app;
    game_t *const game = &app->game;
    memset(r, 0, sizeof(*r));
    const float64_t eye[3] = {(float64_t)options->at[0] + 0.5f64, (float64_t)options->at[1] + 0.5f64,
                              (float64_t)options->at[2] + SL_EYE_ABOVE_FEET};
    game_init(game, &app->world, &app->terrain, &app->render, &app->job, &app->texture, &app->topology);
    game_ui_set(game, &app->ui);
    const uint32_t distance = row->render_distance;
    uint32_t block_radius = options->block_radius < distance ? options->block_radius : distance;
    uint32_t features_off = (uint32_t)SHADE_FEATURE_DAY_CYCLE | options->features_off;
    if (options->lod_reference != 0u) {
        /* Level 0 as far as the picture goes, and no far field: every block everywhere. */
        block_radius = distance;
        features_off |= (uint32_t)SHADE_FEATURE_LOD;
    }
    r->render_distance = distance;
    r->width = sl_scaled(SL_WIDTH, options->render_scale);
    r->height = sl_scaled(SL_HEIGHT, options->render_scale);
    const game_config_t config = {.render_distance = distance,
                                  .width = SL_WIDTH,
                                  .height = SL_HEIGHT,
                                  .features_off = features_off,
                                  .block_radius_given = block_radius,
                                  .table_chunks = block_radius,
                                  .fov_degrees = SL_FOV_DEGREES,
                                  .render_scale_percent = (uint8_t)options->render_scale};
    if (!game_open(game, &config)) {
        LOG_ERROR("bench_world_slope: the world would not open with level 0 at %" PRIu32 ", render distance %" PRIu32,
                  block_radius, distance);
        return false;
    }
    r->block_radius = game->view.block_radius;
    r->sun_set_deg = options->has_sun != 0u ? options->sun_deg : row->sun_deg;
    game_clock_set(game, sl_clock_days(game->day_zero), sl_day_fraction_for(game->day_zero, eye[1], r->sun_set_deg));
    render_camera_start(&game->camera, eye[0], eye[1], eye[2], (float32_t)(options->yaw_deg * SL_DEGREES),
                        (float32_t)(options->pitch_deg * SL_DEGREES));
    const render_fly_input_t still = {0};
    game_fly(game, &still, 0.0f32);
    bool ok = game_settle(game);
    ok = ok && sl_row(sl, options, r);
    if (ok) {
        r->vram_voxels = app->gpu_bridge.device_memory.pools[DEVICE_POOL_VOXELS].live_bytes;
        r->ran = 1u;
        if (options->shots != nullptr) {
            char path[SL_PATH_BYTES];
            (void)snprintf(path, sizeof(path), "%s/ahmed_slope-%s.png", options->shots, row->name);
            (void)sl_png_write(sl, path);
        }
    }
    game_report(game);
    game_close(game);
    return ok;
}

static void sl_row_print(const sl_row_t *row, const sl_result_t *r) {
    printf("ahmed_slope/%-9s %s  rd %" PRIu32 " r0 %" PRIu32
           " %ux%u sun %+.1f  card %.2f ms (p50 %.2f, p99 %.2f), %.2f "
           "Mcycles at %.0f MHz (%.0f-%.0f)\n",
           row->name, r->ran != 0u ? (r->sound != 0u ? "ok" : "NOT SOUND") : "FAILED", r->render_distance,
           r->block_radius, r->width, r->height, (libc_double_t)r->sun_set_deg, (libc_double_t)r->card_mean_ms,
           (libc_double_t)r->card_p50_ms, (libc_double_t)r->card_p99_ms, (libc_double_t)r->card_mcycles,
           (libc_double_t)r->card_mhz, (libc_double_t)r->card_mhz_low, (libc_double_t)r->card_mhz_high);
    printf(
        "    coarse %.2f  primary %.2f (far %.2f)  shade %.2f (water %.2f surface %.2f sun %.2f ao %.2f fresh %.2f)  "
        "post %.2f  readback %.2f  fresh %.0f  voxels %.0f MiB  picture %016" PRIX64 "\n",
        (libc_double_t)r->pass_ms[FRAME_PASS_COARSE], (libc_double_t)r->pass_ms[FRAME_PASS_PRIMARY],
        (libc_double_t)r->lod_ms, (libc_double_t)r->pass_ms[FRAME_PASS_SHADE],
        (libc_double_t)r->shade_ms[SHADE_SPAN_WATER], (libc_double_t)r->shade_ms[SHADE_SPAN_SURFACES],
        (libc_double_t)r->shade_ms[SHADE_SPAN_SUN], (libc_double_t)r->shade_ms[SHADE_SPAN_AO],
        (libc_double_t)r->shade_ms[SHADE_SPAN_FRESH], (libc_double_t)r->pass_ms[FRAME_PASS_POST],
        (libc_double_t)r->pass_ms[FRAME_PASS_READBACK], (libc_double_t)r->fresh_mean,
        (libc_double_t)((float64_t)r->vram_voxels / 1048576.0f64), r->picture_hash);
    if (r->counted != 0u) {
        printf("    steps a frame:");
        for (uint32_t kind = 0u; kind < TRAVERSE_COUNT_KINDS; kind++) {
            printf(" %s %.1fM", SL_KIND_NAMES[kind], (libc_double_t)(r->steps_frame[kind] / 1e6f64));
        }
        printf("; primary %.1f a pixel\n", (libc_double_t)r->primary_per_pixel);
    }
}

/* ---- The history --------------------------------------------------------------------------------------------------- */

static const char *sl_preset_name(void) {
#if defined(MC_PROFILE)
    return "profile";
#elif defined(NDEBUG)
    return "release";
#else
    return "debug";
#endif
}

static bool sl_record(const sl_result_t *results, const sl_options_t *options) {
    FILE *const file = fopen(options->record, "a");
    if (file == nullptr) {
        LOG_ERROR("bench_world_slope: could not open %s to add to it", options->record);
        return false;
    }
    (void)fseek(file, 0, SEEK_END);
    if (ftell(file) == 0) {
        fprintf(file,
                "date\tlabel\tpreset\trow\tseed\tat_x\tat_y\tat_z\tyaw_deg\tpitch_deg\tsun_deg\trender_distance\t"
                "block_radius\twidth\theight\tfeatures_off\tlod_reference\tcard_mean_ms\tcard_p50_ms\tcard_p99_ms\t"
                "card_mcycles\tcard_mhz\tcard_mhz_low\tcard_mhz_high\tcoarse_ms\tprimary_ms\tlod_ms\tshade_ms\t"
                "water_ms\tsurface_ms\tsun_ms\tao_ms\tao_fresh_ms\tpost_ms\treadback_ms\tfresh_mean\tvoxels_mib\t"
                "picture\tsound\tprimary_steps_a_pixel\tprimary_steps\tfar_steps\tsun_steps\tao_steps\t"
                "alpha_steps\tfar_past_steps\tfar_sun_steps\treflect_steps\n");
    }
    char date[32] = "";
    const time_t now = time(nullptr);
    struct tm parts;
    if (gmtime_r(&now, &parts) != nullptr) {
        (void)strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &parts);
    }
    for (uint32_t i = 0u; i < SL_ROWS; i++) {
        const sl_result_t *const r = &results[i];
        if (r->ran == 0u) {
            continue;
        }
        fprintf(file,
                "%s\t%s\t%s\tahmed_slope/%s\t%" PRIX64 "\t%" PRId32 "\t%" PRId32 "\t%" PRId32
                "\t%.1f\t%.1f\t%.1f\t%" PRIu32 "\t%" PRIu32 "\t%" PRIu32 "\t%" PRIu32 "\t%" PRIX32 "\t%u",
                date, options->label != nullptr ? options->label : "-", sl_preset_name(), SL_ROW_LIST[i].name,
                options->seed, options->at[0], options->at[1], options->at[2], (libc_double_t)options->yaw_deg,
                (libc_double_t)options->pitch_deg, (libc_double_t)r->sun_set_deg, r->render_distance, r->block_radius,
                r->width, r->height, options->features_off, (uint32_t)options->lod_reference);
        const float64_t numbers[] = {
            r->card_mean_ms,
            r->card_p50_ms,
            r->card_p99_ms,
            r->card_mcycles,
            r->card_mhz,
            r->card_mhz_low,
            r->card_mhz_high,
            r->pass_ms[FRAME_PASS_COARSE],
            r->pass_ms[FRAME_PASS_PRIMARY],
            r->lod_ms,
            r->pass_ms[FRAME_PASS_SHADE],
            r->shade_ms[SHADE_SPAN_WATER],
            r->shade_ms[SHADE_SPAN_SURFACES],
            r->shade_ms[SHADE_SPAN_SUN],
            r->shade_ms[SHADE_SPAN_AO],
            r->shade_ms[SHADE_SPAN_FRESH],
            r->pass_ms[FRAME_PASS_POST],
            r->pass_ms[FRAME_PASS_READBACK],
            r->fresh_mean,
            (float64_t)r->vram_voxels / 1048576.0f64,
        };
        for (uint32_t k = 0u; k < sizeof(numbers) / sizeof(numbers[0]); k++) {
            fprintf(file, "\t%.3f", (libc_double_t)numbers[k]);
        }
        fprintf(file, "\t%016" PRIX64 "\t%u\t%.2f", r->picture_hash, (uint32_t)r->sound,
                (libc_double_t)r->primary_per_pixel);
        for (uint32_t kind = 0u; kind < TRAVERSE_COUNT_KINDS; kind++) {
            fprintf(file, "\t%.0f", (libc_double_t)r->steps_frame[kind]);
        }
        fprintf(file, "\n");
    }
    const bool closed = fclose(file) == 0;
    if (!closed) {
        LOG_ERROR("bench_world_slope: could not finish writing %s", options->record);
    }
    return closed;
}

/* ---- Running it ---------------------------------------------------------------------------------------------------- */

static void sl_usage(const char *program) {
    fprintf(
        stderr,
        "usage: %s [--only <text>] [--at <x> <y> <z>] [--seed <n>] [--look <yaw> <pitch>] [--sun <degrees>]\n"
        "          [--block-radius <chunks>] [--render-scale <percent>] [--lod-reference] [--features-off <hex>]\n"
        "          [--steps <file.tsv>] [--record <file>] [--label <text>] [--shots <dir>] [--list]\n"
        "  --only <text>          the rows whose name holds this, or =<name>,<name>... (with or without ahmed_slope/)\n"
        "  --at <x> <y> <z>       the block the feet are in, as F3 says: -385 347 2 unless this\n"
        "  --seed <n>             the world's seed: saves/test_7's unless this\n"
        "  --look <yaw> <pitch>   degrees, 0 east and 90 north, pitch up: the fit to 5.png unless this\n"
        "  --sun <degrees>        the sun's elevation in place of the row's, on day 92, as the clock gives it here\n"
        "  --block-radius <chunks> level 0 held this far, in place of what the game works out from the bands\n"
        "  --render-scale <percent>  drawn at this share of 1920x1080 and brought up\n"
        "  --lod-reference        level 0 out to the render distance and no far field: every block everywhere\n"
        "  --features-off <hex>   take these SHADE_FEATURE_* bits away (shadow 20, alpha 2, AO 400, far field 8000)\n"
        "  --steps <file.tsv>     with one row: the band table, and <file>.png and its kin, the step heatmaps\n"
        "  --record <file>        add a line per row: bench/world/slope/history.tsv is the repository's\n"
        "  --shots <dir>          the last measured picture of each row, here\n"
        "  --list                 print the rows, and do nothing\n",
        program);
}

static bool sl_parse(int32_t argc, char **argv, sl_options_t *out) {
    memset(out, 0, sizeof(*out));
    out->seed = SL_SEED_DEFAULT;
    out->yaw_deg = SL_LOOK_YAW_DEG;
    out->pitch_deg = SL_LOOK_PITCH_DEG;
    memcpy(out->at, SL_AT_DEFAULT, sizeof(out->at));
    for (int32_t i = 1; i < argc; i++) {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--list") == 0) {
            out->list = 1u;
        } else if (strcmp(argv[i], "--lod-reference") == 0) {
            out->lod_reference = 1u;
        } else if (strcmp(argv[i], "--only") == 0 && has_value) {
            out->only = argv[++i];
        } else if (strcmp(argv[i], "--record") == 0 && has_value) {
            out->record = argv[++i];
        } else if (strcmp(argv[i], "--label") == 0 && has_value) {
            out->label = argv[++i];
        } else if (strcmp(argv[i], "--shots") == 0 && has_value) {
            out->shots = argv[++i];
        } else if (strcmp(argv[i], "--steps") == 0 && has_value) {
            out->steps = argv[++i];
        } else if (strcmp(argv[i], "--seed") == 0 && has_value) {
            out->seed = strtoull(argv[++i], nullptr, 0);
        } else if (strcmp(argv[i], "--sun") == 0 && has_value) {
            out->sun_deg = strtod(argv[++i], nullptr);
            out->has_sun = 1u;
            if (out->sun_deg < -89.0f64 || out->sun_deg > 89.0f64) {
                return false;
            }
        } else if (strcmp(argv[i], "--features-off") == 0 && has_value) {
            out->features_off = (uint32_t)strtoul(argv[++i], nullptr, 16);
        } else if (strcmp(argv[i], "--block-radius") == 0 && has_value) {
            const uint64_t chunks = strtoull(argv[++i], nullptr, 10);
            if (chunks < 2u || chunks > (uint64_t)MC_GAME_RENDER_DISTANCE_MAX) {
                return false;
            }
            out->block_radius = (uint32_t)chunks;
        } else if (strcmp(argv[i], "--render-scale") == 0 && has_value) {
            const uint64_t percent = strtoull(argv[++i], nullptr, 10);
            if (percent < 25u || percent > 100u) {
                return false;
            }
            out->render_scale = (uint32_t)percent;
        } else if (strcmp(argv[i], "--look") == 0 && i + 2 < argc) {
            out->yaw_deg = strtod(argv[++i], nullptr);
            out->pitch_deg = strtod(argv[++i], nullptr);
        } else if (strcmp(argv[i], "--at") == 0 && i + 3 < argc) {
            for (uint32_t k = 0u; k < 3u; k++) {
                out->at[k] = (int32_t)strtol(argv[++i], nullptr, 10);
            }
        } else {
            return false;
        }
    }
    return out->label == nullptr || strchr(out->label, '\t') == nullptr;
}

/* With =, the names after it, split by commas, each whole, with or without "ahmed_slope/". */
static bool sl_chosen(uint32_t row, const char *only) {
    const char *const name = SL_ROW_LIST[row].name;
    if (only != nullptr && only[0] == '=') {
        const uint64_t length = strlen(name);
        for (const char *at = only + 1; *at != '\0';) {
            if (strncmp(at, "ahmed_slope/", 12u) == 0) {
                at += 12;
            }
            const char *const comma = strchr(at, ',');
            const uint64_t token = comma != nullptr ? (uint64_t)(comma - at) : strlen(at);
            if (token == length && strncmp(at, name, length) == 0) {
                return true;
            }
            at += token + (comma != nullptr ? 1u : 0u);
        }
        return false;
    }
    if (only == nullptr) {
        return true;
    }
    char whole[64];
    (void)snprintf(whole, sizeof(whole), "ahmed_slope/%s", name);
    return strstr(whole, only) != nullptr;
}

static uint64_t sl_page(uint64_t bytes) {
    return (bytes + 4095ull) & ~4095ull;
}

static bool sl_memory(sl_t *sl) {
    const uint64_t png_bytes = display_png_bytes_needed(SL_WIDTH, SL_HEIGHT + SL_LEGEND_HEIGHT);
    const uint64_t sizes[] = {
        sl_page(sizeof(app_t)),
        sl_page((uint64_t)SL_MEASURED_FRAMES * sizeof(sl_sample_t)),
        sl_page((uint64_t)SL_MEASURED_FRAMES * sizeof(float32_t)),
        sl_page((uint64_t)SL_WIDTH * SL_HEIGHT * 4u),
        sl_page(png_bytes),
        sl_page(traverse_counts_bytes(SL_WIDTH * SL_HEIGHT)),
        sl_page((uint64_t)SL_WIDTH * (SL_HEIGHT + SL_LEGEND_HEIGHT) * 4u),
    };
    uint64_t total = 0u;
    for (uint32_t i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        total += sizes[i];
    }
    if (png_bytes == 0u || !heap_reserve(total, &sl->owned_memory)) {
        return false;
    }
    uint8_t *at = sl->owned_memory.owned_base;
    sl->borrowed_app = (app_t *)at;
    at += sizes[0];
    sl->borrowed_samples = (sl_sample_t *)at;
    at += sizes[1];
    sl->borrowed_sort = (float32_t *)at;
    at += sizes[2];
    sl->borrowed_picture = at;
    at += sizes[3];
    sl->borrowed_png = at;
    at += sizes[4];
    sl->borrowed_counts = at;
    at += sizes[5];
    sl->borrowed_heat = at;
    sl->png_bytes = png_bytes;
    memset(sl->borrowed_app, 0, sizeof(app_t));
    return true;
}

/* One row in this process, which holds nothing yet. Returns the exit status. */
static int32_t sl_process(const sl_options_t *options, uint32_t row, bool say_card, sl_result_t *result) {
    sl_t sl;
    memset(&sl, 0, sizeof(sl));
    if (!sl_memory(&sl)) {
        return 2;
    }
    int32_t status = 2;
    const bool wide = options->lod_reference != 0u || options->block_radius > SL_BLOCK_RADIUS_NARROW ||
                      SL_ROW_LIST[row].render_distance > SL_BLOCK_RADIUS_NARROW;
    if (sl_start(&sl, options->seed, wide ? SL_VOXELS_WIDE_MIB : SL_VOXELS_MIB)) {
        if (say_card) {
            const device_memory_t *const memory = &sl.borrowed_app->gpu_bridge.device_memory;
            printf("build %s, seed %" PRIX64 ", at %" PRId32 " %" PRId32 " %" PRId32
                   ", look %.1f %.1f, %ux%u, card %s\n",
                   sl_preset_name(), options->seed, options->at[0], options->at[1], options->at[2],
                   (libc_double_t)options->yaw_deg, (libc_double_t)options->pitch_deg, SL_WIDTH, SL_HEIGHT,
                   memory->device_name);
            (void)fflush(stdout);
        }
        const bool ran = sl_run(&sl, &SL_ROW_LIST[row], options, result);
        status = ran && result->sound != 0u ? 0 : 1;
    }
    const bool gpu_clean = sl.started < 5u || gpu_bridge_exit_check(&sl.borrowed_app->gpu_bridge);
    sl_stop(&sl);
    heap_release(&sl.owned_memory);
    return gpu_clean && heap_exit_check() ? status : 1;
}

/* One row in a process of its own, as bench_world does: plan 005 phase 8 saw whole runs slow the card for later rows. */
static int32_t sl_forked(const sl_options_t *options, uint32_t row, bool say_card, sl_result_t *results) {
    int32_t ends[2];
    if (pipe(ends) != 0) {
        return 2;
    }
    (void)fflush(stdout);
    (void)fflush(stderr);
    const pid_t child = fork();
    if (child < 0) {
        (void)close(ends[0]);
        (void)close(ends[1]);
        return 2;
    }
    if (child == 0) {
        (void)close(ends[0]);
        const int32_t status = sl_process(options, row, say_card, &results[row]);
        const bool sent = write(ends[1], &results[row], sizeof(results[row])) == (int64_t)sizeof(results[row]);
        (void)fflush(stdout);
        (void)fflush(stderr);
        _exit(sent ? status : 1);
    }
    (void)close(ends[1]);
    sl_result_t received;
    const int64_t got = read(ends[0], &received, sizeof(received));
    (void)close(ends[0]);
    int32_t wait_status = 0;
    const pid_t waited = waitpid(child, &wait_status, 0);
    if (got == (int64_t)sizeof(received)) {
        results[row] = received;
    }
    if (waited != child || !WIFEXITED(wait_status)) {
        return 1;
    }
    return WEXITSTATUS(wait_status);
}

int32_t main(int32_t argc, char **argv) {
    sl_options_t options;
    if (!sl_parse(argc, argv, &options)) {
        sl_usage(argv[0]);
        return 2;
    }
    if (options.list != 0u) {
        for (uint32_t i = 0u; i < SL_ROWS; i++) {
            printf("ahmed_slope/%-9s render distance %" PRIu32 ", sun %+.1f\n", SL_ROW_LIST[i].name,
                   SL_ROW_LIST[i].render_distance, (libc_double_t)SL_ROW_LIST[i].sun_deg);
        }
        return 0;
    }
    uint32_t chosen = 0u;
    for (uint32_t i = 0u; i < SL_ROWS; i++) {
        chosen += sl_chosen(i, options.only) ? 1u : 0u;
    }
    if (chosen == 0u) {
        fprintf(stderr, "no row's name holds '%s': --list prints them\n", options.only);
        return 2;
    }
    if (options.steps != nullptr && chosen != 1u) {
        fprintf(stderr, "--steps counts one row: pick it with --only\n");
        return 2;
    }
    if (options.shots != nullptr && mkdir(options.shots, 0755) != 0 && access(options.shots, W_OK) != 0) {
        fprintf(stderr, "could not make or write to %s\n", options.shots);
        return 2;
    }
    sl_result_t results[SL_ROWS];
    memset(results, 0, sizeof(results));
    const int64_t run_ns = time_now_ns();
    int32_t status = 0;
    bool said = false;
    for (uint32_t i = 0u; i < SL_ROWS; i++) {
        if (!sl_chosen(i, options.only)) {
            continue;
        }
        const int32_t row_status = sl_forked(&options, i, !said, results);
        said = true;
        status = row_status > status ? row_status : status;
        sl_row_print(&SL_ROW_LIST[i], &results[i]);
        (void)fflush(stdout);
    }
    printf("the whole run took %.1f s\n", (libc_double_t)(time_ns_to_ms(time_now_ns() - run_ns) / 1000.0f64));
    if (options.record != nullptr && !sl_record(results, &options)) {
        status = status > 1 ? status : 1;
    }
    return status;
}
