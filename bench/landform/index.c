/* bench_landform: the shape of a seed's land, measured as plan 007 section 4 defines it, and drawn. See README.md. */
#include "display/headless/png/index.h"
#include "game/index.h"
#include "landform/draw/index.h"
#include "landform/measure/index.h"
#include "heap/index.h"
#include "job/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/font/index.h"
#include "ui/index.h"
#include "world/column/index.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct lf_options_t {
    const char *directory; /* 0 */
    uint32_t size_km;      /* 8 */
    uint32_t step;         /* 12 */
    uint32_t side;         /* 16 */
    uint32_t windows;      /* 20 */
    uint32_t windows_side; /* 24 */
    uint8_t strip;         /* 28 */
    uint8_t self_test;     /* 29 */
    uint8_t _pad0[2];      /* 30 */
} lf_options_t;
static_assert(sizeof(lf_options_t) == 32);

[[gnu::format(printf, 3, 4)]] static void lf_append(char *text, uint64_t *used, const char *format, ...) {
    va_list list;
    va_start(list, format);
    const uint64_t room = LF_TEXT_BYTES - *used;
    const int32_t wrote = vsnprintf(text + *used, room, format, list);
    va_end(list);
    *used += wrote > 0 ? ((uint64_t)wrote < room ? (uint64_t)wrote : room - 1u) : 0u;
}

/* ---- The table ----------------------------------------------------------------------------------------------------- */

static void lf_table(const lf_result_t *r, bool real, char *text, uint64_t *used) {
    const float64_t *const v = r->v;
    lf_append(text, used, "  slope classes, %% of land        ");
    for (uint32_t c = 0u; c < LF_CLASSES; c++) {
        lf_append(text, used, " %8s", LF_CLASS_NAME[c]);
    }
    static const char *const SOURCE[3] = {"profile, the windows at 4 blocks", "profile, the whole map at its step",
                                          "real blocks, the windows at 4    "};
    for (uint32_t s = real ? 0u : 1u; s < (real ? 3u : 2u); s++) {
        lf_append(text, used, "\n  %s", SOURCE[s]);
        for (uint32_t c = 0u; c < LF_CLASSES; c++) {
            lf_append(text, used, " %7.2f%%", (libc_double_t)v[LF_V_PROFILE + s * LF_CLASSES + c]);
        }
    }
    lf_append(text, used, "\n");
    if (real) {
        lf_append(text, used, "  profile against blocks: the largest class difference %.2f points\n",
                  (libc_double_t)v[LF_V_DIFFERENCE]);
    }
    lf_append(text, used,
              "  of land steeper than 1:2, %.1f%% is in the top 30%% of its local relief (2048 blocks); faces: none "
              "traced yet\n",
              (libc_double_t)v[LF_V_STEEP_TOP]);
    lf_append(text, used, "  median slope by decile of relative height, where local relief >= 150:");
    for (uint32_t d = 0u; d < LF_DECILES; d++) {
        lf_append(text, used, " %.3f", (libc_double_t)v[LF_V_DECILE + d]);
    }
    lf_append(text, used, "\n  top decile over the bottom 3: %.2f\n", (libc_double_t)v[LF_V_DECILE_RATIO]);
    lf_append(text, used, "  peaks (relief >= 150): %.0f; concavity ratio, median %.2f; W50/H, median %.2f\n",
              (libc_double_t)v[LF_V_PEAKS], (libc_double_t)v[LF_V_CONCAVITY], (libc_double_t)v[LF_V_WIDTH]);
    lf_append(text, used,
              "  plateau pieces (>= 250, relief <= 80 in 512): %.0f, %.0f of them >= 25 km^2; the largest %.2f km^2, "
              "%.2f km across; escarpment %.2f km\n",
              (libc_double_t)v[LF_V_PLATEAUS], (libc_double_t)v[LF_V_PLATEAUS_BIG], (libc_double_t)v[LF_V_PLATEAU_KM2],
              (libc_double_t)v[LF_V_PLATEAU_ACROSS], (libc_double_t)v[LF_V_ESCARPMENT]);
    lf_append(text, used, "  range pieces at >= 500: %.0f; the longest %.2f km, %.2f km^2, elongation %.2f\n",
              (libc_double_t)v[LF_V_RANGES], (libc_double_t)v[LF_V_RANGE_KM], (libc_double_t)v[LF_V_RANGE_KM2],
              (libc_double_t)v[LF_V_RANGE_ELONGATION]);
    lf_append(text, used,
              "  knobs (prominence 15 to 150): %.0f; by base < 150 / 150-250 / >= 250: %.0f / %.0f / %.0f\n",
              (libc_double_t)v[LF_V_KNOBS], (libc_double_t)v[LF_V_KNOB_BASE], (libc_double_t)v[LF_V_KNOB_BASE + 1u],
              (libc_double_t)v[LF_V_KNOB_BASE + 2u]);
    lf_append(text, used, "  sea %.2f%%; reaching", (libc_double_t)v[LF_V_SEA]);
    for (uint32_t k = 0u; k < 5u; k++) {
        lf_append(text, used, " %d: %.2f%%", LF_REACH[k], (libc_double_t)v[LF_V_REACH + k]);
    }
    lf_append(text, used, "; top %.0f\n", (libc_double_t)v[LF_V_TOP]);
    for (uint32_t p = 0u; p < LF_PLACES; p++) {
        if (!real && (p == LF_PLACE_STEEP_WINDOW || p == LF_PLACE_BELOW_SEA)) {
            continue;
        }
        if (r->place[p].found != 0u) {
            lf_append(text, used, "  %-30s /tp %d %d   (z %.0f)\n", LF_PLACE_NAME[p], r->place[p].x, r->place[p].y,
                      (libc_double_t)r->place[p].z);
        } else {
            lf_append(text, used, "  %-30s none\n", LF_PLACE_NAME[p]);
        }
    }
}

