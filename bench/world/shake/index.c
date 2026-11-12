/* bench_world_shake: plan 008 phase 01's head shake and cost rose, the whole game on the real card with no display. See README.md. */
#include "display/headless/png/index.h"
#include "file/index.h"
#include "game/index.h"
#include "game/pace/index.h"
#include "game/watch/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "log/index.h"
#include "main/index.h"
#include "tables/biomes/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SH_PI 3.14159265358979f64
#define SH_DEGREES (SH_PI / 180.0f64)
#define SH_DAY_MORNING 0.35f64
#define SH_SLOT_MS (1000.0f64 / (float64_t)MC_DISPLAY_REFRESH_HZ)
#define SH_AMPLITUDE_DEG 60.0f64   /* the yaw sweeps this far either side of the heading */
#define SH_PITCH_DEG (-10.0f64)    /* and looks this far down */
#define SH_PITCH_SWEEP_DEG 20.0f64 /* the pitch row sweeps this far either side of SH_PITCH_DEG */
#define SH_PEAKS_ABOVE 12.0f32     /* jagged_peaks: blocks over the ground, as bench_world's */
#define SH_EYE_ABOVE_FEET 1.62f64  /* --at is the block the feet are in, as F3 shows it */
/* The rose is drawn in ms at this clock, from each direction's cycles: the master's capped card, about 600 MHz. The card's own speed moves
 * from one direction to the next on this card, so its ms alone would draw the clock and not the direction. */
#define SH_ROSE_MHZ 600.0f64

enum : uint32_t {
    SH_WIDTH = 1920u,
    SH_HEIGHT = 1080u,
    SH_DISTANCE = 32u,
    SH_DISTANCE_FAR = 64u,
    SH_WARM_FRAMES = 30u,
    SH_MEASURED_FRAMES = 240u,
    SH_SETTLED_HZ = 60u, /* frame k of a settled row is at k / 60 s, whatever the speed */
    SH_LIVE_SECONDS = 10u,
    SH_SAMPLES = 16384u,  /* pictures a row keeps: a minute at 270 a second */
    SH_BINS = 12u,        /* of the intervals, a slot wide each; the last holds all past it */
    SH_TREND_PARTS = 6u,  /* a live row's time cut into this many, to see a trend */
    SH_ROSE_YAWS = 72u,   /* every 5 degrees */
    SH_ROSE_PITCHES = 3u, /* -20, 0 and +15 */
    SH_ROSE_WARM = 8u,
    SH_ROSE_SIZE = 800u, /* the polar plot's PNG, square */
    SH_ROWS = 18u,
    SH_PATH_BYTES = 512u,
    SH_NAME_BYTES = 64u,
    SH_PLACE_STEP = 128u,
    SH_PLACE_RINGS = 256u,
    SH_PLACE_INSIDE = 64u,
    SH_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    SH_COLUMN_VOXELS = 16u * 16u * SH_LAYERS,
    SH_DISTINCT_WANTED = 8u,
    SH_VOXELS_MIB = 3072u,
};
static_assert(MC_GPU_BUDGET_TEXTURES_MIB + MC_GPU_BUDGET_FRAMES_MIB + SH_VOXELS_MIB < 4096u);

typedef enum sh_mode_t : uint8_t {
    SH_MODE_SETTLED = 0, /* one thread, the world whole before every picture */
    SH_MODE_CAPPED =
        1, /* the game's threads, main at the display's rate, vsync on: a picture only once main took the last */
    SH_MODE_UNCAPPED = 2 /* the same with vsync off: the gpu thread draws back to back */
} sh_mode_t;

/* One row: a speed, a way of running and a render distance. */
typedef struct sh_row_t {
    const char *name;         /* 0: after the place's name and a slash */
    float64_t speed_deg_s;    /* 8 */
    uint32_t render_distance; /* 16 */
    uint8_t mode;             /* 20: sh_mode_t */
    uint8_t pitch;            /* 21: sweeps the pitch, not the yaw */
    uint8_t _pad0[2];         /* 22 */
} sh_row_t;
static_assert(sizeof(sh_row_t) == 24);

static const sh_row_t SH_ROW_LIST[SH_ROWS] = {
    {"shake-0", 0.0f64, SH_DISTANCE, SH_MODE_SETTLED, 0u, {0}},
    {"shake-90", 90.0f64, SH_DISTANCE, SH_MODE_SETTLED, 0u, {0}},
    {"shake-360", 360.0f64, SH_DISTANCE, SH_MODE_SETTLED, 0u, {0}},
    {"shake-720", 720.0f64, SH_DISTANCE, SH_MODE_SETTLED, 0u, {0}},
    {"shake-1440", 1440.0f64, SH_DISTANCE, SH_MODE_SETTLED, 0u, {0}},
    {"shake-720-pitch", 720.0f64, SH_DISTANCE, SH_MODE_SETTLED, 1u, {0}},
    {"shake-0-live-capped", 0.0f64, SH_DISTANCE, SH_MODE_CAPPED, 0u, {0}},
    {"shake-720-live-capped", 720.0f64, SH_DISTANCE, SH_MODE_CAPPED, 0u, {0}},
    {"shake-720-live", 720.0f64, SH_DISTANCE, SH_MODE_UNCAPPED, 0u, {0}},
    {"shake-0-r64", 0.0f64, SH_DISTANCE_FAR, SH_MODE_SETTLED, 0u, {0}},
    {"shake-90-r64", 90.0f64, SH_DISTANCE_FAR, SH_MODE_SETTLED, 0u, {0}},
    {"shake-360-r64", 360.0f64, SH_DISTANCE_FAR, SH_MODE_SETTLED, 0u, {0}},
    {"shake-720-r64", 720.0f64, SH_DISTANCE_FAR, SH_MODE_SETTLED, 0u, {0}},
    {"shake-1440-r64", 1440.0f64, SH_DISTANCE_FAR, SH_MODE_SETTLED, 0u, {0}},
    {"shake-720-pitch-r64", 720.0f64, SH_DISTANCE_FAR, SH_MODE_SETTLED, 1u, {0}},
    {"shake-0-live-capped-r64", 0.0f64, SH_DISTANCE_FAR, SH_MODE_CAPPED, 0u, {0}},
    {"shake-720-live-capped-r64", 720.0f64, SH_DISTANCE_FAR, SH_MODE_CAPPED, 0u, {0}},
    {"shake-720-live-r64", 720.0f64, SH_DISTANCE_FAR, SH_MODE_UNCAPPED, 0u, {0}},
};

/* What a row came to. Settled, an interval is the card's time on the picture, as there is no clock to show it by. */
typedef struct sh_result_t {
    float64_t interval_p50_ms;            /* 0 */
    float64_t interval_p99_ms;            /* 8 */
    float64_t interval_worst_ms;          /* 16 */
    float64_t late_share;                 /* 24: live: pictures main took late, of all it took */
    float64_t card_mean_ms;               /* 32 */
    float64_t card_p99_ms;                /* 40 */
    float64_t card_worst_ms;              /* 48 */
    float64_t pass_ms[FRAME_PASS_COUNT];  /* 56 */
    float64_t lod_ms;                     /* 96 */
    float64_t shade_ms[SHADE_SPAN_COUNT]; /* 104 */
    float64_t fresh_mean;                 /* 144 */
    float64_t p_ms;           /* 152: the gpu thread's own work on a picture; settled, the host's past the card */
    float64_t wait_take_ms;   /* 160: live: the gpu thread waiting for main to take the last picture */
    float64_t card_mhz;       /* 168: the mean of the readings */
    float64_t card_mhz_low;   /* 176 */
    float64_t card_mhz_high;  /* 184 */
    float64_t pictures_per_s; /* 192 */
    float64_t seconds;        /* 200 */
    float64_t trend_card_ms[SH_TREND_PARTS]; /* 208 */
    float64_t trend_p_ms[SH_TREND_PARTS];    /* 256 */
    float64_t trend_mhz[SH_TREND_PARTS];     /* 304 */
    /* 352: a picture's card time in the SM's cycles, millions: ms x MHz / 1000 on each picture that has its own clock reading. It is what
     * compares rows whose card ran at different speeds, as this card's does from second to second. */
    float64_t card_mcycles;
    uint64_t picture_hash;  /* 360 */
    uint64_t pictures;      /* 368 */
    uint32_t bins[SH_BINS]; /* 376 */
    uint32_t fresh_max;     /* 424 */
    int32_t place[3];       /* 428 */
    uint8_t ran;            /* 440 */
    uint8_t sound;          /* 441 */
    uint8_t _pad0[2];       /* 442 */
    /* 444: plan 008 phase 02: the gpu thread's wait before a picture the rule started at once (after a late one, or back to back): the card
     * idle while late. The target is under 0.5 ms */
    float32_t wait_at_once_ms;
} sh_result_t;
static_assert(sizeof(sh_result_t) == 448);

