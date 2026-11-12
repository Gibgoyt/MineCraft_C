/* bench_biome_vs_class: plan 006 phase 03's survey and pictures of biomes against the climate. See README.md.
 *
 * For each seed, every land cell of a strip from x = 0 to the strip's width and pole to pole (as bench_climate_survey's
 * phase A): its climate class, and the biome the terrain gives the column at its centre, on real climate from the tiles.
 * Two measures per class: plan 004 phase 11's (a desert or badlands biome outside Desert and Steppe, or another biome in
 * them) and plan 006's (the biome is not a member of the family the class names). Then, for the first seeds, pictures: a
 * 16 km square round the equator and a 4 km strip from the equator to the north pole, each as biomes, classes and the
 * mismatch. The tables go to stdout and are the same bytes on 1 worker or 16; times go to stderr. */
#include "climate/index.h"
#include "display/headless/png/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "tables/biomes/index.h"
#include "terrain/index.h"
#include "terrain/rng/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum : uint32_t {
    VS_SEEDS_MAX = 8u,
    VS_STRIP_BLOCKS = 16384u, /* the survey strip's width, as bench_climate_survey's */
    VS_STRIP_BLOCKS_MAX = 32768u,
    VS_BAND_BLOCKS = 16384u,  /* rows built at a time: inside the climate's 64 km keep square */
    VS_MARGIN_BLOCKS = 1024u, /* tiles built round a band: past a read's 112-block reach */
    VS_STEP = 16u,            /* a picture's pixel, in blocks */
    VS_SQUARE_BLOCKS = 16384u,
    VS_SQUARE_SIDE = VS_SQUARE_BLOCKS / VS_STEP,
    VS_STRIP_PICTURE_BLOCKS = 4096u, /* the equator-to-pole strip's width */
    VS_STRIP_SIDE = VS_STRIP_PICTURE_BLOCKS / VS_STEP,
    VS_PICTURE_SEEDS = 3u,
    VS_PATH_BYTES = 4096u,
    VS_CLASSES = CLIMATE_CLASS_COUNT,
    VS_MEASURES =
        5u, /* land cells, plan 004's disagree, plan 006's, column family not the class's, anywhere in the cell */
    VS_TARGET_PERCENT = 5u,
};
/* plan 003 phase 8 stage 1's eight seeds, as bench_climate_survey's: Ahmed's first. */
static const uint64_t VS_SEEDS[VS_SEEDS_MAX] = {
    2198175988018515243ull, 12636146071196537873ull, 5569068473609503046ull, 1ull, 42ull, 20261104ull,
    9999999999999ull,       18446744073709551557ull};
static const char *const VS_CLASS_NAMES[VS_CLASSES] = {"Sea",           "Ice",        "Tundra",      "Desert",
                                                       "Steppe",        "Rainforest", "Monsoon",     "Savanna",
                                                       "Mediterranean", "Temperate",  "Continental", "Boreal"};
/* Plan 004 phase 05's class colours (ui/atlas/layer), and the sea. */
static const uint32_t VS_CLASS_COLOUR[VS_CLASSES] = {0x183C78u, 0xFFFFFFu, 0xB0B8C0u, 0xF0D080u, 0xD0B060u, 0x006020u,
                                                     0x208040u, 0xA0C040u, 0xE0A040u, 0x60B060u, 0x6080C0u, 0x305060u};
static const uint32_t VS_MISMATCH = 0xE02020u;
static const uint32_t VS_MATCH = 0xC8C8C8u;
static const uint32_t VS_WATER = 0x304060u;

