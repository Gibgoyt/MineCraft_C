/* bench_world_map_atlas: a seed's real world as the Atlas shows it, every layer, with no display and no card. See
 * README.md. */
#include "climate/index.h"
#include "climate/layer/index.h"
#include "display/headless/png/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "machine/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/column/index.h"
#include "world/section/index.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum : uint32_t {
    WMA_WORKERS = 16u,
    WMA_SIDE = 512u, /* the pictures, pixels a side */
    WMA_PIXELS = WMA_SIDE * WMA_SIDE,
    WMA_LAYERS = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    WMA_COLUMN_VOXELS = (uint32_t)COLUMN_SECTIONS * SECTION_VOXELS,
    WMA_ROWS_MOST = 512u,    /* rows of columns made: 8192 blocks */
    WMA_LEVEL0_TILES = 128u, /* of UI_ATLAS_SLOTS, so that nothing made is forgotten before it is drawn */
    WMA_DAY = 196u,          /* the day normals layers are drawn on, as bench_climate's */
    WMA_CENSUS_WEEK = 28u,
};
static_assert(MC_THREADS_WORKERS >= WMA_WORKERS);
static_assert((uint32_t)WMA_LEVEL0_TILES < (uint32_t)UI_ATLAS_SLOTS);
static const float64_t WMA_T = 196.4f64; /* the systems' time: day 196, 09:36 */

/* A row of columns, made by one job, into the rows' pixels: the game's own column job, terrain_generate_box and the top. */
typedef struct wma_rows_t {
    const terrain_t *borrowed_terrain; /* 0 */
    const uint16_t *borrowed_colours;  /* 8: ui's RGB565 by block id */
    uint16_t *borrowed_states;         /* 16: a whole column a worker */
    uint8_t *borrowed_opened;          /* 24: the same */
    uint32_t *borrowed_pixels;         /* 32: UI_ATLAS_COLUMN_PIXELS a column, row after row of columns */
    int32_t west;                      /* 40: the first column's chunk x */
    int32_t south;                     /* 44: the first row's chunk y */
    uint32_t across;                   /* 48: columns a row */
    uint8_t _pad0[4];                  /* 52 */
} wma_rows_t;
static_assert(sizeof(wma_rows_t) == 56);

typedef struct wma_row_t {
    const wma_rows_t *borrowed_rows; /* 0 */
    uint32_t row;                    /* 8 */
    uint8_t _pad0[4];                /* 12 */
} wma_row_t;
static_assert(sizeof(wma_row_t) == 16);

static void wma_row_job(job_worker_t *worker, void *borrowed_argument) {
    const wma_row_t *const job = borrowed_argument;
    const wma_rows_t *const rows = job->borrowed_rows;
    uint16_t *const states = rows->borrowed_states + (uint64_t)worker->index * WMA_COLUMN_VOXELS;
    uint8_t *const opened = rows->borrowed_opened + (uint64_t)worker->index * WMA_COLUMN_VOXELS;
    for (uint32_t c = 0u; c < rows->across; c++) {
        const terrain_density_box_t box = {.origin_x = (rows->west + (int32_t)c) * (int32_t)COLUMN_SIZE,
                                           .origin_y = (rows->south + (int32_t)job->row) * (int32_t)COLUMN_SIZE,
                                           .origin_z = COLUMN_Z_STORAGE_MIN,
                                           .count_x = COLUMN_SIZE,
                                           .count_y = COLUMN_SIZE,
                                           .count_z = WMA_LAYERS};
        terrain_generate_box(rows->borrowed_terrain, &box, opened, states);
        ui_atlas_column_top(rows->borrowed_colours, BLOCK_COUNT, states,
                            rows->borrowed_pixels + ((uint64_t)job->row * rows->across + c) * UI_ATLAS_COLUMN_PIXELS);
    }
}