typedef struct sh_options_t {
    const char *only;         /* 0 */
    const char *record;       /* 8 */
    const char *label;        /* 16 */
    const char *shots;        /* 24 */
    uint64_t seed;            /* 32 */
    int32_t at[3];            /* 40 */
    uint32_t features_off;    /* 52 */
    float64_t yaw_deg;        /* 56: the heading the shake is about */
    uint32_t render_distance; /* 64: the rows' 32 becomes this; 0 for 32 */
    uint32_t seconds;         /* 68: a live row's length */
    uint8_t has_at;           /* 72 */
    uint8_t rose;             /* 73 */
    uint8_t list;             /* 74 */
    uint8_t _pad0[5];         /* 75 */
} sh_options_t;
static_assert(sizeof(sh_options_t) == 80);

/* One picture of a live row, kept as it is read off the rings. */
typedef struct sh_sample_t {
    int64_t taken_ns;                     /* 0 */
    float32_t interval;                   /* 8: ms from the last take; 0 for the first */
    float32_t card_ms;                    /* 12 */
    float32_t p_ms;                       /* 16 */
    float32_t wait_ms;                    /* 20 */
    float32_t mhz;                        /* 24 */
    uint32_t fresh;                       /* 28 */
    float32_t pass_ms[FRAME_PASS_COUNT];  /* 32 */
    float32_t lod_ms;                     /* 52 */
    float32_t shade_ms[SHADE_SPAN_COUNT]; /* 56 */
    uint8_t late;                         /* 76 */
    uint8_t joined;                       /* 77: its picture was still in the ring */
    uint8_t how;                          /* 78: plan 008 phase 02: how the start rule started it, game_pace_how_t */
    uint8_t _pad0[1];                     /* 79 */
} sh_sample_t;
static_assert(sizeof(sh_sample_t) == 80);

typedef struct sh_t {
    heap_region_t owned_memory;    /* 0 */
    app_t *borrowed_app;           /* 16 */
    sh_sample_t *borrowed_samples; /* 24: SH_SAMPLES */
    float32_t *borrowed_sort;      /* 32: SH_SAMPLES, for percentiles */
    uint16_t *borrowed_states;     /* 40 */
    uint8_t *borrowed_opened;      /* 48 */
    uint8_t *borrowed_picture;     /* 56 */
    uint8_t *borrowed_png;         /* 64 */
    uint8_t *borrowed_plot;        /* 72: SH_ROSE_SIZE square, XRGB */
    uint64_t png_bytes;            /* 80 */
    uint32_t started;              /* 88 */
    uint8_t _pad0[4];              /* 92 */
} sh_t;
static_assert(sizeof(sh_t) == 96);

/* ---- Starting and stopping, as bench_world does ------------------------------------------------------------------- */

static bool sh_start(sh_t *sh, uint64_t seed) {
    app_t *const app = sh->borrowed_app;
    if (!math_init(&app->math)) {
        return false;
    }
    sh->started = 1u;
    if (!simd_init(&app->simd)) {
        return false;
    }
    sh->started = 2u;
    if (!texture_init(&app->texture)) {
        return false;
    }
    sh->started = 3u;
    if (!thread_topology_read(&app->topology) || !job_init(&app->job, &app->topology)) {
        return false;
    }
    sh->started = 4u;
    gpu_bridge_limits_t limits = gpu_bridge_limits_default();
    limits.device.pool_bytes[DEVICE_POOL_VOXELS] = (uint64_t)SH_VOXELS_MIB << 20;
    if (gpu_bridge_init(&app->gpu_bridge, limits) != GPU_STATUS_OK) {
        return false;
    }
    sh->started = 5u;
    if (!thread_pin_self(thread_cpu_for(&app->topology, THREAD_ROLE_MAIN, 0u)) ||
        !world_init(&app->world, world_limits_default())) {
        return false;
    }
    sh->started = 6u;
    if (!terrain_init(&app->terrain, seed)) {
        return false;
    }
    sh->started = 7u;
    if (!render_init(&app->render, &app->gpu_bridge.device_memory, &app->job, render_limits_default())) {
        return false;
    }
    sh->started = 8u;
    if (!ui_init(&app->ui, &app->texture)) {
        return false;
    }
    sh->started = 9u;
    return true;
}