/* What each biome looks like, as bench_biome_map has it. */
static const uint32_t VS_BIOME_COLOUR[BIOME_COUNT] = {
    [BIOME_ID_BADLANDS] = 0xC8642Au,
    [BIOME_ID_BAMBOO_JUNGLE] = 0x38A818u,
    [BIOME_ID_BEACH] = 0xF0E4A0u,
    [BIOME_ID_BIRCH_FOREST] = 0x60B060u,
    [BIOME_ID_CHERRY_GROVE] = 0xF0A0C8u,
    [BIOME_ID_COLD_OCEAN] = 0x2850A0u,
    [BIOME_ID_DARK_FOREST] = 0x205020u,
    [BIOME_ID_DEEP_COLD_OCEAN] = 0x183468u,
    [BIOME_ID_DEEP_FROZEN_OCEAN] = 0x5078A8u,
    [BIOME_ID_DEEP_LUKEWARM_OCEAN] = 0x105878u,
    [BIOME_ID_DEEP_OCEAN] = 0x102860u,
    [BIOME_ID_DESERT] = 0xE8D478u,
    [BIOME_ID_ERODED_BADLANDS] = 0xE04818u,
    [BIOME_ID_FLOWER_FOREST] = 0x88C040u,
    [BIOME_ID_FOREST] = 0x388038u,
    [BIOME_ID_FROZEN_OCEAN] = 0x90B8E0u,
    [BIOME_ID_FROZEN_PEAKS] = 0xE0F0FFu,
    [BIOME_ID_FROZEN_RIVER] = 0xA0D8F0u,
    [BIOME_ID_GROVE] = 0x90B0A0u,
    [BIOME_ID_ICE_SPIKES] = 0xB0E8F8u,
    [BIOME_ID_JAGGED_PEAKS] = 0xF8F8F8u,
    [BIOME_ID_JUNGLE] = 0x208008u,
    [BIOME_ID_LUKEWARM_OCEAN] = 0x1880A8u,
    [BIOME_ID_MANGROVE_SWAMP] = 0x487038u,
    [BIOME_ID_MEADOW] = 0xA8D880u,
    [BIOME_ID_MUSHROOM_FIELDS] = 0xFF00FFu,
    [BIOME_ID_OCEAN] = 0x1840A0u,
    [BIOME_ID_OLD_GROWTH_BIRCH_FOREST] = 0x489848u,
    [BIOME_ID_OLD_GROWTH_PINE_TAIGA] = 0x386048u,
    [BIOME_ID_OLD_GROWTH_SPRUCE_TAIGA] = 0x305840u,
    [BIOME_ID_PLAINS] = 0x90C860u,
    [BIOME_ID_RIVER] = 0x20C8E8u,
    [BIOME_ID_SAVANNA] = 0xB8B048u,
    [BIOME_ID_SAVANNA_PLATEAU] = 0xA09838u,
    [BIOME_ID_SNOWY_BEACH] = 0xE8E8D8u,
    [BIOME_ID_SNOWY_PLAINS] = 0xD8E8E8u,
    [BIOME_ID_SNOWY_SLOPES] = 0xC8D8E8u,
    [BIOME_ID_SNOWY_TAIGA] = 0xA8C0C0u,
    [BIOME_ID_SPARSE_JUNGLE] = 0x58A030u,
    [BIOME_ID_STONY_PEAKS] = 0x888888u,
    [BIOME_ID_STONY_SHORE] = 0x686868u,
    [BIOME_ID_SUNFLOWER_PLAINS] = 0xC0D850u,
    [BIOME_ID_SWAMP] = 0x506830u,
    [BIOME_ID_TAIGA] = 0x487860u,
    [BIOME_ID_WARM_OCEAN] = 0x20A8B8u,
    [BIOME_ID_WINDSWEPT_FOREST] = 0x608870u,
    [BIOME_ID_WINDSWEPT_GRAVELLY_HILLS] = 0x909890u,
    [BIOME_ID_WINDSWEPT_HILLS] = 0x789880u,
    [BIOME_ID_WINDSWEPT_SAVANNA] = 0x989860u,
    [BIOME_ID_WOODED_BADLANDS] = 0x987038u,
};

/* One place, as both measures and the pictures see it. */
typedef struct vs_place_t {
    uint8_t klass;    /* 0: the cell's CLIMATE_CLASS_*, CLIMATE_CLASS_NONE for sea and large lakes */
    uint8_t biome;    /* 1 */
    uint8_t family;   /* 2: the column's own family */
    uint8_t missing;  /* 3: 1 if its tile was not built: a bug */
    uint8_t inside;   /* 4: the survey's: the biome at a place in the cell picked by hash */
    uint8_t _pad0[3]; /* 5 */
    float32_t ground; /* 8 */
} vs_place_t;
static_assert(sizeof(vs_place_t) == 12);