static bool wma_png_write(const char *directory, const char *name, const uint8_t *pixels, uint8_t *stream,
                          uint64_t capacity) {
    char path[1024];
    const int32_t written = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (written <= 0 || (uint64_t)written >= sizeof(path)) {
        fprintf(stderr, "bench_world_map_atlas: the path %s/%s is too long\n", directory, name);
        return false;
    }
    uint64_t bytes = 0u;
    if (!display_png_encode(pixels, WMA_SIDE * 4u, WMA_SIDE, WMA_SIDE, stream, capacity, &bytes)) {
        fprintf(stderr, "bench_world_map_atlas: cannot encode %s\n", path);
        return false;
    }
    FILE *const file = fopen(path, "wb");
    if (file == nullptr) {
        fprintf(stderr, "bench_world_map_atlas: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    const bool wrote = fwrite(stream, 1u, bytes, file) == bytes;
    if (fclose(file) != 0 || !wrote) {
        fprintf(stderr, "bench_world_map_atlas: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    printf("wrote %s\n", path);
    return true;
}

/* What was asked for. */
typedef struct wma_args_t {
    const char *directory; /* 0 */
    const char *layer;     /* 8: a registry key, or "all" */
    uint64_t seed;         /* 16 */
    int32_t zoom;          /* 24 */
    uint8_t flat;          /* 28: hill shading off and every pixel sampled */
    uint8_t _pad0[3];      /* 29 */
} wma_args_t;
static_assert(sizeof(wma_args_t) == 32);

static bool wma_args_parse(int32_t argc, char **argv, wma_args_t *out) {
    *out = (wma_args_t){
        .directory = nullptr, .layer = "all", .seed = TERRAIN_SEED_DEFAULT, .zoom = 4, .flat = 0u, ._pad0 = {0}};
    bool have_seed = false;
    for (int32_t i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--flat") == 0) {
            out->flat = 1u;
        } else if (strcmp(argv[i], "--layer") == 0 && i + 1 < argc) {
            out->layer = argv[++i];
        } else if (strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) {
            char *end = nullptr;
            const int64_t zoom = strtoll(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || zoom < 0 || zoom > UI_ATLAS_ZOOM_MAX) {
                return false;
            }
            out->zoom = (int32_t)zoom;
        } else if (out->directory == nullptr) {
            out->directory = argv[i];
        } else if (!have_seed) {
            char *end = nullptr;
            errno = 0;
            out->seed = strtoull(argv[i], &end, 0);
            if (end == argv[i] || *end != '\0' || errno != 0) {
                return false;
            }
            have_seed = true;
        } else {
            return false;
        }
    }
    return out->directory != nullptr;
}

/* Everything one run holds, each where the next 64-byte line starts in one heap region. */
typedef struct wma_memory_t {
    uint8_t *picture;   /* 0: RGBA */
    uint8_t *grey;      /* 8 */
    uint8_t *field;     /* 16 */
    uint8_t *overlay;   /* 24 */
    uint8_t *check;     /* 32: climate_layer_draw's picture */
    uint8_t *stream;    /* 40: a PNG */
    ui_t *ui;           /* 48 */
    texture_t *texture; /* 56 */
    terrain_t *terrain; /* 64 */
    climate_t *climate; /* 72 */
    uint8_t *bands;     /* 80 */
    uint8_t *systems;   /* 88 */
    uint16_t *states;   /* 96 */
    uint8_t *opened;    /* 104 */
    uint32_t *columns;  /* 112 */
    wma_row_t *rows;    /* 120 */
} wma_memory_t;
static_assert(sizeof(wma_memory_t) == 128);

enum : uint32_t {
    WMA_PARTS = 16u,
};

/* The Atlas row of each layer asked for, over the store. With --flat at a whole number of blocks a pixel, each climate
 * field is held to climate_layer_draw's bytes at the same place. 0, 1 for a field that differs, 2 for a write that
 * failed. */
static int32_t wma_draw(const wma_args_t *args, const wma_memory_t *m, uint64_t sizes_bands, uint64_t sizes_systems,
                        uint64_t png_bytes, job_system_t *system) {
    const uint32_t only = strcmp(args->layer, "all") == 0 ? UI_ATLAS_LAYER_COUNT : ui_atlas_layer_find(args->layer);
    ui_atlas_t *const atlas = ui_atlas(m->ui);
    arena_t scratch, systems;
    arena_init(&scratch, "bench_world_map_atlas_bands", m->bands, sizes_bands);
    arena_init(&systems, "bench_world_map_atlas_systems", m->systems, sizes_systems);
    const ui_atlas_target_t target = {(uint32_t *)(void *)m->picture, m->grey, (float32_t *)(void *)m->field,
                                      m->overlay};
    const float64_t step = ui_atlas_blocks_per_pixel(args->zoom);
    arena_reset(&atlas->arena_save);
    ui_atlas_mips_make(atlas, ui_atlas_level_of(args->zoom), &atlas->arena_save);
    job_group_t group;
    job_group_init(&group);
    int32_t status = 0;
    uint32_t same = 0u, checked = 0u;
    for (uint32_t layer = 0u; layer < UI_ATLAS_LAYER_COUNT && status != 2; layer++) {
        if (only != UI_ATLAS_LAYER_COUNT && layer != only) {
            continue;
        }
        const ui_atlas_layer_t *const row = &ui_atlas_layers[layer];
        ui_atlas_draw_input_t input = {
            .view = {.center = {0.0f64, 0.0f64},
                     .zoom = args->zoom,
                     .width = WMA_SIDE,
                     .height = WMA_SIDE,
                     .day = row->time == UI_ATLAS_TIME_NORMALS ? WMA_DAY : 0u,
                     .layer = (uint8_t)layer},
            .ground = {.borrowed_context = atlas, .read = ui_atlas_store_read},
            .borrowed_climate = m->climate,
            .borrowed_week = climate_week_store(m->climate),
            .borrowed_font = &m->ui->font,
            .t = WMA_T,
            .census_week = WMA_CENSUS_WEEK,
            .sample = args->flat != 0u ? 1u : UI_ATLAS_SAMPLE_PIXELS,
            .flat = args->flat,
        };
        arena_reset(&systems);
        climate_systems_day_t day;
        if (row->time == UI_ATLAS_TIME_NOW &&
            ui_atlas_systems_build(m->climate, &systems, &input.view, input.t, &day)) {
            input.borrowed_systems = &day;
        }
        const int64_t drawn_at = time_now_ns();
        ui_atlas_draw_picture(&input, &target, system, &group, UI_ATLAS_BANDS, &scratch);
        const float64_t ms = time_ns_to_ms(time_now_ns() - drawn_at);
        uint64_t grey = 0u;
        for (uint64_t k = 0u; k < WMA_PIXELS; k++) {
            grey += m->grey[k];
        }
        char name[64];
        snprintf(name, sizeof(name), "%" PRIu64 "_atlas_%s.png", args->seed, row->key);
        printf("%-20s %.1f ms, %" PRIu64 " pixels ungenerated\n", row->key, (libc_double_t)ms, grey);
        if (!wma_png_write(args->directory, name, m->picture, m->stream, png_bytes)) {
            status = 2;
        }
        const ui_atlas_geometry_t g = ui_atlas_geometry(&input.view);
        const bool whole = step >= 1.0f64 && step == floor(step);
        if (args->flat != 0u && whole && row->climate_layer != UI_ATLAS_NO_CLIMATE_LAYER &&
            row->climate_layer != CLIMATE_LAYER_LATITUDE) {
            climate_layer_draw(m->climate, (climate_layer_t)row->climate_layer, (int32_t)((float64_t)g.column0 * step),
                               (int32_t)(-(float64_t)g.row0 * step), (int32_t)step, WMA_SIDE, WMA_SIDE, input.view.day,
                               m->check, WMA_SIDE * 4u);
            uint64_t differ = 0u;
            for (uint64_t k = 0u; k < WMA_PIXELS; k++) {
                uint32_t c = 0u;
                memcpy(&c, m->check + 4u * k, 4u);
                differ += c == CLIMATE_LAYER_GREY || (target.borrowed_pixels[k] & 0xFFFFFFu) == c ? 0u : 1u;
            }
            printf("  %s: %s climate_layer_draw's bytes (%" PRIu64 " pixels differ)\n", row->key,
                   differ == 0u ? "the same as" : "NOT the same as", differ);
            same += differ == 0u ? 1u : 0u;
            checked++;
        }
    }
    if (checked > 0u) {
        printf("%u of %u climate fields the same bytes as climate_layer_draw\n", same, checked);
        status = same == checked || status == 2 ? status : 1;
    }
    job_group_deinit(&group);
    return status;
}

/* The picture's land, column by column on the workers, then into the store as the sim writes it. */
static void wma_make(const wma_args_t *args, const wma_memory_t *m, job_system_t *system, int32_t west, int32_t south,
                     uint32_t across) {
    const wma_rows_t rows = {.borrowed_terrain = m->terrain,
                             .borrowed_colours = ui_atlas_colours(m->ui),
                             .borrowed_states = m->states,
                             .borrowed_opened = m->opened,
                             .borrowed_pixels = m->columns,
                             .west = west,
                             .south = south,
                             .across = across,
                             ._pad0 = {0}};
    const int64_t started = time_now_ns();
    job_group_t group;
    job_group_init(&group);
    for (uint32_t r = 0u; r < across; r++) {
        m->rows[r] = (wma_row_t){.borrowed_rows = &rows, .row = r, ._pad0 = {0}};
        job_submit(system, &group, wma_row_job, &m->rows[r]);
    }
    job_group_wait(system, &group);
    job_group_deinit(&group);
    ui_atlas_t *const atlas = ui_atlas(m->ui);
    for (uint32_t r = 0u; r < across; r++) {
        for (uint32_t c = 0u; c < across; c++) {
            ui_atlas_column_write(atlas, west + (int32_t)c, south + (int32_t)r,
                                  m->columns + ((uint64_t)r * across + c) * UI_ATLAS_COLUMN_PIXELS);
        }
    }
    printf("seed %" PRIu64 ": %u by %u columns (%u blocks a side) made on %u workers in %.1f s, %" PRIu64
           " columns lost\n",
           args->seed, across, across, across * COLUMN_SIZE, WMA_WORKERS,
           (libc_double_t)time_ns_to_seconds(time_now_ns() - started), atlas->store.columns_lost);
}

int32_t main(int32_t argc, char **argv) {
    wma_args_t args;
    if (!wma_args_parse(argc, argv, &args)) {
        fprintf(stderr,
                "usage: bench_world_map_atlas <directory for the pictures> [seed] [--layer <key|all>] "
                "[--zoom <0 to %d>] [--flat]\n",
                UI_ATLAS_ZOOM_MAX);
        return 2;
    }
    if (strcmp(args.layer, "all") != 0 && ui_atlas_layer_find(args.layer) == UI_ATLAS_LAYER_COUNT) {
        fprintf(stderr, "bench_world_map_atlas: no Atlas layer is called '%s'\n", args.layer);
        return 2;
    }
    /* The picture's blocks, and a column round them, made whole: everything shown is generated. */
    const float64_t step = ui_atlas_blocks_per_pixel(args.zoom);
    const int32_t half_columns = (int32_t)ceil(step * (float64_t)WMA_SIDE / 2.0f64 / (float64_t)COLUMN_SIZE) + 1;
    const uint32_t across = (uint32_t)(2 * half_columns);
    const uint32_t tiles_across = across / 32u + 2u;
    if (across > WMA_ROWS_MOST || tiles_across * tiles_across > WMA_LEVEL0_TILES) {
        fprintf(stderr,
                "bench_world_map_atlas: zoom %d would need %u by %u columns, more than the store keeps; 6 is "
                "the most\n",
                args.zoom, across, across);
        return 2;
    }
    if (mkdir(args.directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bench_world_map_atlas: cannot make %s: %s\n", args.directory, strerror(errno));
        return 2;
    }
    const uint64_t png_bytes = display_png_bytes_needed(WMA_SIDE, WMA_SIDE);
    const uint64_t band_bytes =
        ui_atlas_band_scratch_bytes(WMA_SIDE, (WMA_SIDE + UI_ATLAS_BANDS - 1u) / UI_ATLAS_BANDS);
    const uint64_t sizes[WMA_PARTS] = {(uint64_t)WMA_PIXELS * 4u,
                                       WMA_PIXELS,
                                       (uint64_t)WMA_PIXELS * 4u,
                                       UI_ATLAS_OVERLAY_SCRATCH_BYTES,
                                       (uint64_t)WMA_PIXELS * 4u,
                                       png_bytes,
                                       sizeof(ui_t),
                                       sizeof(texture_t),
                                       sizeof(terrain_t),
                                       sizeof(climate_t),
                                       band_bytes + (1ull << 20),
                                       16ull << 20,
                                       (uint64_t)MC_THREADS_WORKERS * WMA_COLUMN_VOXELS * sizeof(uint16_t),
                                       (uint64_t)MC_THREADS_WORKERS * WMA_COLUMN_VOXELS,
                                       (uint64_t)across * across * UI_ATLAS_COLUMN_PIXELS * sizeof(uint32_t),
                                       (uint64_t)across * sizeof(wma_row_t)};
    uint64_t total = 0u;
    for (uint32_t k = 0u; k < WMA_PARTS; k++) {
        total += (sizes[k] + 63u) & ~63ull;
    }
    heap_region_t region = {0};
    if (!heap_reserve(total, &region)) {
        return 2;
    }
    uint8_t *parts[WMA_PARTS];
    uint8_t *at = region.owned_base;
    for (uint32_t k = 0u; k < WMA_PARTS; k++) {
        parts[k] = at;
        at += (sizes[k] + 63u) & ~63ull;
    }
    const wma_memory_t m = {.picture = parts[0],
                            .grey = parts[1],
                            .field = parts[2],
                            .overlay = parts[3],
                            .check = parts[4],
                            .stream = parts[5],
                            .ui = (ui_t *)(void *)parts[6],
                            .texture = (texture_t *)(void *)parts[7],
                            .terrain = (terrain_t *)(void *)parts[8],
                            .climate = (climate_t *)(void *)parts[9],
                            .bands = parts[10],
                            .systems = parts[11],
                            .states = (uint16_t *)(void *)parts[12],
                            .opened = parts[13],
                            .columns = (uint32_t *)(void *)parts[14],
                            .rows = (wma_row_t *)(void *)parts[15]};
    thread_topology_t topology;
    job_system_t system;
    if (!thread_topology_read(&topology) || !job_init_count(&system, &topology, WMA_WORKERS)) {
        heap_release(&region);
        return 2;
    }
    int32_t status = 2;
    const bool climate_ready = climate_init(m.climate);
    if (climate_ready && texture_init(m.texture) && ui_init(m.ui, m.texture) && terrain_init(m.terrain, args.seed) &&
        climate_open(m.climate, m.terrain)) {
        const int32_t half_blocks = half_columns * (int32_t)COLUMN_SIZE;
        const int64_t started = time_now_ns();
        climate_prepare_area(m.climate, &system, -half_blocks - 64, -half_blocks - 64, half_blocks + 63,
                             half_blocks + 63, nullptr, nullptr);
        printf("seed %" PRIu64 ": the climate of %d by %d blocks round 0 worked out in %.1f ms; Atlas zoom %d, %.3f "
               "blocks a pixel%s\n",
               args.seed, 2 * half_blocks, 2 * half_blocks, (libc_double_t)time_ns_to_ms(time_now_ns() - started),
               args.zoom, (libc_double_t)step, args.flat != 0u ? ", flat" : "");
        if (ui_atlas_world_open(m.ui, nullptr, args.seed, &system, m.climate)) {
            wma_make(&args, &m, &system, -half_columns, -half_columns, across);
            status = wma_draw(&args, &m, sizes[10], sizes[11], png_bytes, &system);
            ui_atlas_world_close(m.ui);
        }
        climate_close(m.climate, &system);
    }
    terrain_deinit(m.terrain);
    ui_deinit(m.ui);
    texture_deinit(m.texture);
    if (climate_ready) {
        climate_deinit(m.climate);
    }
    job_deinit(&system);
    heap_release(&region);
    return status;
}
