/* bench_atlas: what the Atlas costs. See README.md. */
#include "climate/index.h"
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

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    BENCH_WORKERS = 16u,
    BENCH_WIDTH = 1920u,
    BENCH_HEIGHT = 1080u,
    BENCH_SIDE_TILES = 20u, /* 20 by 20 level-0 tiles of 512 blocks: 10240 blocks a side, 105 km^2 */
    BENCH_OPENS = 5u,
    BENCH_DRAWS = 3u,
    BENCH_FRAMES = 200u,
    BENCH_WRITES = 20000u,
    BENCH_TOPS = 2000u,
};
static const uint64_t BENCH_SEED = 2198175988018515243ull;
static const float64_t BENCH_OPEN_TARGET_MS = 100.0f64;
static const float64_t BENCH_DRAW_TARGET_MS = 60.0f64;
static const float64_t BENCH_FRAME_TARGET_MS = 2.0f64;
static const float64_t BENCH_WRITE_TARGET_US = 5.0f64;
static const float64_t BENCH_TOP_TARGET_US = 20.0f64;
static const int32_t BENCH_CLIMATE_MARGIN = 8192; /* blocks of built climate tiles round the layers' view */
static const int32_t BENCH_OPEN_ZOOM = 7; /* 11.3 blocks a pixel: 21.7 by 12.2 km, the 100 km^2 and room round it */

/* What the bench holds, each at its own place in one heap region. */
typedef struct bench_t {
    job_system_t *job;           /* 0 */
    ui_t *ui;                    /* 8 */
    texture_t *texture;          /* 16 */
    climate_t *climate;          /* 24 */
    terrain_t *terrain;          /* 32 */
    thread_topology_t *topology; /* 40 */
} bench_t;
static_assert(sizeof(bench_t) == 48);