/* A row's job: count_x places from (x0, y) step blocks apart into out. */
typedef struct vs_row_job_t {
    const climate_t *borrowed_climate; /* 0 */
    const terrain_t *borrowed_terrain; /* 8 */
    vs_place_t *borrowed_out;          /* 16 */
    int32_t x0;                        /* 24 */
    int32_t y;                         /* 28 */
    uint32_t step;                     /* 32 */
    uint32_t count_x;                  /* 36 */
} vs_row_job_t;
static_assert(sizeof(vs_row_job_t) == 40);

typedef struct vs_options_t {
    const char *pictures;   /* 0: --pictures: the directory, or nullptr for none */
    uint32_t first;         /* 8 */
    uint32_t seeds;         /* 12 */
    uint32_t workers;       /* 16 */
    uint32_t width;         /* 20 */
    uint32_t picture_seeds; /* 24 */
    uint8_t _pad0[4];       /* 28 */
} vs_options_t;
static_assert(sizeof(vs_options_t) == 32);

/* Every count, by seed, class and measure. */
typedef struct vs_totals_t {
    uint64_t counts[VS_SEEDS_MAX][VS_CLASSES][VS_MEASURES]; /* 0 */
    uint64_t wrong[VS_CLASSES][BIOME_COUNT];                /* the biomes of the disagreeing cells, pooled */
} vs_totals_t;
static_assert(sizeof(vs_totals_t) == 8u * VS_SEEDS_MAX * VS_CLASSES * VS_MEASURES + 8u * VS_CLASSES * BIOME_COUNT);

static bool vs_desert_biome(uint8_t biome) {
    return biome == BIOME_ID_DESERT || biome == BIOME_ID_BADLANDS || biome == BIOME_ID_ERODED_BADLANDS ||
           biome == BIOME_ID_WOODED_BADLANDS;
}

/* Plan 006's measure: the biome is not a member of the family the class names. */
static bool vs_strict_disagrees(uint8_t klass, uint8_t biome) {
    const uint8_t family = terrain_biome_family_of_class(klass);
    return family >= BIOME_FAMILY_COUNT || (biome_defs[biome].families & (1u << family)) == 0u;
}

static void vs_row(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    const vs_row_job_t *const job = borrowed_argument;
    for (uint32_t i = 0u; i < job->count_x; i++) {
        const int32_t x = job->x0 + (int32_t)(i * job->step);
        vs_place_t *const out = &job->borrowed_out[i];
        memset(out, 0, sizeof(*out));
        climate_cell_t cell;
        if (!climate_cell_get(job->borrowed_climate, x, job->y, &cell)) {
            out->missing = 1u;
            continue;
        }
        const bool water = (cell.flags & (CLIMATE_CELL_SEA | CLIMATE_CELL_LAKE)) != 0u;
        out->klass = water ? (uint8_t)CLIMATE_CLASS_NONE : cell.climate_class;
        terrain_climate_sample_t climate;
        const bool built = climate_terrain_sample(job->borrowed_climate, job->borrowed_terrain->seed, (int64_t)x - 8,
                                                  (int64_t)job->y - 8, &climate);
        out->missing = (uint8_t)(built ? 0u : 1u);
        const terrain_biome_column_t column =
            terrain_biome_column_at(job->borrowed_terrain->borrowed_biome, &climate, x, job->y);
        out->biome = column.biome;
        out->family = column.family;
        out->ground = column.ground;
        if (job->step == (uint32_t)CLIMATE_CELL_BLOCKS) {
            /* The survey's second look: anywhere in the cell, not only its centre, where the family's borders wobble. */
            const uint64_t hash = terrain_rng_hash2(job->borrowed_terrain->seed ^ 0x1A51DEull, x, job->y);
            const int32_t in_x = x + (int32_t)(hash & 63u) - CLIMATE_HALF_CELL_BLOCKS;
            const int32_t in_y = job->y + (int32_t)((hash >> 8u) & 63u) - CLIMATE_HALF_CELL_BLOCKS;
            out->inside = terrain_biome_column_at(job->borrowed_terrain->borrowed_biome, &climate, in_x, in_y).biome;
        }
    }
}