static void sh_stop(sh_t *sh) {
    app_t *const app = sh->borrowed_app;
    const uint32_t started = sh->started;
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

/* ---- The place ----------------------------------------------------------------------------------------------------- */

static int32_t sh_ground_z(const sh_t *sh, int32_t x, int32_t y) {
    const terrain_density_box_t box = {.origin_x = x,
                                       .origin_y = y,
                                       .origin_z = COLUMN_Z_STORAGE_MIN,
                                       .count_x = 1u,
                                       .count_y = 1u,
                                       .count_z = SH_LAYERS};
    terrain_generate_box_fallback(&sh->borrowed_app->terrain, &box, sh->borrowed_opened, sh->borrowed_states);
    for (int32_t storage = (int32_t)SH_LAYERS - 1; storage > 0; storage--) {
        if (sh->borrowed_states[storage] != BLOCK_ID_AIR) {
            return storage + COLUMN_Z_STORAGE_MIN;
        }
    }
    return COLUMN_Z_STORAGE_MIN;
}

static bool sh_biome_is(const terrain_t *terrain, int32_t x, int32_t y, uint8_t biome) {
    const terrain_fields_area_t area = {.origin_x = x - (int32_t)SH_PLACE_INSIDE,
                                        .origin_y = y - (int32_t)SH_PLACE_INSIDE,
                                        .step_x = (int32_t)SH_PLACE_INSIDE,
                                        .step_y = (int32_t)SH_PLACE_INSIDE,
                                        .count_x = 3u,
                                        .count_y = 3u};
    terrain_biome_column_t columns[9];
    terrain_biome_columns_area(terrain->borrowed_biome, &area, columns);
    for (uint32_t i = 0u; i < 9u; i++) {
        if (columns[i].biome != biome) {
            return false;
        }
    }
    return true;
}

/* The nearest jagged peaks to the world's middle, in square rings, as bench_world finds them. Its eye is over the highest ground near it. */
static bool sh_find_peaks(const sh_t *sh, float64_t out_eye[3]) {
    int32_t dx = 0, dy = 0, ring = 0;
    while (ring <= (int32_t)SH_PLACE_RINGS) {
        const int32_t x = dx * (int32_t)SH_PLACE_STEP, y = dy * (int32_t)SH_PLACE_STEP;
        if (sh_biome_is(&sh->borrowed_app->terrain, x, y, (uint8_t)BIOME_ID_JAGGED_PEAKS)) {
            int32_t highest = sh_ground_z(sh, x, y);
            for (uint32_t i = 0u; i < 24u; i++) {
                const float64_t angle = 2.0f64 * SH_PI * (float64_t)i / 8.0f64;
                const float64_t radius = (float64_t)(12u * (i / 8u + 1u));
                const int32_t ground =
                    sh_ground_z(sh, x + (int32_t)(radius * cos(angle)), y + (int32_t)(radius * sin(angle)));
                highest = ground > highest ? ground : highest;
            }
            out_eye[0] = (float64_t)x + 0.5f64;
            out_eye[1] = (float64_t)y + 0.5f64;
            out_eye[2] = (float64_t)highest + (float64_t)SH_PEAKS_ABOVE;
            return true;
        }
        if (dx == ring && dy == ring) {
            ring++;
            dx = -ring;
            dy = -ring;
        } else if (dx == ring) {
            dx = -ring;
            dy++;
        } else if (dy == -ring || dy == ring) {
            dx++;
        } else {
            dx = ring;
        }
    }
    return false;
}

/* ---- Numbers ------------------------------------------------------------------------------------------------------- */

static int32_t sh_compare(const void *left, const void *right) {
    const float32_t a = *(const float32_t *)left, b = *(const float32_t *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

/* p50, p99 and the most of count values, which it sorts. */
static void sh_spread(float32_t *values, uint32_t count, float64_t *p50, float64_t *p99, float64_t *most) {
    *p50 = 0.0f64;
    *p99 = 0.0f64;
    *most = 0.0f64;
    if (count == 0u) {
        return;
    }
    qsort(values, count, sizeof(values[0]), sh_compare);
    *p50 = (float64_t)values[count / 2u];
    *p99 = (float64_t)values[(uint32_t)((uint64_t)count * 99u / 100u)];
    *most = (float64_t)values[count - 1u];
}

static uint64_t sh_hash(const uint8_t *bytes, uint64_t count) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint64_t i = 0u; i < count; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

static bool sh_picture_has_things(const uint8_t *picture) {
    uint32_t seen[SH_DISTINCT_WANTED];
    uint32_t count = 0u;
    for (uint32_t row = 0u; row < 36u && count < SH_DISTINCT_WANTED; row++) {
        const uint32_t y = (row * 2u + 1u) * SH_HEIGHT / 72u;
        for (uint32_t across = 0u; across < 64u && count < SH_DISTINCT_WANTED; across++) {
            const uint32_t x = (across * 2u + 1u) * SH_WIDTH / 128u;
            uint32_t pixel = 0u;
            memcpy(&pixel, picture + ((uint64_t)y * SH_WIDTH + x) * 4u, sizeof(pixel));
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
    return count >= SH_DISTINCT_WANTED;
}

/* A triangle wave from -1 to 1, a period every 4 / rate, at 0 when t is 0. */
static float64_t sh_triangle(float64_t phase) {
    const float64_t wrapped = phase - 4.0f64 * floor(phase / 4.0f64);
    return wrapped < 1.0f64 ? wrapped : (wrapped < 3.0f64 ? 2.0f64 - wrapped : wrapped - 4.0f64);
}

/* Where a shake looks seconds in: a constant angular speed back and forth over twice the sweep. */
static void sh_look(const sh_row_t *row, float64_t heading_deg, float64_t seconds, render_camera_t *camera) {
    const float64_t sweep = row->pitch != 0u ? SH_PITCH_SWEEP_DEG : SH_AMPLITUDE_DEG;
    const float64_t swing =
        row->speed_deg_s > 0.0f64 ? sweep * sh_triangle(row->speed_deg_s * seconds / sweep) : 0.0f64;
    camera->yaw = (float32_t)((heading_deg + (row->pitch != 0u ? 0.0f64 : swing)) * SH_DEGREES);
    camera->pitch = (float32_t)((SH_PITCH_DEG + (row->pitch != 0u ? swing : 0.0f64)) * SH_DEGREES);
}

static void sh_card_note(sh_result_t *r, float32_t mhz) {
    if (mhz <= 0.0f32) {
        return;
    }
    r->card_mhz_low = r->card_mhz_low == 0.0f64 || (float64_t)mhz < r->card_mhz_low ? (float64_t)mhz : r->card_mhz_low;
    r->card_mhz_high = (float64_t)mhz > r->card_mhz_high ? (float64_t)mhz : r->card_mhz_high;
}

/* What the samples come to, intervals from the samples' own field. */
static void sh_samples_sum(sh_t *sh, uint32_t count, sh_result_t *r) {
    const sh_sample_t *const samples = sh->borrowed_samples;
    float32_t *const sort = sh->borrowed_sort;
    uint32_t intervals = 0u, late = 0u, joined = 0u, mhz_count = 0u, at_once = 0u;
    float64_t fresh = 0.0f64, p = 0.0f64, wait = 0.0f64, mhz = 0.0f64, lod = 0.0f64, wait_at_once = 0.0f64;
    for (uint32_t i = 0u; i < count; i++) {
        const sh_sample_t *const s = &samples[i];
        late += s->late;
        if (s->interval > 0.0f32) {
            sort[intervals++] = s->interval;
            const uint32_t bin = (uint32_t)((float64_t)s->interval / SH_SLOT_MS);
            r->bins[bin < SH_BINS ? bin : SH_BINS - 1u]++;
        }
        if (s->joined == 0u) {
            continue;
        }
        joined++;
        fresh += (float64_t)s->fresh;
        r->fresh_max = s->fresh > r->fresh_max ? s->fresh : r->fresh_max;
        p += (float64_t)s->p_ms;
        wait += (float64_t)s->wait_ms;
        if (s->how == GAME_PACE_HOW_AFTER_LATE || s->how == GAME_PACE_HOW_BACK_TO_BACK) {
            wait_at_once += (float64_t)s->wait_ms;
            at_once++;
        }
        lod += (float64_t)s->lod_ms;
        for (uint32_t k = 0u; k < FRAME_PASS_COUNT; k++) {
            r->pass_ms[k] += (float64_t)s->pass_ms[k];
        }
        for (uint32_t k = 0u; k < SHADE_SPAN_COUNT; k++) {
            r->shade_ms[k] += (float64_t)s->shade_ms[k];
        }
        if (s->mhz > 0.0f32) {
            mhz += (float64_t)s->mhz;
            mhz_count++;
            sh_card_note(r, s->mhz);
        }
    }
    sh_spread(sort, intervals, &r->interval_p50_ms, &r->interval_p99_ms, &r->interval_worst_ms);
    r->late_share = count != 0u ? (float64_t)late / (float64_t)count : 0.0f64;
    r->wait_at_once_ms = at_once != 0u ? (float32_t)(wait_at_once / (float64_t)at_once) : 0.0f32;
    r->pictures = count;
    if (joined != 0u) {
        r->fresh_mean = fresh / (float64_t)joined;
        r->p_ms = p / (float64_t)joined;
        r->wait_take_ms = wait / (float64_t)joined;
        r->lod_ms = lod / (float64_t)joined;
        for (uint32_t k = 0u; k < FRAME_PASS_COUNT; k++) {
            r->pass_ms[k] /= (float64_t)joined;
        }
        for (uint32_t k = 0u; k < SHADE_SPAN_COUNT; k++) {
            r->shade_ms[k] /= (float64_t)joined;
        }
    }
    r->card_mhz = mhz_count != 0u ? mhz / (float64_t)mhz_count : 0.0f64;
    float64_t cycles = 0.0f64;
    uint32_t clocked = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        if (samples[i].joined != 0u && samples[i].mhz > 0.0f32) {
            cycles += (float64_t)samples[i].card_ms * (float64_t)samples[i].mhz / 1000.0f64;
            clocked++;
        }
    }
    r->card_mcycles = clocked != 0u ? cycles / (float64_t)clocked : 0.0f64;
    uint32_t cards = 0u;
    float64_t card_sum = 0.0f64;
    for (uint32_t i = 0u; i < count; i++) {
        if (samples[i].joined != 0u) {
            sort[cards++] = samples[i].card_ms;
            card_sum += (float64_t)samples[i].card_ms;
        }
    }
    float64_t p50 = 0.0f64;
    r->card_mean_ms = cards != 0u ? card_sum / (float64_t)cards : 0.0f64;
    sh_spread(sort, cards, &p50, &r->card_p99_ms, &r->card_worst_ms);
    /* The trend: the samples cut into equal parts by count. */
    for (uint32_t part = 0u; part < SH_TREND_PARTS && count != 0u; part++) {
        const uint32_t from = part * count / SH_TREND_PARTS, to = (part + 1u) * count / SH_TREND_PARTS;
        uint32_t n = 0u, m = 0u;
        float64_t card = 0.0f64, pp = 0.0f64, hz = 0.0f64;
        for (uint32_t i = from; i < to; i++) {
            if (samples[i].joined != 0u) {
                card += (float64_t)samples[i].card_ms;
                pp += (float64_t)samples[i].p_ms;
                n++;
                if (samples[i].mhz > 0.0f32) {
                    hz += (float64_t)samples[i].mhz;
                    m++;
                }
            }
        }
        r->trend_card_ms[part] = n != 0u ? card / (float64_t)n : 0.0f64;
        r->trend_p_ms[part] = n != 0u ? pp / (float64_t)n : 0.0f64;
        r->trend_mhz[part] = m != 0u ? hz / (float64_t)m : 0.0f64;
    }
}

/* ---- A settled row ------------------------------------------------------------------------------------------------- */

static display_buffer_t sh_target(const sh_t *sh) {
    return (display_buffer_t){.owned_map = sh->borrowed_picture,
                              .map_bytes = (uint64_t)SH_WIDTH * SH_HEIGHT * 4u,
                              .pitch = SH_WIDTH * 4u,
                              .width = SH_WIDTH,
                              .height = SH_HEIGHT};
}

/* One settled picture from camera, as the game draws one with no threads: a tick, then the draw. With clock, the card's speed is read
 * before it, outside its passes' times: the speed moves from one picture to the next on this card. */
static bool sh_settled_draw(sh_t *sh, const display_headless_scene_t *scene, uint32_t frame, bool clock) {
    if (clock) {
        game_watch_of(&sh->borrowed_app->game)->clock_asked_ns = 0;
    }
    display_buffer_t target = sh_target(sh);
    display_script_state_t state;
    memset(&state, 0, sizeof(state));
    state.tick = frame;
    display_headless_tick_t tick = {.events = nullptr, .state = &state, .event_count = 0u, .tick = frame};
    return scene->tick(scene->borrowed_context, &tick) && scene->draw(scene->borrowed_context, &tick, &target);
}

/* A settled picture's numbers, from the frame's books and the watch's marks. */
static void sh_settled_sample(const sh_t *sh, sh_sample_t *out, float32_t host_ms) {
    const app_t *const app = sh->borrowed_app;
    const frame_timings_t *const timings = &app->render.frame.last_timings;
    const frame_marks_t *const marks = game_watch_of(&app->game)->borrowed_marks;
    memset(out, 0, sizeof(*out));
    out->interval = timings->total_ms;
    out->card_ms = timings->total_ms;
    out->p_ms = host_ms - timings->total_ms;
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
    out->joined = 1u;
}

static bool sh_row_settled(sh_t *sh, const sh_row_t *row, float64_t heading_deg, sh_result_t *r) {
    game_t *const game = &sh->borrowed_app->game;
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    const int64_t started_ns = time_now_ns();
    for (uint32_t frame = 0u; frame < SH_WARM_FRAMES + SH_MEASURED_FRAMES; frame++) {
        sh_look(row, heading_deg, (float64_t)frame / (float64_t)SH_SETTLED_HZ, &game->camera);
        const int64_t draw_ns = time_now_ns();
        if (!sh_settled_draw(sh, &scene, frame, frame >= SH_WARM_FRAMES)) {
            return false;
        }
        if (frame >= SH_WARM_FRAMES) {
            sh_settled_sample(sh, &sh->borrowed_samples[frame - SH_WARM_FRAMES],
                              (float32_t)time_ns_to_ms(time_now_ns() - draw_ns));
        }
    }
    r->seconds = time_ns_to_ms(time_now_ns() - started_ns) / 1000.0f64;
    sh_samples_sum(sh, SH_MEASURED_FRAMES, r);
    r->late_share = 0.0f64;
    r->pictures_per_s = r->card_mean_ms > 0.0f64 ? 1000.0f64 / r->card_mean_ms : 0.0f64;
    return true;
}

/* ---- A live row ---------------------------------------------------------------------------------------------------- */

/* The takes main has made since the last look, joined to their pictures, into samples. Main's own ring, and pictures under their count:
 * read on main between its frames. */
static uint32_t sh_rings_take(sh_t *sh, const game_watch_t *watch, uint64_t *takes_read, int64_t *last_taken_ns,
                              uint32_t count) {
    const uint64_t made = atomic_load_explicit(&watch->takes_made, memory_order_relaxed);
    const uint64_t drawn = atomic_load_explicit(&watch->pictures_drawn, memory_order_acquire);
    while (*takes_read < made && count < SH_SAMPLES) {
        const game_watch_take_t *const take = &watch->borrowed_takes[*takes_read % GAME_WATCH_RING];
        if (take->seq >= drawn) {
            break;
        }
        sh_sample_t *const s = &sh->borrowed_samples[count];
        memset(s, 0, sizeof(*s));
        s->taken_ns = take->taken_ns;
        s->interval = *last_taken_ns != 0 ? (float32_t)time_ns_to_ms(take->taken_ns - *last_taken_ns) : 0.0f32;
        s->late = take->frame > take->start_frame + 1u ? 1u : 0u;
        const game_watch_picture_t *const p = &watch->borrowed_pictures[take->seq % GAME_WATCH_RING];
        if (drawn - take->seq < GAME_WATCH_RING - 64u && p->seq == take->seq) {
            const int64_t *const at = p->stamp_ns;
            /* Plan 008 phase 02: late is ready after the take the start rule aimed it at, as the frame log says, when it was aimed. */
            s->late = p->aimed_ns != 0 ? p->missed : s->late;
            s->how = p->pace_how;
            s->joined = 1u;
            s->card_ms = p->total_ms;
            s->p_ms = (float32_t)(time_ns_to_ms(at[GAME_WATCH_STAMP_READY] - at[GAME_WATCH_STAMP_START]) -
                                  (float64_t)p->total_ms);
            s->wait_ms = (float32_t)time_ns_to_ms(at[GAME_WATCH_STAMP_START] - at[GAME_WATCH_STAMP_WAITED]);
            s->mhz = p->clock_read != 0u ? p->card_mhz : 0.0f32;
            s->fresh = p->fresh;
            for (uint32_t k = 0u; k < FRAME_PASS_COUNT; k++) {
                s->pass_ms[k] = p->pass_ms[k];
            }
            s->lod_ms = p->lod_ms;
            for (uint32_t k = 0u; k < SHADE_SPAN_COUNT; k++) {
                s->shade_ms[k] = p->shade_ms[k];
            }
        }
        *last_taken_ns = take->taken_ns;
        (*takes_read)++;
        count++;
    }
    return count;
}

static bool sh_row_live(sh_t *sh, const sh_row_t *row, float64_t heading_deg, uint32_t seconds, sh_result_t *r) {
    game_t *const game = &sh->borrowed_app->game;
    display_buffer_t target = sh_target(sh);
    if (!game_loop_start(game, nullptr)) {
        return false;
    }
    const game_watch_t *const watch = game_watch_of(game);
    uint64_t takes_read = atomic_load_explicit(&watch->takes_made, memory_order_relaxed);
    int64_t last_taken_ns = 0;
    uint32_t count = 0u;
    const render_fly_input_t still = {0};
    const int64_t frame_ns = TIME_NS_PER_SECOND / (int64_t)MC_DISPLAY_REFRESH_HZ;
    const int64_t total_ns = (int64_t)seconds * TIME_NS_PER_SECOND;
    const int64_t started_ns = time_now_ns();
    int64_t now_ns = started_ns, last_ns = started_ns, next_ns = started_ns;
    bool ok = true;
    while (ok && now_ns - started_ns < total_ns) {
        sh_look(row, heading_deg, (float64_t)(now_ns - started_ns) / (float64_t)TIME_NS_PER_SECOND, &game->camera);
        game_loop_next_take(game,
                            next_ns + frame_ns); /* plan 008 phase 02: the tick it sleeps to next, as a window's */
        ok = game_loop_frame(game, &still, (float32_t)((float64_t)(now_ns - last_ns) / (float64_t)TIME_NS_PER_SECOND),
                             &target);
        count = sh_rings_take(sh, watch, &takes_read, &last_taken_ns, count);
        last_ns = now_ns;
        next_ns += frame_ns;
        now_ns = time_now_ns();
        /* Main keeps to the display's rate by the clock whether vsync is on or not, as in a window: only the gpu thread is uncapped. */
        if (next_ns > now_ns) {
            time_sleep_until_ns(next_ns);
            now_ns = time_now_ns();
        }
    }
    ok = game_loop_stop(game) && ok;
    count = sh_rings_take(sh, watch, &takes_read, &last_taken_ns, count);
    if (!ok) {
        return false;
    }
    r->seconds = time_ns_to_ms(time_now_ns() - started_ns) / 1000.0f64;
    sh_samples_sum(sh, count, r);
    r->pictures_per_s = (float64_t)count / r->seconds;
    return true;
}

/* ---- The cost rose ------------------------------------------------------------------------------------------------- */

static const float64_t SH_ROSE_PITCH_DEG[SH_ROSE_PITCHES] = {-20.0f64, 0.0f64, 15.0f64};
static const uint32_t SH_ROSE_COLOUR[SH_ROSE_PITCHES] = {0x004E9AFFu, 0x0020C060u, 0x00F08020u};

static void sh_plot_dot(uint8_t *plot, int32_t x, int32_t y, uint32_t colour, int32_t radius) {
    for (int32_t dy = -radius; dy <= radius; dy++) {
        for (int32_t dx = -radius; dx <= radius; dx++) {
            const int32_t px = x + dx, py = y + dy;
            if (px >= 0 && py >= 0 && px < (int32_t)SH_ROSE_SIZE && py < (int32_t)SH_ROSE_SIZE) {
                memcpy(plot + ((uint64_t)py * SH_ROSE_SIZE + (uint64_t)px) * 4u, &colour, 4u);
            }
        }
    }
}

/* A line from one point to the next, as dots. */
static void sh_plot_line(uint8_t *plot, float64_t x0, float64_t y0, float64_t x1, float64_t y1, uint32_t colour) {
    const float64_t steps = fmax(fabs(x1 - x0), fabs(y1 - y0)) + 1.0f64;
    for (float64_t i = 0.0f64; i <= steps; i += 1.0f64) {
        const float64_t f = i / steps;
        sh_plot_dot(plot, (int32_t)(x0 + (x1 - x0) * f), (int32_t)(y0 + (y1 - y0) * f), colour, 1);
    }
}

/* The polar plot: north up, east right, as a map; a ring per pitch, and grey circles at one and two slots. ms_most is the outer edge. */
static void sh_rose_plot(sh_t *sh, const float32_t ms[SH_ROSE_PITCHES][SH_ROSE_YAWS], float64_t ms_most) {
    uint8_t *const plot = sh->borrowed_plot;
    const uint32_t white = 0x00FFFFFFu, grey = 0x00A0A0A0u, black = 0x00000000u;
    for (uint64_t i = 0u; i < (uint64_t)SH_ROSE_SIZE * SH_ROSE_SIZE; i++) {
        memcpy(plot + i * 4u, &white, 4u);
    }
    const float64_t centre = (float64_t)SH_ROSE_SIZE / 2.0f64, scale = (centre - 20.0f64) / ms_most;
    for (uint32_t slots = 1u; slots <= 3u; slots++) {
        const float64_t radius = SH_SLOT_MS * (float64_t)slots * scale;
        for (uint32_t i = 0u; i < 720u; i++) {
            const float64_t a = 2.0f64 * SH_PI * (float64_t)i / 720.0f64;
            sh_plot_dot(plot, (int32_t)(centre + radius * cos(a)), (int32_t)(centre - radius * sin(a)), grey, 0);
        }
    }
    sh_plot_line(plot, centre, 10.0f64, centre, (float64_t)SH_ROSE_SIZE - 10.0f64, grey);
    sh_plot_line(plot, 10.0f64, centre, (float64_t)SH_ROSE_SIZE - 10.0f64, centre, grey);
    sh_plot_dot(plot, (int32_t)centre, (int32_t)centre, black, 2);
    for (uint32_t p = 0u; p < SH_ROSE_PITCHES; p++) {
        for (uint32_t y = 0u; y < SH_ROSE_YAWS; y++) {
            const uint32_t next = (y + 1u) % SH_ROSE_YAWS;
            /* Yaw 0 looks along +x, east; yaw grows towards +y, north. */
            const float64_t a0 = 2.0f64 * SH_PI * (float64_t)y / SH_ROSE_YAWS;
            const float64_t a1 = 2.0f64 * SH_PI * (float64_t)next / SH_ROSE_YAWS;
            const float64_t r0 = (float64_t)ms[p][y] * scale, r1 = (float64_t)ms[p][next] * scale;
            sh_plot_line(plot, centre + r0 * cos(a0), centre - r0 * sin(a0), centre + r1 * cos(a1),
                         centre - r1 * sin(a1), SH_ROSE_COLOUR[p]);
        }
    }
}

static bool sh_png_write(sh_t *sh, const uint8_t *pixels, uint32_t width, uint32_t height, const char *path) {
    uint64_t bytes = 0u;
    if (!display_png_encode(pixels, width * 4u, width, height, sh->borrowed_png, sh->png_bytes, &bytes) ||
        file_write_atomic(path, sh->borrowed_png, bytes) != FILE_OK) {
        LOG_WARN("bench_world_shake: could not write %s", path);
        return false;
    }
    return true;
}

static bool sh_rose(sh_t *sh, const sh_options_t *options, const char *place_name) {
    game_t *const game = &sh->borrowed_app->game;
    display_headless_scene_t scene;
    game_headless_scene(game, &scene);
    float32_t total[SH_ROSE_PITCHES][SH_ROSE_YAWS];
    char path[SH_PATH_BYTES];
    const char *const directory = options->shots != nullptr ? options->shots : ".";
    (void)snprintf(path, sizeof(path), "%s/rose-%s-r%" PRIu32 ".csv", directory, place_name,
                   options->render_distance != 0u ? options->render_distance : (uint32_t)SH_DISTANCE);
    FILE *const csv = fopen(path, "w");
    if (csv == nullptr) {
        LOG_ERROR("bench_world_shake: could not write %s", path);
        return false;
    }
    fprintf(csv, "yaw_deg,pitch_deg,card_ms,coarse_ms,primary_ms,lod_ms,shade_ms,water_ms,surfaces_ms,sun_ms,ao_ms,"
                 "ao_fresh_ms,post_ms,readback_ms,fresh,card_mhz,card_mcycles,card_ms_at_600_mhz\n");
    uint32_t frame = 0u, worst_p = 0u, worst_y = 0u;
    float32_t mhz = 0.0f32;
    for (uint32_t p = 0u; p < SH_ROSE_PITCHES; p++) {
        for (uint32_t y = 0u; y < SH_ROSE_YAWS; y++) {
            game->camera.yaw = (float32_t)((float64_t)y * 360.0f64 / SH_ROSE_YAWS * SH_DEGREES);
            game->camera.pitch = (float32_t)(SH_ROSE_PITCH_DEG[p] * SH_DEGREES);
            for (uint32_t warm = 0u; warm <= SH_ROSE_WARM; warm++) {
                if (!sh_settled_draw(sh, &scene, frame++, warm == SH_ROSE_WARM)) {
                    (void)fclose(csv);
                    return false;
                }
            }
            sh_sample_t s;
            sh_settled_sample(sh, &s, 0.0f32);
            mhz = s.mhz > 0.0f32 ? s.mhz : mhz;
            const float64_t mcycles = (float64_t)s.card_ms * (float64_t)mhz / 1000.0f64;
            total[p][y] = mhz > 0.0f32 ? (float32_t)(mcycles * 1000.0f64 / SH_ROSE_MHZ) : s.card_ms;
            if (total[p][y] > total[worst_p][worst_y]) {
                worst_p = p;
                worst_y = y;
            }
            fprintf(csv,
                    "%.0f,%.0f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%" PRIu32
                    ",%.0f,%.3f,%.3f\n",
                    (libc_double_t)((float64_t)y * 360.0f64 / SH_ROSE_YAWS), (libc_double_t)SH_ROSE_PITCH_DEG[p],
                    (libc_double_t)s.card_ms, (libc_double_t)s.pass_ms[FRAME_PASS_COARSE],
                    (libc_double_t)s.pass_ms[FRAME_PASS_PRIMARY], (libc_double_t)s.lod_ms,
                    (libc_double_t)s.pass_ms[FRAME_PASS_SHADE], (libc_double_t)s.shade_ms[SHADE_SPAN_WATER],
                    (libc_double_t)s.shade_ms[SHADE_SPAN_SURFACES], (libc_double_t)s.shade_ms[SHADE_SPAN_SUN],
                    (libc_double_t)s.shade_ms[SHADE_SPAN_AO], (libc_double_t)s.shade_ms[SHADE_SPAN_FRESH],
                    (libc_double_t)s.pass_ms[FRAME_PASS_POST], (libc_double_t)s.pass_ms[FRAME_PASS_READBACK], s.fresh,
                    (libc_double_t)mhz, (libc_double_t)mcycles, (libc_double_t)total[p][y]);
        }
    }
    (void)fclose(csv);
    float64_t most = SH_SLOT_MS * 2.0f64, sum = 0.0f64, least = 1.0e9f64;
    for (uint32_t p = 0u; p < SH_ROSE_PITCHES; p++) {
        for (uint32_t y = 0u; y < SH_ROSE_YAWS; y++) {
            most = (float64_t)total[p][y] > most ? (float64_t)total[p][y] : most;
            least = (float64_t)total[p][y] < least ? (float64_t)total[p][y] : least;
            sum += (float64_t)total[p][y];
        }
    }
    printf("rose %s r%" PRIu32
           ": card ms at 600 MHz least %.2f mean %.2f most %.2f, at yaw %.0f pitch %.0f; card %.0f MHz last; %s\n",
           place_name, options->render_distance != 0u ? options->render_distance : (uint32_t)SH_DISTANCE,
           (libc_double_t)least, (libc_double_t)(sum / (SH_ROSE_PITCHES * SH_ROSE_YAWS)),
           (libc_double_t)total[worst_p][worst_y], (libc_double_t)((float64_t)worst_y * 360.0f64 / SH_ROSE_YAWS),
           (libc_double_t)SH_ROSE_PITCH_DEG[worst_p], (libc_double_t)mhz, path);
    sh_rose_plot(sh, (const float32_t(*)[SH_ROSE_YAWS])total, most * 1.05f64);
    (void)snprintf(path, sizeof(path), "%s/rose-%s-r%" PRIu32 ".png", directory, place_name,
                   options->render_distance != 0u ? options->render_distance : (uint32_t)SH_DISTANCE);
    bool ok = sh_png_write(sh, sh->borrowed_plot, SH_ROSE_SIZE, SH_ROSE_SIZE, path);
    /* The worst direction, settled again, as a picture. */
    game->camera.yaw = (float32_t)((float64_t)worst_y * 360.0f64 / SH_ROSE_YAWS * SH_DEGREES);
    game->camera.pitch = (float32_t)(SH_ROSE_PITCH_DEG[worst_p] * SH_DEGREES);
    for (uint32_t warm = 0u; warm <= SH_ROSE_WARM; warm++) {
        ok = sh_settled_draw(sh, &scene, frame++, false) && ok;
    }
    (void)snprintf(path, sizeof(path), "%s/rose-%s-r%" PRIu32 "-worst.png", directory, place_name,
                   options->render_distance != 0u ? options->render_distance : (uint32_t)SH_DISTANCE);
    return sh_png_write(sh, sh->borrowed_picture, SH_WIDTH, SH_HEIGHT, path) && ok;
}

/* ---- One row ------------------------------------------------------------------------------------------------------- */

static const char *sh_mode_name(uint8_t mode) {
    return mode == SH_MODE_CAPPED ? "capped" : (mode == SH_MODE_UNCAPPED ? "uncapped" : "settled");
}

/* Opens a world at the place, settles it, and runs the row or the rose. */
static bool sh_run(sh_t *sh, const sh_row_t *row, const sh_options_t *options, sh_result_t *r) {
    app_t *const app = sh->borrowed_app;
    game_t *const game = &app->game;
    memset(r, 0, sizeof(*r));
    float64_t eye[3] = {0.0f64, 0.0f64, 0.0f64};
    if (options->has_at != 0u) {
        eye[0] = (float64_t)options->at[0] + 0.5f64;
        eye[1] = (float64_t)options->at[1] + 0.5f64;
        eye[2] = (float64_t)options->at[2] + SH_EYE_ABOVE_FEET;
    } else if (!sh_find_peaks(sh, eye)) {
        LOG_ERROR("bench_world_shake: no jagged peaks within the search");
        return false;
    }
    r->place[0] = (int32_t)floor(eye[0]);
    r->place[1] = (int32_t)floor(eye[1]);
    r->place[2] = (int32_t)floor(eye[2]);
    game_init(game, &app->world, &app->terrain, &app->render, &app->job, &app->texture, &app->topology);
    game_ui_set(game, &app->ui);
    const uint32_t distance = options->render_distance != 0u && row->render_distance == SH_DISTANCE
                                  ? options->render_distance
                                  : row->render_distance;
    const game_config_t config = {.render_distance = distance,
                                  .width = SH_WIDTH,
                                  .height = SH_HEIGHT,
                                  .features_off = (uint32_t)SHADE_FEATURE_DAY_CYCLE | options->features_off,
                                  .uncapped = row->mode == SH_MODE_UNCAPPED ? 1u : 0u};
    if (!game_open(game, &config)) {
        return false;
    }
    game_day_set(game, SH_DAY_MORNING);
    render_camera_start(&game->camera, eye[0], eye[1], eye[2], (float32_t)(options->yaw_deg * SH_DEGREES),
                        (float32_t)(SH_PITCH_DEG * SH_DEGREES));
    const render_fly_input_t still = {0};
    game_fly(game, &still, 0.0f32);
    bool ok = game_settle(game);
    if (ok && options->rose != 0u) {
        ok = sh_rose(sh, options, options->has_at != 0u ? "spot" : "jagged_peaks");
    } else if (ok) {
        ok = row->mode == SH_MODE_SETTLED ? sh_row_settled(sh, row, options->yaw_deg, r)
                                          : sh_row_live(sh, row, options->yaw_deg,
                                                        options->seconds != 0u ? options->seconds : SH_LIVE_SECONDS, r);
    }
    if (ok) {
        r->picture_hash = sh_hash(sh->borrowed_picture, (uint64_t)SH_WIDTH * SH_HEIGHT * 4u);
        r->sound = sh_picture_has_things(sh->borrowed_picture) && (r->pictures != 0u || options->rose != 0u) ? 1u : 0u;
        r->ran = 1u;
        if (options->shots != nullptr && options->rose == 0u) {
            char path[SH_PATH_BYTES];
            (void)snprintf(path, sizeof(path), "%s/%s-%s.png", options->shots,
                           options->has_at != 0u ? "spot" : "jagged_peaks", row->name);
            (void)sh_png_write(sh, sh->borrowed_picture, SH_WIDTH, SH_HEIGHT, path);
        }
    }
    game_report(game);
    game_close(game);
    return ok;
}

static void sh_row_print(const char *place, const sh_row_t *row, const sh_result_t *r) {
    if (r->ran == 0u) {
        printf("%s/%-26s FAILED: its process died, or the log says why\n", place, row->name);
        return;
    }
    printf("%s/%-26s %-8s interval p50 %.2f p99 %.2f worst %.2f ms; late %.1f %%; %.1f pictures/s; card mean %.2f p99 "
           "%.2f worst "
           "%.2f ms at %.0f MHz (%.0f to %.0f): %.2f Mcycles%s\n",
           place, row->name, sh_mode_name(row->mode), (libc_double_t)r->interval_p50_ms,
           (libc_double_t)r->interval_p99_ms, (libc_double_t)r->interval_worst_ms,
           (libc_double_t)(r->late_share * 100.0f64), (libc_double_t)r->pictures_per_s, (libc_double_t)r->card_mean_ms,
           (libc_double_t)r->card_p99_ms, (libc_double_t)r->card_worst_ms, (libc_double_t)r->card_mhz,
           (libc_double_t)r->card_mhz_low, (libc_double_t)r->card_mhz_high, (libc_double_t)r->card_mcycles,
           r->sound != 0u ? "" : "; NOT SOUND");
    printf("    passes coarse %.2f primary %.2f (far field %.2f) shade %.2f post %.2f readback %.2f; shade water %.2f "
           "surfaces "
           "%.2f sun %.2f ao %.2f fresh %.2f ms; fresh mean %.0f max %" PRIu32 "; P %.2f ms; wait for take %.2f ms\n",
           (libc_double_t)r->pass_ms[FRAME_PASS_COARSE], (libc_double_t)r->pass_ms[FRAME_PASS_PRIMARY],
           (libc_double_t)r->lod_ms, (libc_double_t)r->pass_ms[FRAME_PASS_SHADE],
           (libc_double_t)r->pass_ms[FRAME_PASS_POST], (libc_double_t)r->pass_ms[FRAME_PASS_READBACK],
           (libc_double_t)r->shade_ms[SHADE_SPAN_WATER], (libc_double_t)r->shade_ms[SHADE_SPAN_SURFACES],
           (libc_double_t)r->shade_ms[SHADE_SPAN_SUN], (libc_double_t)r->shade_ms[SHADE_SPAN_AO],
           (libc_double_t)r->shade_ms[SHADE_SPAN_FRESH], (libc_double_t)r->fresh_mean, r->fresh_max,
           (libc_double_t)r->p_ms, (libc_double_t)r->wait_take_ms);
    if (row->mode != SH_MODE_SETTLED) {
        printf("    pace: the card idle before a picture started at once (after a late one, or back to back) %.2f ms\n",
               (libc_double_t)r->wait_at_once_ms);
    }
    printf("    intervals by slot (8.33 ms each, last is all past):");
    for (uint32_t i = 0u; i < SH_BINS; i++) {
        printf(" %" PRIu32, r->bins[i]);
    }
    printf("; %" PRIu64 " pictures in %.1f s; picture %016" PRIX64 "\n", r->pictures, (libc_double_t)r->seconds,
           r->picture_hash);
    if (row->mode != SH_MODE_SETTLED) {
        printf("    trend, card ms / P ms / MHz by sixths:");
        for (uint32_t i = 0u; i < SH_TREND_PARTS; i++) {
            printf(" %.2f/%.2f/%.0f", (libc_double_t)r->trend_card_ms[i], (libc_double_t)r->trend_p_ms[i],
                   (libc_double_t)r->trend_mhz[i]);
        }
        printf("\n");
    }
}

/* ---- The history --------------------------------------------------------------------------------------------------- */

static const char *sh_preset_name(void) {
#if defined(MC_PROFILE)
    return "profile";
#elif defined(NDEBUG)
    return "release";
#else
    return "debug";
#endif
}

static bool sh_record(const sh_result_t *results, const sh_options_t *options) {
    FILE *const file = fopen(options->record, "a");
    if (file == nullptr) {
        LOG_ERROR("bench_world_shake: could not open %s to add to it", options->record);
        return false;
    }
    (void)fseek(file, 0, SEEK_END);
    if (ftell(file) == 0) {
        fprintf(
            file,
            "date\tlabel\tpreset\trow\tmode\tseed\tplace_x\tplace_y\tplace_z\tyaw_deg\trender_distance\tfeatures_off\t"
            "interval_p50_ms\tinterval_p99_ms\tinterval_worst_ms\tlate_share\tpictures_per_s\tcard_mean_ms\t"
            "card_p99_ms\tcard_worst_ms\tcoarse_ms\tprimary_ms\tlod_ms\tshade_ms\tpost_ms\treadback_ms\twater_ms\t"
            "surface_ms\tsun_ms\tao_ms\tao_fresh_ms\tfresh_mean\tfresh_max\tp_ms\tgpu_wait_take_ms\tcard_mhz\t"
            "card_mhz_low\tcard_mhz_high\tpictures\tseconds\tpicture\tsound\tcard_mcycles\n");
    }
    char date[32] = "";
    const time_t now = time(nullptr);
    struct tm parts;
    if (gmtime_r(&now, &parts) != nullptr) {
        (void)strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &parts);
    }
    const char *const place = options->has_at != 0u ? "spot" : "jagged_peaks";
    for (uint32_t i = 0u; i < SH_ROWS; i++) {
        const sh_row_t *const row = &SH_ROW_LIST[i];
        const sh_result_t *const r = &results[i];
        if (r->ran == 0u) {
            continue;
        }
        const uint32_t distance = options->render_distance != 0u && row->render_distance == SH_DISTANCE
                                      ? options->render_distance
                                      : row->render_distance;
        fprintf(
            file,
            "%s\t%s\t%s\t%s/%s\t%s\t%" PRIX64 "\t%" PRId32 "\t%" PRId32 "\t%" PRId32 "\t%.1f\t%" PRIu32 "\t%" PRIX32
            "\t%.3f\t%.3f\t%.3f\t%.4f\t%.2f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t"
            "%.3f\t%.3f\t%.0f\t%" PRIu32 "\t%.3f\t%.3f\t%.0f\t%.0f\t%.0f\t%" PRIu64 "\t%.2f\t%016" PRIX64
            "\t%u\t%.3f\n",
            date, options->label != nullptr ? options->label : "-", sh_preset_name(), place, row->name,
            sh_mode_name(row->mode), options->seed, r->place[0], r->place[1], r->place[2],
            (libc_double_t)options->yaw_deg, distance, options->features_off, (libc_double_t)r->interval_p50_ms,
            (libc_double_t)r->interval_p99_ms, (libc_double_t)r->interval_worst_ms, (libc_double_t)r->late_share,
            (libc_double_t)r->pictures_per_s, (libc_double_t)r->card_mean_ms, (libc_double_t)r->card_p99_ms,
            (libc_double_t)r->card_worst_ms, (libc_double_t)r->pass_ms[FRAME_PASS_COARSE],
            (libc_double_t)r->pass_ms[FRAME_PASS_PRIMARY], (libc_double_t)r->lod_ms,
            (libc_double_t)r->pass_ms[FRAME_PASS_SHADE], (libc_double_t)r->pass_ms[FRAME_PASS_POST],
            (libc_double_t)r->pass_ms[FRAME_PASS_READBACK], (libc_double_t)r->shade_ms[SHADE_SPAN_WATER],
            (libc_double_t)r->shade_ms[SHADE_SPAN_SURFACES], (libc_double_t)r->shade_ms[SHADE_SPAN_SUN],
            (libc_double_t)r->shade_ms[SHADE_SPAN_AO], (libc_double_t)r->shade_ms[SHADE_SPAN_FRESH],
            (libc_double_t)r->fresh_mean, r->fresh_max, (libc_double_t)r->p_ms, (libc_double_t)r->wait_take_ms,
            (libc_double_t)r->card_mhz, (libc_double_t)r->card_mhz_low, (libc_double_t)r->card_mhz_high, r->pictures,
            (libc_double_t)r->seconds, row->mode == SH_MODE_SETTLED ? r->picture_hash : 0u, (uint32_t)r->sound,
            (libc_double_t)r->card_mcycles);
    }
    const bool closed = fclose(file) == 0;
    if (!closed) {
        LOG_ERROR("bench_world_shake: could not finish writing %s", options->record);
    }
    return closed;
}

/* ---- Running it ---------------------------------------------------------------------------------------------------- */

static void sh_usage(const char *program) {
    fprintf(
        stderr,
        "usage: %s [--only <text>] [--at <x> <y> <z>] [--seed <n>] [--yaw <degrees>] [--render-distance <chunks>]\n"
        "          [--features-off <hex>] [--seconds <n>] [--record <file>] [--label <text>] [--shots <dir>] [--rose] "
        "[--list]\n"
        "  --only <text>     only the rows whose name holds this, or =<name>,<name>... for just those\n"
        "  --at <x> <y> <z>  the block the feet are in, as F3 says, in place of the nearest jagged peaks\n"
        "  --seed <n>        the world's seed, as a save's state file has it (hex with 0x, else decimal)\n"
        "  --yaw <degrees>   the heading the shake swings about, and the rose's eye; 0 looks east, 90 north\n"
        "  --render-distance <chunks>  the rows at 32 at this many in place\n"
        "  --features-off <hex>  take these SHADE_FEATURE_* bits away (AO 0x400, TAA as the header says)\n"
        "  --seconds <n>     a live row's length: 10 unless this\n"
        "  --record <file>   add a line per row: bench/world/shake/history.tsv is the repository's\n"
        "  --shots <dir>     the last picture of each row, or the rose's CSV, plot and worst direction, here\n"
        "  --rose            the cost rose in place of the rows: 72 yaws by 3 pitches, settled\n"
        "  --list            print the rows, and do nothing\n",
        program);
}

static bool sh_parse(int32_t argc, char **argv, sh_options_t *out) {
    memset(out, 0, sizeof(*out));
    out->seed = TERRAIN_SEED_DEFAULT;
    for (int32_t i = 1; i < argc; i++) {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--list") == 0) {
            out->list = 1u;
        } else if (strcmp(argv[i], "--rose") == 0) {
            out->rose = 1u;
        } else if (strcmp(argv[i], "--only") == 0 && has_value) {
            out->only = argv[++i];
        } else if (strcmp(argv[i], "--record") == 0 && has_value) {
            out->record = argv[++i];
        } else if (strcmp(argv[i], "--label") == 0 && has_value) {
            out->label = argv[++i];
        } else if (strcmp(argv[i], "--shots") == 0 && has_value) {
            out->shots = argv[++i];
        } else if (strcmp(argv[i], "--seed") == 0 && has_value) {
            out->seed = strtoull(argv[++i], nullptr, 0);
        } else if (strcmp(argv[i], "--yaw") == 0 && has_value) {
            out->yaw_deg = strtod(argv[++i], nullptr);
        } else if (strcmp(argv[i], "--seconds") == 0 && has_value) {
            out->seconds = (uint32_t)strtoul(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--features-off") == 0 && has_value) {
            out->features_off = (uint32_t)strtoul(argv[++i], nullptr, 16);
        } else if (strcmp(argv[i], "--render-distance") == 0 && has_value) {
            const uint64_t chunks = strtoull(argv[++i], nullptr, 10);
            if (chunks < 2u || chunks > (uint64_t)MC_GAME_RENDER_DISTANCE_MAX) {
                return false;
            }
            out->render_distance = (uint32_t)chunks;
        } else if (strcmp(argv[i], "--at") == 0 && i + 3 < argc) {
            for (uint32_t k = 0u; k < 3u; k++) {
                out->at[k] = (int32_t)strtol(argv[++i], nullptr, 10);
            }
            out->has_at = 1u;
        } else {
            return false;
        }
    }
    return out->label == nullptr || strchr(out->label, '\t') == nullptr;
}

/* With =, the names after it, split by commas, each whole. */
static bool sh_chosen(uint32_t row, const char *only) {
    if (only != nullptr && only[0] == '=') {
        const char *const name = SH_ROW_LIST[row].name;
        const uint64_t length = strlen(name);
        for (const char *at = only + 1; *at != '\0';) {
            const char *const comma = strchr(at, ',');
            const uint64_t token = comma != nullptr ? (uint64_t)(comma - at) : strlen(at);
            if (token == length && strncmp(at, name, length) == 0) {
                return true;
            }
            at += token + (comma != nullptr ? 1u : 0u);
        }
        return false;
    }
    return only == nullptr || strstr(SH_ROW_LIST[row].name, only) != nullptr;
}

static uint64_t sh_page(uint64_t bytes) {
    return (bytes + 4095ull) & ~4095ull;
}

static bool sh_memory(sh_t *sh) {
    const uint64_t png_bytes = display_png_bytes_needed(SH_WIDTH, SH_HEIGHT);
    const uint64_t sizes[] = {
        sh_page(sizeof(app_t)),
        sh_page((uint64_t)SH_SAMPLES * sizeof(sh_sample_t)),
        sh_page((uint64_t)SH_SAMPLES * sizeof(float32_t)),
        sh_page((uint64_t)SH_COLUMN_VOXELS * sizeof(uint16_t)),
        sh_page(SH_COLUMN_VOXELS),
        sh_page((uint64_t)SH_WIDTH * SH_HEIGHT * 4u),
        sh_page(png_bytes),
        sh_page((uint64_t)SH_ROSE_SIZE * SH_ROSE_SIZE * 4u),
    };
    uint64_t total = 0u;
    for (uint32_t i = 0u; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        total += sizes[i];
    }
    if (png_bytes == 0u || !heap_reserve(total, &sh->owned_memory)) {
        return false;
    }
    uint8_t *at = sh->owned_memory.owned_base;
    sh->borrowed_app = (app_t *)at;
    at += sizes[0];
    sh->borrowed_samples = (sh_sample_t *)at;
    at += sizes[1];
    sh->borrowed_sort = (float32_t *)at;
    at += sizes[2];
    sh->borrowed_states = (uint16_t *)at;
    at += sizes[3];
    sh->borrowed_opened = at;
    at += sizes[4];
    sh->borrowed_picture = at;
    at += sizes[5];
    sh->borrowed_png = at;
    at += sizes[6];
    sh->borrowed_plot = at;
    sh->png_bytes = png_bytes;
    memset(sh->borrowed_app, 0, sizeof(app_t));
    return true;
}

/* One row, or the rose, in this process, which holds nothing yet. Returns the exit status. */
static int32_t sh_process(const sh_options_t *options, uint32_t row, bool say_card, sh_result_t *result) {
    sh_t sh;
    memset(&sh, 0, sizeof(sh));
    if (!sh_memory(&sh)) {
        return 2;
    }
    int32_t status = 2;
    if (sh_start(&sh, options->seed)) {
        if (say_card) {
            const device_memory_t *const memory = &sh.borrowed_app->gpu_bridge.device_memory;
            printf("build %s, seed %" PRIX64 ", %ux%u, card %s\n", sh_preset_name(), options->seed, SH_WIDTH, SH_HEIGHT,
                   memory->device_name);
            (void)fflush(stdout);
        }
        const bool ran = sh_run(&sh, &SH_ROW_LIST[row], options, result);
        status = ran && result->sound != 0u ? 0 : 1;
    }
    const bool gpu_clean = sh.started < 5u || gpu_bridge_exit_check(&sh.borrowed_app->gpu_bridge);
    sh_stop(&sh);
    heap_release(&sh.owned_memory);
    return gpu_clean && heap_exit_check() ? status : 1;
}

/* One row in a process of its own, as bench_world does: a row that dies costs that row, and no world reaches the next. */
static int32_t sh_forked(const sh_options_t *options, uint32_t row, bool say_card, sh_result_t *results) {
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
        const int32_t status = sh_process(options, row, say_card, &results[row]);
        const bool sent = write(ends[1], &results[row], sizeof(results[row])) == (int64_t)sizeof(results[row]);
        (void)fflush(stdout);
        (void)fflush(stderr);
        _exit(sent ? status : 1);
    }
    (void)close(ends[1]);
    sh_result_t received;
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
    sh_options_t options;
    if (!sh_parse(argc, argv, &options)) {
        sh_usage(argv[0]);
        return 2;
    }
    const char *const place = options.has_at != 0u ? "spot" : "jagged_peaks";
    if (options.list != 0u) {
        for (uint32_t i = 0u; i < SH_ROWS; i++) {
            printf("%s/%-26s %-8s %4.0f deg/s, render distance %" PRIu32 "%s\n", place, SH_ROW_LIST[i].name,
                   sh_mode_name(SH_ROW_LIST[i].mode), (libc_double_t)SH_ROW_LIST[i].speed_deg_s,
                   SH_ROW_LIST[i].render_distance, SH_ROW_LIST[i].pitch != 0u ? ", pitch" : "");
        }
        return 0;
    }
    if (options.shots != nullptr && mkdir(options.shots, 0755) != 0 && access(options.shots, W_OK) != 0) {
        fprintf(stderr, "could not make or write to %s\n", options.shots);
        return 2;
    }
    sh_result_t results[SH_ROWS];
    memset(results, 0, sizeof(results));
    const int64_t run_ns = time_now_ns();
    if (options.rose != 0u) {
        const int32_t status = sh_forked(&options, 0u, true, results);
        printf("the rose took %.1f s\n", (libc_double_t)(time_ns_to_ms(time_now_ns() - run_ns) / 1000.0f64));
        return status;
    }
    int32_t status = 0;
    bool said = false;
    for (uint32_t i = 0u; i < SH_ROWS; i++) {
        if (!sh_chosen(i, options.only)) {
            continue;
        }
        const int32_t row_status = sh_forked(&options, i, !said, results);
        said = true;
        status = row_status > status ? row_status : status;
        sh_row_print(place, &SH_ROW_LIST[i], &results[i]);
        (void)fflush(stdout);
    }
    if (!said) {
        fprintf(stderr, "no row's name holds '%s': --list prints them\n", options.only);
        return 2;
    }
    printf("the whole run took %.1f s\n", (libc_double_t)(time_ns_to_ms(time_now_ns() - run_ns) / 1000.0f64));
    if (options.record != nullptr && !sh_record(results, &options)) {
        status = status > 1 ? status : 1;
    }
    return status;
}