static void lf_csv(const lf_result_t *r, char *text, uint64_t *used) {
    lf_append(text, used, "measure,value\nseed,%" PRIu64 "\n", r->seed);
    for (uint32_t k = 0u; k < LF_V_COUNT; k++) {
        lf_append(text, used, "%s,%.4f\n", LF_VALUE_NAME[k], (libc_double_t)r->v[k]);
    }
    for (uint32_t p = 0u; p < LF_PLACES; p++) {
        lf_append(text, used, "place_%u_found,%u\nplace_%u_x,%d\nplace_%u_y,%d\n", p, r->place[p].found, p,
                  r->place[p].x, p, r->place[p].y);
    }
}

/* ---- One seed ------------------------------------------------------------------------------------------------------ */

/* What the profile costs beside the whole box, on one worker, and how fast boxes are at the tallest place and on plains
 * on all of them. */
static void lf_cost(job_system_t *system, lf_run_t *run, const lf_memory_t *m, const terrain_t *terrain,
                    lf_result_t *r) {
    const lf_place_t *const tall = &r->place[LF_PLACE_PEAK];
    const lf_place_t *const plains = &r->place[LF_PLACE_PLAINS];
    float64_t *const xs = (float64_t *)(void *)m->a;
    float64_t *const ys = xs + LF_COST_LATTICE;
    float32_t *const heights = (float32_t *)(void *)(ys + LF_COST_LATTICE);
    float32_t *const softness = heights + LF_COST_LATTICE;
    run->cost_x = (int32_t)floorf((float32_t)tall->x / 16.0f32) * 16 - (int32_t)(LF_COST_SIDE / 2u * LF_CHUNK);
    run->cost_y = (int32_t)floorf((float32_t)tall->y / 16.0f32) * 16 - (int32_t)(LF_COST_SIDE / 2u * LF_CHUNK);
    for (uint32_t c = 0u; c < LF_COST_CHUNKS; c++) {
        for (uint32_t k = 0u; k < 25u; k++) {
            xs[c * 25u + k] = (float64_t)(run->cost_x + (int32_t)((c % LF_COST_SIDE) * LF_CHUNK + 4u * (k % 5u)));
            ys[c * 25u + k] = (float64_t)(run->cost_y + (int32_t)((c / LF_COST_SIDE) * LF_CHUNK + 4u * (k / 5u)));
        }
    }
    float64_t profile_best = 1.0e30f64, box_best = 1.0e30f64;
    const uint32_t jobs = run->jobs;
    run->jobs = 1u;
    for (uint32_t round = 0u; round < LF_COST_ROUNDS; round++) {
        int64_t started = time_now_ns();
        for (uint32_t k = 0u; k < LF_COST_REPEATS; k++) {
            terrain_density_profile_points(terrain->borrowed_density, xs, ys, LF_COST_LATTICE, heights, softness);
        }
        const float64_t profile = (float64_t)(time_now_ns() - started) / (float64_t)LF_COST_REPEATS;
        profile_best = profile < profile_best ? profile : profile_best;
        started = time_now_ns();
        lf_jobs(system, run, LF_STAGE_COST, LF_COST_CHUNKS);
        const float64_t box = (float64_t)(time_now_ns() - started);
        box_best = box < box_best ? box : box_best;
    }
    run->jobs = jobs;
    r->v[LF_V_PROFILE_US] = profile_best / 1000.0f64 / (float64_t)LF_COST_LATTICE;
    r->v[LF_V_PROFILE_SHARE] = 100.0f64 * profile_best / box_best;
    for (uint32_t place = 0u; place < 2u; place++) {
        const lf_place_t *const at = place == 0u ? tall : plains;
        run->cost_x = (int32_t)floorf((float32_t)at->x / 16.0f32) * 16 - (int32_t)(LF_COST_SIDE / 2u * LF_CHUNK);
        run->cost_y = (int32_t)floorf((float32_t)at->y / 16.0f32) * 16 - (int32_t)(LF_COST_SIDE / 2u * LF_CHUNK);
        float64_t best = 1.0e30f64;
        for (uint32_t round = 0u; round < LF_COST_ROUNDS; round++) {
            const int64_t started = time_now_ns();
            lf_jobs(system, run, LF_STAGE_COST, LF_COST_CHUNKS);
            const float64_t seconds = (float64_t)(time_now_ns() - started) / (float64_t)TIME_NS_PER_SECOND;
            best = seconds < best ? seconds : best;
        }
        r->v[place == 0u ? LF_V_TALL_COLUMNS : LF_V_PLAIN_COLUMNS] = (float64_t)LF_COST_CHUNKS / best;
    }
}