static void vs_run_jobs(job_system_t *job, vs_row_job_t *jobs, uint32_t count) {
    job_group_t group;
    job_group_init(&group);
    for (uint32_t k = 0u; k < count; k++) {
        job_submit(job, &group, vs_row, &jobs[k]);
    }
    job_group_wait(job, &group);
    job_group_deinit(&group);
}

/* Every place of an area, rows south to north, built a band at a time. out holds count_x * count_y places. */
static uint32_t vs_area(job_system_t *job, climate_t *climate, const terrain_t *terrain, int32_t x0, int32_t y0,
                        uint32_t step, uint32_t count_x, uint32_t count_y, vs_row_job_t *jobs, vs_place_t *out) {
    const uint32_t band_rows = VS_BAND_BLOCKS / step;
    uint32_t missing = 0u;
    for (uint32_t row0 = 0u; row0 < count_y; row0 += band_rows) {
        const uint32_t row1 = row0 + band_rows < count_y ? row0 + band_rows : count_y;
        const int32_t y_low = y0 + (int32_t)(row0 * step) - (int32_t)VS_MARGIN_BLOCKS;
        const int32_t y_high = y0 + (int32_t)(row1 * step) + (int32_t)VS_MARGIN_BLOCKS;
        climate_prepare_area(climate, job, x0 - (int32_t)VS_MARGIN_BLOCKS, y_low,
                             x0 + (int32_t)(count_x * step) + (int32_t)VS_MARGIN_BLOCKS, y_high, nullptr, nullptr);
        for (uint32_t r = row0; r < row1; r++) {
            jobs[r - row0] = (vs_row_job_t){.borrowed_climate = climate,
                                            .borrowed_terrain = terrain,
                                            .borrowed_out = out + (uint64_t)r * count_x,
                                            .x0 = x0,
                                            .y = y0 + (int32_t)(r * step),
                                            .step = step,
                                            .count_x = count_x};
        }
        vs_run_jobs(job, jobs, row1 - row0);
    }
    for (uint64_t i = 0u; i < (uint64_t)count_x * count_y; i++) {
        missing += out[i].missing;
    }
    return missing;
}

/* ---- Pictures -------------------------------------------------------------------------------------------------------- */

static uint32_t vs_shade(uint32_t colour, float32_t light) {
    uint32_t out = 0u;
    for (uint32_t shift = 0u; shift <= 16u; shift += 8u) {
        float32_t channel = (float32_t)((colour >> shift) & 0xFFu) * light;
        channel = channel > 255.0f32 ? 255.0f32 : channel;
        out |= (uint32_t)channel << shift;
    }
    return out;
}

/* Three panels side by side, north at the top: biomes lit from the north-west, classes, and the mismatch. */
static void vs_draw(const vs_place_t *places, uint32_t count_x, uint32_t count_y, uint32_t *pixels) {
    const uint32_t pitch = 3u * count_x;
    for (uint32_t r = 0u; r < count_y; r++) {
        const uint32_t y = count_y - 1u - r; /* row r of the picture is the place row count_y - 1 - r */
        for (uint32_t x = 0u; x < count_x; x++) {
            const vs_place_t *const place = &places[(uint64_t)y * count_x + x];
            float32_t light = 1.0f32;
            if (x > 0u && y + 1u < count_y) {
                const float32_t rise = place->ground - places[(uint64_t)(y + 1u) * count_x + x - 1u].ground;
                light = 1.0f32 + rise * 0.02f32;
                light = light < 0.55f32 ? 0.55f32 : (light > 1.3f32 ? 1.3f32 : light);
            }
            const bool land = place->klass != CLIMATE_CLASS_NONE;
            uint32_t *const row = pixels + (uint64_t)r * pitch;
            row[x] = vs_shade(VS_BIOME_COLOUR[place->biome], light);
            row[count_x + x] = VS_CLASS_COLOUR[place->klass < VS_CLASSES ? place->klass : 0u];
            row[2u * count_x + x] =
                !land ? VS_WATER : (vs_strict_disagrees(place->klass, place->biome) ? VS_MISMATCH : VS_MATCH);
        }
    }
}