static int32_t bench_compare(const void *a, const void *b) {
    const float64_t x = *(const float64_t *)a, y = *(const float64_t *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* Fastest, median, slowest, against a target. */
static void bench_report(const char *what, float64_t *values, uint32_t count, float64_t target, const char *unit) {
    qsort(values, count, sizeof(values[0]), bench_compare);
    printf("%-34s fastest %8.3f  median %8.3f  slowest %8.3f %s  target %.1f  %s\n", what, (libc_double_t)values[0],
           (libc_double_t)values[count / 2u], (libc_double_t)values[count - 1u], unit, (libc_double_t)target,
           values[count / 2u] < target ? "met" : "MISSED");
}

/* A column's top of a smooth made-up land: height from a few sines, colour by height. Cheap, so building 100 km^2 is
 * the store's cost and not the terrain's. */
static void bench_column(int32_t chunk_x, int32_t chunk_y, uint32_t *out) {
    for (uint32_t r = 0u; r < 16u; r++) {
        for (uint32_t c = 0u; c < 16u; c++) {
            const float64_t x = (float64_t)chunk_x * 16.0f64 + c, y = (float64_t)chunk_y * 16.0f64 + 15.0f64 - r;
            const float64_t h =
                60.0f64 * sin(x / 700.0f64) + 45.0f64 * cos(y / 530.0f64) + 20.0f64 * sin((x + y) / 90.0f64);
            const int16_t z = (int16_t)lround(h);
            const uint32_t rgb = z < 0 ? 0x2050A0u : (z < 40 ? 0x50A040u : 0x808070u);
            out[r * 16u + c] = ui_atlas_pixel_pack(ui_atlas_rgb565_of(rgb), z);
        }
    }
}

/* 100 km^2 of columns written, saved as it goes so no write is dropped, and closed: every tile and mip on disk. */
static bool bench_build(bench_t *bench, const char *directory) {
    if (!ui_atlas_world_open(bench->ui, directory, BENCH_SEED, bench->job, nullptr)) {
        return false;
    }
    ui_atlas_t *const atlas = ui_atlas(bench->ui);
    const int64_t started = time_now_ns();
    uint32_t column[UI_ATLAS_COLUMN_PIXELS];
    const int32_t half = (int32_t)(BENCH_SIDE_TILES / 2u);
    uint32_t tiles = 0u;
    for (int32_t ty = -half; ty < half; ty++) {
        for (int32_t tx = -half; tx < half; tx++) {
            for (int32_t cy = 0; cy < 32; cy++) {
                for (int32_t cx = 0; cx < 32; cx++) {
                    bench_column(tx * 32 + cx, ty * 32 + cy, column);
                    ui_atlas_column_write(atlas, tx * 32 + cx, ty * 32 + cy, column);
                }
            }
            if (++tiles % 100u == 0u) {
                arena_reset(&atlas->arena_save);
                ui_atlas_save_now(atlas, &atlas->arena_save);
            }
        }
    }
    const uint64_t lost = atlas->store.columns_lost;
    ui_atlas_world_close(bench->ui); /* writes what is left and every mip */
    printf("built %u tiles of 512 by 512 blocks (%.0f km^2) in %.1f s, %" PRIu64 " columns lost\n", tiles,
           (libc_double_t)((float64_t)tiles * 0.262144f64), (libc_double_t)time_ns_to_seconds(time_now_ns() - started),
           lost);
    return lost == 0u;
}

static ui_atlas_world_t bench_world(int64_t ns) {
    return (ui_atlas_world_t){.player = {0.0f64, 0.0f64},
                              .t = 196.4f64,
                              .world_ns = ns,
                              .census_week = 28u,
                              .yaw = 0.0f32,
                              .day_of_year = 196u};
}

/* M to the first complete picture, over the whole 100 km^2, with the files in the page cache and nothing in memory. */
static void bench_opens(bench_t *bench, const char *directory) {
    float64_t ms[BENCH_OPENS];
    for (uint32_t round = 0u; round < BENCH_OPENS; round++) {
        if (!ui_atlas_world_open(bench->ui, directory, BENCH_SEED, bench->job, nullptr)) {
            return;
        }
        ui_atlas_t *const atlas = ui_atlas(bench->ui);
        const ui_atlas_world_t world = bench_world(1000000000);
        input_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.pressed.words[KEY_M / 64u] |= 1ull << (KEY_M % 64u);
        const int64_t started = time_now_ns();
        (void)ui_atlas_input(atlas, &bench->ui->widget, &frame, BENCH_WIDTH, BENCH_HEIGHT, &world, false);
        atlas->wanted.zoom = BENCH_OPEN_ZOOM;
        while (atlas->pictures_drawn == 0u) {
            ui_atlas_frame(atlas, BENCH_WIDTH, BENCH_HEIGHT, &world);
        }
        ms[round] = time_ns_to_ms(time_now_ns() - started);
        ui_atlas_world_close(bench->ui);
    }
    bench_report("open over 100 km^2, M to picture", ms, BENCH_OPENS, BENCH_OPEN_TARGET_MS, "ms");
}

/* Each layer's whole picture at 1920 by 1080 on 16 workers, over the store's ground, climate tiles warm. */
static void bench_layers(bench_t *bench, const char *directory, uint8_t *memory) {
    if (!ui_atlas_world_open(bench->ui, directory, BENCH_SEED, bench->job, bench->climate)) {
        return;
    }
    ui_atlas_t *const atlas = ui_atlas(bench->ui);
    const uint64_t pixels = (uint64_t)BENCH_WIDTH * BENCH_HEIGHT;
    ui_atlas_target_t target = {
        .borrowed_pixels = (uint32_t *)(void *)memory,
        .borrowed_grey = memory + pixels * 4u,
        .borrowed_field = (float32_t *)(void *)(memory + pixels * 5u),
        .borrowed_scratch = memory + pixels * 9u,
    };
    arena_t *const systems = &atlas->arena_draw;
    job_group_t group;
    job_group_init(&group);
    for (uint32_t layer = 0u; layer < UI_ATLAS_LAYER_COUNT; layer++) {
        float64_t ms[BENCH_DRAWS];
        for (uint32_t round = 0u; round < BENCH_DRAWS; round++) {
            arena_reset(systems);
            ui_atlas_draw_input_t input = {
                .view = {.center = {0.0f64, 0.0f64},
                         .zoom = 5, /* 5.66 blocks a pixel: 10.9 by 6.1 km, all of it generated */
                         .width = BENCH_WIDTH,
                         .height = BENCH_HEIGHT,
                         .day = 196u,
                         .layer = (uint8_t)layer,
                         .now_bucket = round},
                .ground = {.borrowed_context = atlas, .read = ui_atlas_store_read},
                .borrowed_climate = bench->climate,
                .borrowed_week = climate_week_store(bench->climate),
                .borrowed_font = &bench->ui->font,
                .t = 196.4f64,
                .census_week = 28u,
                .sample = UI_ATLAS_SAMPLE_PIXELS,
            };
            const int64_t started = time_now_ns();
            climate_systems_day_t day;
            if (ui_atlas_layers[layer].time == UI_ATLAS_TIME_NOW &&
                ui_atlas_systems_build(bench->climate, systems, &input.view, input.t, &day)) {
                input.borrowed_systems = &day;
            }
            ui_atlas_draw_picture(&input, &target, bench->job, &group, UI_ATLAS_BANDS, &atlas->arena_band);
            ms[round] = time_ns_to_ms(time_now_ns() - started);
        }
        char what[64];
        snprintf(what, sizeof(what), "layer %s", ui_atlas_layers[layer].key);
        bench_report(what, ms, BENCH_DRAWS, BENCH_DRAW_TARGET_MS, "ms");
        if (ui_atlas_layers[layer].value != UI_ATLAS_VALUE_NONE) {
            /* A value layer timed while it had nothing to show would be timing its failures. */
            uint64_t valued = 0u;
            for (uint64_t i = 0u; i < pixels; i++) {
                valued += isnan(target.borrowed_field[i]) ? 0u : 1u;
            }
            printf("  %s: %.1f%% of its pixels have a value\n", ui_atlas_layers[layer].key,
                   (libc_double_t)(100.0f64 * (float64_t)valued / (float64_t)pixels));
        }
    }
    job_group_deinit(&group);
    /* The main thread's frame with the Atlas up and a picture shown: the copy, title, legend, slider, readout, mark. */
    const ui_atlas_world_t world = bench_world(2000000000);
    input_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.pressed.words[KEY_M / 64u] |= 1ull << (KEY_M % 64u);
    (void)ui_atlas_input(atlas, &bench->ui->widget, &frame, BENCH_WIDTH, BENCH_HEIGHT, &world, false);
    atlas->wanted.zoom = 5;
    atlas->wanted.layer = 2u; /* temperature on the day: a slider and a scale */
    while (atlas->pictures_drawn == 0u || atlas->drawing_busy != 0u) {
        ui_atlas_frame(atlas, BENCH_WIDTH, BENCH_HEIGHT, &world);
    }
    float64_t ms[BENCH_FRAMES];
    for (uint32_t f = 0u; f < BENCH_FRAMES; f++) {
        const int64_t started = time_now_ns();
        ui_atlas_frame(atlas, BENCH_WIDTH, BENCH_HEIGHT, &world);
        if (ui_frame_begin(bench->ui, memory, (uint64_t)BENCH_WIDTH * 4u, BENCH_WIDTH, BENCH_HEIGHT)) {
            ui_atlas_frame_draw(atlas, &bench->ui->widget, &bench->ui->canvas, &bench->ui->font, &world);
            (void)ui_frame_end(bench->ui, true);
        }
        ms[f] = time_ns_to_ms(time_now_ns() - started);
    }
    bench_report("main's frame with the Atlas up", ms, BENCH_FRAMES, BENCH_FRAME_TARGET_MS, "ms");
    ui_atlas_world_close(bench->ui);
}

/* A column write on the sim, and a column's top on a worker. */
static void bench_columns(bench_t *bench, uint8_t *memory) {
    if (!ui_atlas_world_open(bench->ui, nullptr, BENCH_SEED, nullptr, nullptr)) {
        return;
    }
    ui_atlas_t *const atlas = ui_atlas(bench->ui);
    uint32_t column[UI_ATLAS_COLUMN_PIXELS];
    bench_column(3, 4, column);
    float64_t us[5];
    for (uint32_t round = 0u; round < 5u; round++) {
        const int64_t started = time_now_ns();
        for (uint32_t n = 0u; n < BENCH_WRITES; n++) {
            ui_atlas_column_write(atlas, (int32_t)(n % 256u) - 128, (int32_t)(n / 256u % 64u), column);
        }
        us[round] = (float64_t)(time_now_ns() - started) / 1000.0f64 / (float64_t)BENCH_WRITES;
    }
    bench_report("column write (sim)", us, 5u, BENCH_WRITE_TARGET_US, "us");
    ui_atlas_world_close(bench->ui);
    /* A column of the real terrain's kind: stone to 60, dirt, grass at 64, a flower, the rest air. */
    uint16_t *const states = (uint16_t *)(void *)memory;
    const uint64_t voxels = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS;
    memset(states, 0, voxels * sizeof(uint16_t));
    for (uint32_t z = 0u; z < (uint32_t)(64 - COLUMN_Z_STORAGE_MIN); z++) {
        for (uint32_t k = 0u; k < 256u; k++) {
            states[(uint64_t)(z / 16u) * SECTION_VOXELS + section_voxel_index(k % 16u, k / 16u, z % 16u)] = 1u;
        }
    }
    uint32_t top[UI_ATLAS_COLUMN_PIXELS];
    for (uint32_t round = 0u; round < 5u; round++) {
        const int64_t started = time_now_ns();
        for (uint32_t n = 0u; n < BENCH_TOPS; n++) {
            ui_atlas_column_top(ui_atlas_colours(bench->ui), BLOCK_COUNT, states, top);
        }
        us[round] = (float64_t)(time_now_ns() - started) / 1000.0f64 / (float64_t)BENCH_TOPS;
    }
    bench_report("column top (worker)", us, 5u, BENCH_TOP_TARGET_US, "us");
}

/* Everything, once the region is laid out. 0, or 2 if something could not start. */
static int32_t bench_run(bench_t *bench, uint8_t *memory, char *directory) {
    if (!texture_init(bench->texture) || !ui_init(bench->ui, bench->texture) || !climate_init(bench->climate) ||
        !terrain_init(bench->terrain, BENCH_SEED) || !climate_open(bench->climate, bench->terrain) ||
        mkdtemp(directory) == nullptr) {
        return 2;
    }
    printf("bench_atlas: %u workers, %u by %u, files in %s\n", BENCH_WORKERS, BENCH_WIDTH, BENCH_HEIGHT, directory);
    const int64_t started = time_now_ns();
    /* The view, 10.9 by 6.1 km, and BENCH_CLIMATE_MARGIN round it: temperature now's weather reads every climate tile
     * within 6 km of each place it is asked about, as the census keeps them built round explored land. */
    climate_prepare_area(bench->climate, bench->job, -5632 - BENCH_CLIMATE_MARGIN, -3584 - BENCH_CLIMATE_MARGIN,
                         5631 + BENCH_CLIMATE_MARGIN, 3583 + BENCH_CLIMATE_MARGIN, nullptr, nullptr);
    printf("climate tiles round the view worked out in %.1f s\n",
           (libc_double_t)time_ns_to_seconds(time_now_ns() - started));
    if (!bench_build(bench, directory)) {
        return 2;
    }
    bench_opens(bench, directory);
    bench_layers(bench, directory, memory);
    bench_columns(bench, memory);
    return 0;
}

int32_t main(int32_t argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "usage: bench_atlas\n");
        return 2;
    }
    /* job_system_t, ui_t, texture_t, climate_t, terrain_t and the topology each on a page of its own, then the pictures. */
    enum : uint64_t { PAGE = 64ull << 10 };
    static_assert(sizeof(job_system_t) <= PAGE && sizeof(ui_t) <= PAGE && sizeof(texture_t) <= PAGE);
    static_assert(sizeof(climate_t) <= PAGE && sizeof(terrain_t) <= PAGE && sizeof(thread_topology_t) <= PAGE);
    const uint64_t picture_bytes = (uint64_t)BENCH_WIDTH * BENCH_HEIGHT * 9u + UI_ATLAS_OVERLAY_SCRATCH_BYTES;
    heap_region_t region = {0};
    if (!heap_reserve(6u * PAGE + picture_bytes, &region)) {
        return 2;
    }
    uint8_t *const base = region.owned_base;
    bench_t bench = {
        .job = (job_system_t *)(void *)base,
        .ui = (ui_t *)(void *)(base + PAGE),
        .texture = (texture_t *)(void *)(base + 2u * PAGE),
        .climate = (climate_t *)(void *)(base + 3u * PAGE),
        .terrain = (terrain_t *)(void *)(base + 4u * PAGE),
        .topology = (thread_topology_t *)(void *)(base + 5u * PAGE),
    };
    char directory[] = "/tmp/mc_bench_atlas_XXXXXX";
    int32_t status = 2;
    if (thread_topology_read(bench.topology) && job_init_count(bench.job, bench.topology, BENCH_WORKERS)) {
        status = bench_run(&bench, base + 6u * PAGE, directory);
        if (directory[sizeof(directory) - 7u] != 'X') {
            char command[96];
            snprintf(command, sizeof(command), "rm -rf -- '%s'", directory);
            (void)system(command);
        }
        climate_close(bench.climate, bench.job);
        climate_deinit(bench.climate);
        terrain_deinit(bench.terrain);
        ui_deinit(bench.ui);
        texture_deinit(bench.texture);
        job_deinit(bench.job);
    }
    heap_release(&region);
    return status;
}