static bool lf_seed(job_system_t *system, lf_run_t *run, const lf_memory_t *m, const lf_options_t *options,
                    uint64_t seed, lf_result_t *r) {
    terrain_t terrain;
    if (!terrain_init(&terrain, seed)) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    r->seed = seed;
    const int64_t started = time_now_ns();
    lf_grid_t grid = {.h = m->h, .g = m->g, .side = options->side, .step = options->step};
    grid.origin_x = GAME_WORLD_MIDDLE - (int32_t)(options->side * options->step / 2u);
    grid.origin_x -= grid.origin_x % (int32_t)options->step;
    grid.origin_y = grid.origin_x;
    run->borrowed_terrain = &terrain;
    run->borrowed_grid = &grid;
    printf("seed %" PRIu64 ": %u by %u columns every %u blocks from %d, %d, %u windows\n", seed, grid.side, grid.side,
           grid.step, grid.origin_x, grid.origin_y, options->windows);
    lf_jobs(system, run, LF_STAGE_SWEEP, grid.side);
    const float64_t sweep = (float64_t)(time_now_ns() - started) / (float64_t)TIME_NS_PER_SECOND;
    lf_measure(system, run, m, r);
    const float64_t measure = (float64_t)(time_now_ns() - started) / (float64_t)TIME_NS_PER_SECOND - sweep;
    lf_windows(system, run, m, &grid, options->windows_side, r);
    const float64_t windows = (float64_t)(time_now_ns() - started) / (float64_t)TIME_NS_PER_SECOND - sweep - measure;
    bool ok = true;
    ok = lf_draw_maps(&grid, m, r) && ok;
    ok = lf_draw_profiles(&terrain, &grid, m, r) && ok;
    ok = lf_draw_windows(m, seed, options->windows_side) && ok;
    if (options->strip != 0u) {
        run->borrowed_strip = m->strip;
        lf_jobs(system, run, LF_STAGE_STRIP, LF_STRIP_HIGH);
        ok = lf_draw_strip(m, seed) && ok;
    }
    r->v[LF_V_SECONDS] = (float64_t)(time_now_ns() - started) / (float64_t)TIME_NS_PER_SECOND;
    lf_cost(system, run, m, &terrain, r);
    printf("  profile sweep %.1f s, measures %.1f s, windows %.1f s, all with pictures %.1f s\n", (libc_double_t)sweep,
           (libc_double_t)measure, (libc_double_t)windows, (libc_double_t)r->v[LF_V_SECONDS]);
    printf("  cost: the profile %.2f us a lattice column, %.1f%% of terrain_generate_box on one worker; boxes on %u "
           "workers: %.0f columns a second at the tallest place, %.0f on plains\n",
           (libc_double_t)r->v[LF_V_PROFILE_US], (libc_double_t)r->v[LF_V_PROFILE_SHARE], run->jobs,
           (libc_double_t)r->v[LF_V_TALL_COLUMNS], (libc_double_t)r->v[LF_V_PLAIN_COLUMNS]);
    char *const text = (char *)(void *)m->stream;
    uint64_t used = 0u;
    lf_table(r, true, text, &used);
    fputs(text, stdout);
    used = 0u;
    lf_csv(r, text, &used);
    char path[LF_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/%" PRIu64 ".csv", m->directory, seed);
    FILE *const file = fopen(path, "w");
    if (file == nullptr || fwrite(text, 1u, used, file) != used) {
        fprintf(stderr, "bench_landform: cannot write %s: %s\n", path, strerror(errno));
        ok = false;
    }
    if (file != nullptr && fclose(file) != 0) {
        ok = false;
    }
    fflush(stdout);
    run->borrowed_terrain = nullptr;
    terrain_deinit(&terrain);
    return ok;
}

/* ---- The self-test ------------------------------------------------------------------------------------------------- */

typedef enum lf_shape_t : uint32_t {
    LF_SHAPE_CONE = 0,
    LF_SHAPE_POWER,
    LF_SHAPE_PLATEAU,
    LF_SHAPE_PLANE,
    LF_SHAPE_KNOBS,
} lf_shape_t;

/* A synthetic field of heights at 4 blocks, its gradient over 8 by the same differences as a window's, one-sided at the
 * border. */
static void lf_shape(lf_grid_t *grid, lf_shape_t shape) {
    grid->side = shape == LF_SHAPE_PLATEAU || shape == LF_SHAPE_KNOBS ? 2048u : 1024u;
    grid->step = LF_SELF_STEP;
    grid->origin_x = 0;
    grid->origin_y = 0;
    const uint32_t side = grid->side;
    const float64_t middle = (float64_t)(side * LF_SELF_STEP / 2u);
    for (uint32_t j = 0u; j < side; j++) {
        for (uint32_t i = 0u; i < side; i++) {
            const float64_t x = (float64_t)(i * LF_SELF_STEP), y = (float64_t)(j * LF_SELF_STEP);
            const float64_t r = sqrt((x - middle) * (x - middle) + (y - middle) * (y - middle));
            float64_t z = 0.0f64;
            if (shape == LF_SHAPE_CONE) {
                z = r < 600.0f64 ? 600.0f64 - r : 0.0f64;
            } else if (shape == LF_SHAPE_POWER) {
                z = r < 1500.0f64 ? 800.0f64 * pow(1.0f64 - r / 1500.0f64, 2.5f64) : 0.0f64;
            } else if (shape == LF_SHAPE_PLANE) {
                z = 0.2f64 * x + 0.1f64 * y;
            } else {
                const float64_t out = fmax(fabs(x - middle), fabs(y - middle)) - 2560.0f64;
                z = out <= 0.0f64 ? 400.0f64 : fmax(20.0f64, 400.0f64 - 2.0f64 * out);
            }
            if (shape == LF_SHAPE_KNOBS) {
                /* Ten domes of 40 on the plateau, 400 apart, and ten on the field, 700 apart. */
                for (uint32_t k = 0u; k < 20u; k++) {
                    const float64_t kx = k < 10u ? middle + ((float64_t)k - 4.5f64) * 400.0f64
                                                 : 600.0f64 + 700.0f64 * (float64_t)(k - 10u);
                    const float64_t ky = k < 10u ? middle : 600.0f64;
                    const float64_t d2 = ((x - kx) * (x - kx) + (y - ky) * (y - ky)) / 10000.0f64;
                    z += d2 < 1.0f64 ? 40.0f64 * (1.0f64 - d2) : 0.0f64;
                }
            }
            grid->h[(uint64_t)j * side + i] = (float32_t)z;
        }
    }
    for (uint32_t j = 0u; j < side; j++) {
        for (uint32_t i = 0u; i < side; i++) {
            const uint64_t at = (uint64_t)j * side + i;
            const uint32_t w = i > 0u ? i - 1u : i, e = i + 1u < side ? i + 1u : i;
            const uint32_t n = j > 0u ? j - 1u : j, s = j + 1u < side ? j + 1u : j;
            const float64_t gx =
                ((float64_t)grid->h[(uint64_t)j * side + e] - (float64_t)grid->h[(uint64_t)j * side + w]) /
                (float64_t)((e - w) * LF_SELF_STEP);
            const float64_t gy =
                ((float64_t)grid->h[(uint64_t)s * side + i] - (float64_t)grid->h[(uint64_t)n * side + i]) /
                (float64_t)((s - n) * LF_SELF_STEP);
            grid->g[at] = (float32_t)sqrt(gx * gx + gy * gy);
        }
    }
}

static bool lf_check(bool held, const char *name, const char *got) {
    printf("  %s: %s: %s\n", name, got, held ? "ok" : "FAILED");
    return held;
}

static bool lf_near(float64_t got, float64_t want, float64_t within) {
    return fabs(got - want) <= within;
}

static bool lf_self_test(job_system_t *system, lf_run_t *run, const lf_memory_t *m) {
    lf_grid_t grid = {.h = m->h, .g = m->g};
    run->borrowed_grid = &grid;
    lf_result_t r;
    char got[512];
    bool ok = true;
    printf("bench_landform --self-test: synthetic ground at %u blocks, through the measures the seeds use\n",
           LF_SELF_STEP);

    lf_shape(&grid, LF_SHAPE_CONE);
    memset(&r, 0, sizeof(r));
    lf_measure(system, run, m, &r);
    uint64_t flank = 0u, on_one = 0u;
    for (uint32_t j = 0u; j < grid.side; j++) {
        for (uint32_t i = 0u; i < grid.side; i++) {
            const float64_t dx = (float64_t)i * 4.0f64 - 2048.0f64, dy = (float64_t)j * 4.0f64 - 2048.0f64;
            const float64_t d = sqrt(dx * dx + dy * dy);
            if (d > 16.0f64 && d < 584.0f64) {
                flank++;
                on_one += lf_near((float64_t)grid.g[(uint64_t)j * grid.side + i], 1.0f64, 0.02f64) ? 1u : 0u;
            }
        }
    }
    snprintf(got, sizeof(got), "%.1f%% of the flank at g 1.00 +- 0.02, %.0f peak, concavity %.3f, W50/H %.3f",
             (libc_double_t)lf_percent(on_one, flank), (libc_double_t)r.v[LF_V_PEAKS],
             (libc_double_t)r.v[LF_V_CONCAVITY], (libc_double_t)r.v[LF_V_WIDTH]);
    ok = lf_check(lf_percent(on_one, flank) >= 95.0f64 && r.v[LF_V_PEAKS] == 1.0f64 &&
                      lf_near(r.v[LF_V_CONCAVITY], 1.0f64, 0.05f64) && lf_near(r.v[LF_V_WIDTH], 1.0f64, 0.05f64),
                  "a_one_to_one_cone_is_all_steep_and_has_concavity_one", got) &&
         ok;

    lf_shape(&grid, LF_SHAPE_POWER);
    memset(&r, 0, sizeof(r));
    lf_measure(system, run, m, &r);
    snprintf(got, sizeof(got), "%.0f peak, concavity %.3f (3.13 by hand), top decile over the bottom 3 %.2f",
             (libc_double_t)r.v[LF_V_PEAKS], (libc_double_t)r.v[LF_V_CONCAVITY], (libc_double_t)r.v[LF_V_DECILE_RATIO]);
    ok = lf_check(r.v[LF_V_PEAKS] == 1.0f64 && lf_near(r.v[LF_V_CONCAVITY], 3.13f64, 0.2f64) &&
                      r.v[LF_V_DECILE_RATIO] >= 5.0f64,
                  "a_power_two_and_a_half_peak_has_concavity_three", got) &&
         ok;

    /* The piece takes the edge down to 80 under the top, 40 blocks of the 1:2 slope on each side: 5.2 km across. */
    lf_shape(&grid, LF_SHAPE_PLATEAU);
    memset(&r, 0, sizeof(r));
    lf_measure(system, run, m, &r);
    snprintf(got, sizeof(got),
             "%.0f piece, %.2f km^2 (27.04 by hand), %.3f km across (5.20), escarpment %.2f km (20.8)",
             (libc_double_t)r.v[LF_V_PLATEAUS], (libc_double_t)r.v[LF_V_PLATEAU_KM2],
             (libc_double_t)r.v[LF_V_PLATEAU_ACROSS], (libc_double_t)r.v[LF_V_ESCARPMENT]);
    ok = lf_check(r.v[LF_V_PLATEAUS] == 1.0f64 && r.v[LF_V_PLATEAUS_BIG] == 1.0f64 &&
                      lf_near(r.v[LF_V_PLATEAU_KM2], 27.04f64, 1.0f64) &&
                      lf_near(r.v[LF_V_PLATEAU_ACROSS], 5.2f64, 0.032f64) &&
                      lf_near(r.v[LF_V_ESCARPMENT], 20.8f64, 0.5f64),
                  "a_five_by_five_km_plateau_is_one_piece", got) &&
         ok;

    lf_shape(&grid, LF_SHAPE_PLANE);
    memset(&r, 0, sizeof(r));
    lf_measure(system, run, m, &r);
    snprintf(got, sizeof(got), "%.2f%% moderate", (libc_double_t)r.v[LF_V_MAP + 2u]);
    ok = lf_check(r.v[LF_V_MAP + 2u] == 100.0f64, "a_plane_of_five_to_one_is_all_moderate", got) && ok;

    /* The last shape three times: twice on every worker, then on one, which must all give the same bytes. */
    char *const texts[3] = {(char *)(void *)m->stream, (char *)(void *)m->stream + LF_TEXT_BYTES * 2u,
                            (char *)(void *)m->stream + LF_TEXT_BYTES * 4u};
    uint64_t lengths[3] = {0u};
    const uint32_t jobs = run->jobs;
    for (uint32_t pass = 0u; pass < 3u; pass++) {
        run->jobs = pass == 2u ? 1u : jobs;
        lf_shape(&grid, LF_SHAPE_KNOBS);
        memset(&r, 0, sizeof(r));
        lf_measure(system, run, m, &r);
        lf_table(&r, false, texts[pass], &lengths[pass]);
        lf_csv(&r, texts[pass], &lengths[pass]);
    }
    run->jobs = jobs;
    snprintf(got, sizeof(got), "%.0f knobs: %.0f on a base < 150, %.0f at 150 to 250, %.0f >= 250",
             (libc_double_t)r.v[LF_V_KNOBS], (libc_double_t)r.v[LF_V_KNOB_BASE],
             (libc_double_t)r.v[LF_V_KNOB_BASE + 1u], (libc_double_t)r.v[LF_V_KNOB_BASE + 2u]);
    ok = lf_check(r.v[LF_V_KNOBS] == 20.0f64 && r.v[LF_V_KNOB_BASE] == 10.0f64 && r.v[LF_V_KNOB_BASE + 2u] == 10.0f64,
                  "knobs_are_binned_by_their_base", got) &&
         ok;
    const bool same = lengths[0] == lengths[1] && lengths[0] == lengths[2] &&
                      memcmp(texts[0], texts[1], lengths[0]) == 0 && memcmp(texts[0], texts[2], lengths[0]) == 0;
    snprintf(got, sizeof(got), "%" PRIu64 " bytes of table and CSV, twice on %u workers and once on 1", lengths[0],
             jobs);
    ok = lf_check(same, "the_same_input_gives_the_same_table", got) && ok;
    fputs(texts[0], stdout);
    printf("bench_landform --self-test: %s\n", ok ? "every check held" : "FAILED");
    run->borrowed_grid = nullptr;
    return ok;
}

/* ---- main ---------------------------------------------------------------------------------------------------------- */

static bool lf_options_read(int32_t argc, char **argv, lf_options_t *o, int32_t *out_first_seed) {
    *o = (lf_options_t){.size_km = 64u, .step = 16u, .windows = 64u, .strip = 1u};
    int32_t i = 1;
    for (; i < argc && strncmp(argv[i], "--", 2u) == 0; i++) {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--self-test") == 0) {
            o->self_test = 1u;
        } else if (strcmp(argv[i], "--strip") == 0) {
            o->strip = 1u;
        } else if (strcmp(argv[i], "--no-strip") == 0) {
            o->strip = 0u;
        } else if (strcmp(argv[i], "--size-km") == 0 && has_value) {
            o->size_km = (uint32_t)strtoul(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--step") == 0 && has_value) {
            o->step = (uint32_t)strtoul(argv[++i], nullptr, 10);
        } else if (strcmp(argv[i], "--windows") == 0 && has_value) {
            o->windows = (uint32_t)strtoul(argv[++i], nullptr, 10);
        } else {
            return false;
        }
    }
    while (o->windows_side * o->windows_side < o->windows) {
        o->windows_side++;
    }
    o->side = o->step > 0u ? o->size_km * 1024u / o->step : 0u;
    if (o->step == 0u || o->step % 4u != 0u || o->side < 64u || o->side > LF_SIDE_MAX || o->windows == 0u ||
        o->windows > LF_WINDOWS_MAX || o->windows_side * o->windows_side != o->windows) {
        return false;
    }
    o->directory = i < argc ? argv[i++] : nullptr;
    *out_first_seed = i;
    return o->self_test != 0u || o->directory != nullptr;
}

int32_t main(int32_t argc, char **argv) {
    lf_options_t options;
    int32_t first_seed = 0;
    if (!lf_options_read(argc, argv, &options, &first_seed) || argc - first_seed > (int32_t)LF_SEEDS_MAX) {
        fprintf(stderr, "usage: bench_landform [--self-test] [--size-km N] [--step N] [--strip | --no-strip] "
                        "[--windows N] <directory> [seed...]\n  N of windows a square, at most 64; the step a multiple "
                        "of 4; at most 4096 columns a side\n");
        return 2;
    }
    if (options.directory != nullptr && mkdir(options.directory, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bench_landform: cannot make %s: %s\n", options.directory, strerror(errno));
        return 2;
    }
    const uint64_t png_map = display_png_bytes_needed(LF_SIDE_MAX, LF_SIDE_MAX);
    const uint64_t png_profiles = display_png_bytes_needed(LF_PANEL_WIDE, LF_PICTURE_PIXELS / LF_PANEL_WIDE);
    const uint64_t stream_bytes = png_map > png_profiles ? png_map : png_profiles;
    enum : uint32_t { LF_PARTS = 25u };
    const uint64_t floats = (uint64_t)LF_PIXELS_MAX * 4u;
    const uint64_t windows = (uint64_t)LF_WINDOWS_MAX * LF_WINDOW_COLUMNS;
    const uint64_t sizes[LF_PARTS] = {floats,
                                      floats,
                                      floats,
                                      floats,
                                      floats,
                                      floats,
                                      LF_PIXELS_MAX,
                                      LF_PIXELS_MAX,
                                      LF_PIXELS_MAX,
                                      floats,
                                      (uint64_t)LF_PICTURE_PIXELS * 4u,
                                      stream_bytes,
                                      windows * 4u,
                                      windows * 4u,
                                      windows,
                                      (uint64_t)LF_STRIP_WIDE * LF_STRIP_HIGH * 4u,
                                      (uint64_t)(LF_DECILES + 1u) * LF_MEDIAN_BINS * 4u,
                                      (uint64_t)LF_PEAKS_MAX * sizeof(lf_peak_t),
                                      (2u * LF_RAY_SAMPLES + 3u * LF_PEAKS_MAX) * 4u,
                                      LF_WINDOWS_MAX * 8u,
                                      (uint64_t)LF_WORKERS * LF_SCRATCH_BYTES,
                                      sizeof(ui_t),
                                      sizeof(texture_t),
                                      sizeof(climate_normals_t),
                                      sizeof(lf_result_t) * LF_SEEDS_MAX};
    uint64_t total = 0u;
    for (uint32_t k = 0u; k < LF_PARTS; k++) {
        total += (sizes[k] + 63u) & ~63ull;
    }
    heap_region_t region = {0};
    if (!heap_reserve(total, &region)) {
        return 2;
    }
    uint8_t *parts[LF_PARTS];
    uint8_t *at = region.owned_base;
    for (uint32_t k = 0u; k < LF_PARTS; k++) {
        parts[k] = at;
        at += (sizes[k] + 63u) & ~63ull;
    }
    const lf_memory_t m = {.h = (float32_t *)(void *)parts[0],
                           .g = (float32_t *)(void *)parts[1],
                           .a = (float32_t *)(void *)parts[2],
                           .b = (float32_t *)(void *)parts[3],
                           .c = (float32_t *)(void *)parts[4],
                           .low = (float32_t *)(void *)parts[5],
                           .plateau = parts[6],
                           .range = parts[7],
                           .knobs = parts[8],
                           .queue = (uint32_t *)(void *)parts[9],
                           .pixels = (uint32_t *)(void *)parts[10],
                           .stream = parts[11],
                           .stream_bytes = stream_bytes,
                           .windows_profile = (float32_t *)(void *)parts[12],
                           .windows_real = (float32_t *)(void *)parts[13],
                           .windows_land = parts[14],
                           .strip = (float32_t *)(void *)parts[15],
                           .median = (uint32_t *)(void *)parts[16],
                           .peaks = (lf_peak_t *)(void *)parts[17],
                           .ray = (float32_t *)(void *)parts[18],
                           .window_origins = (int32_t *)(void *)parts[19],
                           .directory = options.directory};
    ui_t *const ui = (ui_t *)(void *)parts[21];
    texture_t *const texture = (texture_t *)(void *)parts[22];
    climate_normals_t *const normals = (climate_normals_t *)(void *)parts[23];
    lf_result_t *const results = (lf_result_t *)(void *)parts[24];
    thread_topology_t topology;
    job_system_t system;
    if (!thread_topology_read(&topology) || !job_init_count(&system, &topology, LF_WORKERS)) {
        heap_release(&region);
        return 2;
    }
    lf_run_t run = {.borrowed_scratch = parts[20], .jobs = LF_WORKERS};
    int32_t status = 0;
    if (options.self_test != 0u) {
        status = lf_self_test(&system, &run, &m) ? 0 : 1;
    }
    const uint32_t seeds = options.directory == nullptr ? 0u : (first_seed < argc ? (uint32_t)(argc - first_seed) : 1u);
    bool ui_ready = false, texture_ready = false;
    if (seeds > 0u) {
        climate_normals_fill(normals);
        texture_ready = texture_init(texture);
        ui_ready = texture_ready && ui_init(ui, texture);
        lf_memory_t seed_memory = m;
        seed_memory.borrowed_font = ui_ready ? &ui->font : nullptr;
        seed_memory.borrowed_normals = normals;
        if (!ui_ready) {
            printf("bench_landform: no font, so the pictures have no labels\n");
        }
        for (uint32_t s = 0u; s < seeds; s++) {
            const uint64_t seed = first_seed < argc ? strtoull(argv[first_seed + (int32_t)s], nullptr, 0)
                                                    : (uint64_t)TERRAIN_SEED_DEFAULT;
            if (!lf_seed(&system, &run, &seed_memory, &options, seed, &results[s])) {
                status = 1;
            }
        }
        /* Every seed's numbers, and the least and most over them: the rows of plan 007 section 4's table. */
        printf("\n%-40s %12s %12s  per seed\n", "measure", "least", "most");
        for (uint32_t k = 0u; k < LF_V_COUNT; k++) {
            float64_t least = results[0].v[k], most = results[0].v[k];
            for (uint32_t s = 1u; s < seeds; s++) {
                least = results[s].v[k] < least ? results[s].v[k] : least;
                most = results[s].v[k] > most ? results[s].v[k] : most;
            }
            printf("%-40s %12.3f %12.3f ", LF_VALUE_NAME[k], (libc_double_t)least, (libc_double_t)most);
            for (uint32_t s = 0u; s < seeds; s++) {
                printf(" %.3f", (libc_double_t)results[s].v[k]);
            }
            printf("\n");
        }
    }
    if (ui_ready) {
        ui_deinit(ui);
    }
    if (texture_ready) {
        texture_deinit(texture);
    }
    job_deinit(&system);
    heap_release(&region);
    return status;
}