static bool vs_png_write(const char *path, const uint32_t *pixels, uint32_t width, uint32_t height, uint8_t *stream,
                         uint64_t stream_bytes) {
    uint64_t bytes = 0u;
    if (!display_png_encode((const uint8_t *)pixels, width * 4u, width, height, stream, stream_bytes, &bytes)) {
        fprintf(stderr, "bench_biome_vs_class: cannot encode %s\n", path);
        return false;
    }
    FILE *const file = fopen(path, "wb");
    if (file == nullptr) {
        fprintf(stderr, "bench_biome_vs_class: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    const bool wrote = fwrite(stream, 1u, bytes, file) == bytes;
    return fclose(file) == 0 && wrote;
}

/* ---- The tables ------------------------------------------------------------------------------------------------------ */

static float64_t vs_percent(uint64_t part, uint64_t whole) {
    return whole == 0u ? 0.0f64 : (float64_t)part * 100.0f64 / (float64_t)whole;
}

/* Prints the per-class table; returns whether every class with land is under the target. */
static bool vs_print(const vs_totals_t *all, const vs_options_t *options) {
    const uint64_t (*const totals)[VS_CLASSES][VS_MEASURES] = all->counts;
    printf("## Biomes against the climate class, %u seeds, strip %u blocks wide, pole to pole\n\n", options->seeds,
           options->width);
    printf(
        "| Class | Land cells | Share of land | Plan 004 disagree | Plan 006 disagree (target < %u%%) | Worst seed | "
        "Column family is not the class's | Plan 006 anywhere in the cell |\n",
        (uint32_t)VS_TARGET_PERCENT);
    printf("|---|---|---|---|---|---|---|---|\n");
    uint64_t land = 0u;
    for (uint32_t s = 0u; s < options->seeds; s++) {
        for (uint32_t c = 1u; c < VS_CLASSES; c++) {
            land += totals[s][c][0];
        }
    }
    bool met = true;
    for (uint32_t c = 1u; c < VS_CLASSES; c++) {
        uint64_t sum[VS_MEASURES] = {0u};
        float64_t worst = 0.0f64;
        for (uint32_t s = 0u; s < options->seeds; s++) {
            for (uint32_t m = 0u; m < VS_MEASURES; m++) {
                sum[m] += totals[s][c][m];
            }
            worst = fmax(worst, vs_percent(totals[s][c][2], totals[s][c][0]));
        }
        const float64_t strict = vs_percent(sum[2], sum[0]);
        met = met && (sum[0] == 0u || strict < (float64_t)VS_TARGET_PERCENT);
        printf("| %s | %" PRIu64 " | %.2f%% | %.1f%% | %.2f%%%s | %.2f%% | %.1f%% | %.1f%% |\n", VS_CLASS_NAMES[c],
               sum[0], (libc_double_t)vs_percent(sum[0], land), (libc_double_t)vs_percent(sum[1], sum[0]),
               (libc_double_t)strict, sum[0] != 0u && strict >= (float64_t)VS_TARGET_PERCENT ? " NOT MET" : "",
               (libc_double_t)worst, (libc_double_t)vs_percent(sum[3], sum[0]),
               (libc_double_t)vs_percent(sum[4], sum[0]));
    }
    printf("\nPlan 006's measure %s in every class.\n", met ? "is under 5%" : "is NOT under 5%");
    printf("\nThe commonest biomes where it disagrees, by class:\n\n");
    for (uint32_t c = 1u; c < VS_CLASSES; c++) {
        uint64_t left[BIOME_COUNT];
        memcpy(left, all->wrong[c], sizeof(left));
        printf("- %s:", VS_CLASS_NAMES[c]);
        for (uint32_t rank = 0u; rank < 4u; rank++) {
            uint32_t best = 0u;
            for (uint32_t b = 1u; b < BIOME_COUNT; b++) {
                best = left[b] > left[best] ? b : best;
            }
            if (left[best] == 0u) {
                break;
            }
            printf(" %s %" PRIu64, biome_defs[best].name, left[best]);
            left[best] = 0u;
        }
        printf("\n");
    }
    return met;
}

static bool vs_options_read(int32_t argc, char **argv, vs_options_t *out) {
    *out = (vs_options_t){.seeds = VS_SEEDS_MAX,
                          .workers = MC_THREADS_WORKERS,
                          .width = VS_STRIP_BLOCKS,
                          .picture_seeds = VS_PICTURE_SEEDS};
    for (int32_t i = 1; i < argc; i++) {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--pictures") == 0 && has_value) {
            out->pictures = argv[++i];
            continue;
        }
        uint32_t *const field = strcmp(argv[i], "--seeds") == 0           ? &out->seeds
                                : strcmp(argv[i], "--first") == 0         ? &out->first
                                : strcmp(argv[i], "--workers") == 0       ? &out->workers
                                : strcmp(argv[i], "--width") == 0         ? &out->width
                                : strcmp(argv[i], "--picture-seeds") == 0 ? &out->picture_seeds
                                                                          : nullptr;
        if (field == nullptr || !has_value) {
            return false;
        }
        *field = (uint32_t)strtoul(argv[++i], nullptr, 10);
    }
    return out->seeds >= 1u && out->first + out->seeds <= VS_SEEDS_MAX && out->workers >= 1u &&
           out->workers <= MC_THREADS_WORKERS && out->width >= (uint32_t)CLIMATE_CELL_BLOCKS &&
           out->width <= VS_STRIP_BLOCKS_MAX && out->width % (uint32_t)CLIMATE_CELL_BLOCKS == 0u;
}

/* The next piece of the region, a whole number of cache lines. */
static void *vs_take(uint8_t **cursor, uint64_t bytes) {
    void *const at = *cursor;
    *cursor += (bytes + MC_CPU_CACHE_LINE_BYTES - 1u) / MC_CPU_CACHE_LINE_BYTES * MC_CPU_CACHE_LINE_BYTES;
    return at;
}

/* Everything this program keeps, in one region. */
typedef struct vs_memory_t {
    vs_row_job_t *jobs;    /* 0: a band's rows */
    vs_place_t *places;    /* 8: the biggest area */
    uint32_t *pixels;      /* 16 */
    uint8_t *stream;       /* 24 */
    uint64_t stream_bytes; /* 32 */
    climate_t *climate;    /* 40 */
    terrain_t *terrain;    /* 48 */
    job_system_t *job;     /* 56 */
} vs_memory_t;
static_assert(sizeof(vs_memory_t) == 64);

/* The pictures of one seed: the square round the equator and the strip to the north pole. */
static bool vs_pictures(const vs_options_t *options, const vs_memory_t *memory, uint64_t seed) {
    char path[VS_PATH_BYTES];
    bool ok = true;
    uint32_t missing = vs_area(memory->job, memory->climate, memory->terrain, 0, -(int32_t)(VS_SQUARE_BLOCKS / 2u),
                               VS_STEP, VS_SQUARE_SIDE, VS_SQUARE_SIDE, memory->jobs, memory->places);
    vs_draw(memory->places, VS_SQUARE_SIDE, VS_SQUARE_SIDE, memory->pixels);
    snprintf(path, sizeof(path), "%s/%" PRIu64 "_square.png", options->pictures, seed);
    ok =
        vs_png_write(path, memory->pixels, 3u * VS_SQUARE_SIDE, VS_SQUARE_SIDE, memory->stream, memory->stream_bytes) &&
        ok;
    fprintf(stderr, "bench_biome_vs_class: %s\n", path);
    const uint32_t strip_rows = (uint32_t)(CLIMATE_POLE_Y / (float64_t)VS_STEP);
    missing += vs_area(memory->job, memory->climate, memory->terrain, 0, 0, VS_STEP, VS_STRIP_SIDE, strip_rows,
                       memory->jobs, memory->places);
    vs_draw(memory->places, VS_STRIP_SIDE, strip_rows, memory->pixels);
    snprintf(path, sizeof(path), "%s/%" PRIu64 "_strip.png", options->pictures, seed);
    ok = vs_png_write(path, memory->pixels, 3u * VS_STRIP_SIDE, strip_rows, memory->stream, memory->stream_bytes) && ok;
    fprintf(stderr, "bench_biome_vs_class: %s\n", path);
    return ok && missing == 0u;
}

int32_t main(int32_t argc, char **argv) {
    vs_options_t options;
    if (!vs_options_read(argc, argv, &options)) {
        fprintf(stderr,
                "usage: %s [--first 0-7] [--seeds 1-8] [--workers 1-%u] [--width blocks] [--pictures directory] "
                "[--picture-seeds N]\n",
                argv[0], (uint32_t)MC_THREADS_WORKERS);
        return 2;
    }
    if (options.pictures != nullptr && mkdir(options.pictures, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bench_biome_vs_class: cannot make %s: %s\n", options.pictures, strerror(errno));
        return 2;
    }
    const int64_t started = time_now_ns();
    /* The rows of cells whose centres lie between the poles, as bench_climate_survey has them. */
    const int32_t row_min =
        (int32_t)ceil((-CLIMATE_POLE_Y - (float64_t)CLIMATE_HALF_CELL_BLOCKS) / CLIMATE_CELL_BLOCKS);
    const int32_t row_max =
        (int32_t)floor((CLIMATE_POLE_Y - (float64_t)CLIMATE_HALF_CELL_BLOCKS) / CLIMATE_CELL_BLOCKS);
    const uint32_t rows = (uint32_t)(row_max - row_min + 1);
    const uint32_t columns = options.width / (uint32_t)CLIMATE_CELL_BLOCKS;
    const uint32_t strip_rows = (uint32_t)(CLIMATE_POLE_Y / (float64_t)VS_STEP);
    uint64_t places = (uint64_t)rows * columns;
    places = places > (uint64_t)VS_SQUARE_SIDE * VS_SQUARE_SIDE ? places : (uint64_t)VS_SQUARE_SIDE * VS_SQUARE_SIDE;
    places = places > (uint64_t)VS_STRIP_SIDE * strip_rows ? places : (uint64_t)VS_STRIP_SIDE * strip_rows;
    const uint64_t pixels = 3u * places;
    const uint64_t stream_bytes = display_png_bytes_needed(3u * VS_SQUARE_SIDE, VS_SQUARE_SIDE) >
                                          display_png_bytes_needed(3u * VS_STRIP_SIDE, strip_rows)
                                      ? display_png_bytes_needed(3u * VS_SQUARE_SIDE, VS_SQUARE_SIDE)
                                      : display_png_bytes_needed(3u * VS_STRIP_SIDE, strip_rows);
    const uint64_t band_rows = VS_BAND_BLOCKS / VS_STEP; /* the most rows of any area's band: the pictures' */
    const uint64_t bytes = sizeof(thread_topology_t) + sizeof(job_system_t) + sizeof(climate_t) + sizeof(terrain_t) +
                           sizeof(vs_totals_t) + band_rows * sizeof(vs_row_job_t) + places * sizeof(vs_place_t) +
                           pixels * 4u + stream_bytes + 16u * MC_CPU_CACHE_LINE_BYTES;
    heap_region_t region;
    if (!heap_reserve((bytes + HEAP_REGION_ALIGNMENT - 1u) / HEAP_REGION_ALIGNMENT * HEAP_REGION_ALIGNMENT, &region)) {
        fprintf(stderr, "bench_biome_vs_class: no memory\n");
        return 2;
    }
    memset(region.owned_base, 0, bytes);
    uint8_t *cursor = region.owned_base;
#define VS_TAKE(type, count) ((type *)vs_take(&cursor, (uint64_t)(count) * sizeof(type)))
    thread_topology_t *const topology = VS_TAKE(thread_topology_t, 1u);
    vs_totals_t *const totals = VS_TAKE(vs_totals_t, 1u);
    vs_memory_t memory = {.stream_bytes = stream_bytes};
    memory.job = VS_TAKE(job_system_t, 1u);
    memory.climate = VS_TAKE(climate_t, 1u);
    memory.terrain = VS_TAKE(terrain_t, 1u);
    memory.jobs = VS_TAKE(vs_row_job_t, band_rows);
    memory.places = VS_TAKE(vs_place_t, places);
    memory.pixels = VS_TAKE(uint32_t, pixels);
    memory.stream = VS_TAKE(uint8_t, stream_bytes);
#undef VS_TAKE
    if (!thread_topology_read(topology) || !job_init_count(memory.job, topology, options.workers) ||
        !climate_init(memory.climate)) {
        fprintf(stderr, "bench_biome_vs_class: could not start\n");
        heap_release(&region);
        return 2;
    }
    fprintf(stderr, "bench_biome_vs_class: %u seeds, %u workers, %u by %u cells\n", options.seeds,
            memory.job->worker_count, columns, rows);
    uint32_t missing = 0u;
    bool pictures_ok = true;
    for (uint32_t s = 0u; s < options.seeds; s++) {
        const uint64_t seed = VS_SEEDS[options.first + s];
        const int64_t seed_started = time_now_ns();
        if (!terrain_init(memory.terrain, seed) || !climate_open(memory.climate, memory.terrain)) {
            fprintf(stderr, "bench_biome_vs_class: seed %" PRIu64 " would not open\n", seed);
            missing++;
            continue;
        }
        missing += vs_area(memory.job, memory.climate, memory.terrain, CLIMATE_HALF_CELL_BLOCKS,
                           row_min * CLIMATE_CELL_BLOCKS + CLIMATE_HALF_CELL_BLOCKS, (uint32_t)CLIMATE_CELL_BLOCKS,
                           columns, rows, memory.jobs, memory.places);
        for (uint64_t i = 0u; i < (uint64_t)rows * columns; i++) {
            const vs_place_t *const place = &memory.places[i];
            if (place->klass == CLIMATE_CLASS_NONE || place->missing != 0u) {
                continue;
            }
            const bool dry = place->klass == CLIMATE_CLASS_DESERT || place->klass == CLIMATE_CLASS_STEPPE;
            uint64_t *const row = totals->counts[s][place->klass];
            row[0]++;
            row[1] += vs_desert_biome(place->biome) != dry ? 1u : 0u;
            if (vs_strict_disagrees(place->klass, place->biome)) {
                row[2]++;
                totals->wrong[place->klass][place->biome]++;
            }
            row[3] += place->family != terrain_biome_family_of_class(place->klass) ? 1u : 0u;
            row[4] += vs_strict_disagrees(place->klass, place->inside) ? 1u : 0u;
        }
        if (options.pictures != nullptr && s < options.picture_seeds) {
            pictures_ok = vs_pictures(&options, &memory, seed) && pictures_ok;
        }
        climate_close(memory.climate, memory.job);
        terrain_deinit(memory.terrain);
        fprintf(stderr, "bench_biome_vs_class: seed %" PRIu64 " in %.1f s\n", seed,
                (libc_double_t)(time_ns_to_ms(time_now_ns() - seed_started) / 1000.0f64));
    }
    const bool met = vs_print(totals, &options);
    fprintf(stderr, "bench_biome_vs_class: %.0f s in all, %u places whose tile was not built\n",
            (libc_double_t)(time_ns_to_ms(time_now_ns() - started) / 1000.0f64), missing);
    climate_deinit(memory.climate);
    job_deinit(memory.job);
    heap_release(&region);
    return met && missing == 0u && pictures_ok ? 0 : 1;
}
