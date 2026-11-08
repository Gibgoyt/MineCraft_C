/* ctest run_climate_systems. See README.md. The phase 06 file's tests for run/src/climate/systems/ and its parts field/
 * and name/: Poisson counts, the rates, the tracks, the spin, the day list and its determinism, thunderstorm cells, names
 * and the mask's point read. */
#include "harness/index.h"

#include "climate/air/index.h"
#include "climate/index.h"
#include "climate/season/index.h"
#include "climate/systems/field/index.h"
#include "climate/systems/index.h"
#include "climate/systems/name/index.h"
#include "job/index.h"
#include "run/climate/fixtures/ground.h"
#include "terrain/index.h"
#include "terrain/rng/index.h"
#include "thread/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Any change to a rate, a draw's order, a steering rule or a constant of the phase moves this on purpose: the FNV-1a digest
 * of every list of one_and_sixteen_workers_give_the_same_lists, on seed 0x1234567890ABCDEF. Moved by the final check
 * (phase 06 Issues found 22): hurricanes need sea at the point only, at 0.3 a square-day; it was 0x69B94D8AD6C147C9. */
static const uint64_t SYS_DIGEST_PIN = 0x80775D7E2D42F80Bull;
static const uint64_t SYS_SEED = 0x1234567890ABCDEFull;
static const uint64_t SYS_ARENA_BYTES = 8ull << 20;
/* 15 degrees north, 45 north, 45 south and 45 north in a strip mirrored past the pole (e = -1). */
static const float64_t SYS_Y15 = 15.0f64 * CLIMATE_POLE_Y / 90.0f64;
static const float64_t SYS_Y45 = 19148.936170212766f64;
static const float64_t SYS_Y45_MIRRORED = 57446.808510638298f64;

typedef struct sys_rig_t {
    job_system_t *job;           /* 0 */
    climate_t *climate;          /* 8 */
    thread_topology_t *topology; /* 16 */
} sys_rig_t;
static_assert(sizeof(sys_rig_t) == 24);

static void *sys_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static bool sys_rig_start(test_context_t *t, sys_rig_t *rig, uint32_t workers) {
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    rig->job = sys_aligned(t, sizeof(job_system_t));
    rig->climate = sys_aligned(t, sizeof(climate_t));
    if (!thread_topology_read(rig->topology) || !job_init_count(rig->job, rig->topology, workers)) {
        return false;
    }
    if (!climate_init(rig->climate)) {
        job_deinit(rig->job);
        return false;
    }
    return true;
}

static void sys_rig_stop(sys_rig_t *rig) {
    climate_close(rig->climate, rig->job);
    climate_deinit(rig->climate);
    job_deinit(rig->job);
}

static arena_t *sys_arena(test_context_t *t, uint64_t bytes) {
    arena_t *const arena = sys_aligned(t, sizeof(arena_t));
    arena_init(arena, "test_systems", sys_aligned(t, bytes), bytes);
    return arena;
}

static climate_systems_mask_t *sys_mask(test_context_t *t, const climate_t *climate) {
    climate_systems_mask_t *const mask = test_alloc(t, sizeof(climate_systems_mask_t));
    climate_systems_mask_start(mask, climate, sys_arena(t, 1ull << 20), nullptr);
    return mask;
}

/* A system placed by hand, sitting still for its whole life: every sample at (x, y), weaken 1. */
static void sys_hand(climate_system_t *system, uint8_t kind, float64_t x, float64_t y, float64_t spawn_day,
                     float32_t life_days, float32_t peak_hpa, float32_t radius, float32_t core) {
    memset(system, 0, sizeof(*system));
    system->kind = kind;
    system->spawn_day = spawn_day;
    system->start_x = x;
    system->start_y = y;
    system->life_days = life_days;
    system->peak_hpa = peak_hpa;
    system->radius = radius;
    system->core_radius = core;
    system->front_length = 8000.0f32;
    system->shield_mm_hour = 2.0f32;
    uint32_t count = (uint32_t)floor((float64_t)life_days / CLIMATE_TRACK_STEP_DAYS) + 1u;
    count = count > CLIMATE_TRACK_SAMPLES_MAX ? CLIMATE_TRACK_SAMPLES_MAX : count;
    for (uint32_t k = 0u; k < count; k++) {
        system->samples[k].x = x;
        system->samples[k].y = y;
        system->samples[k].weaken = 1.0f32;
    }
    system->sample_count = (uint8_t)count;
}

static climate_systems_day_t sys_day_of(climate_system_t *systems, uint32_t count) {
    climate_systems_day_t day;
    memset(&day, 0, sizeof(day));
    day.borrowed_systems = systems;
    day.system_count = count;
    day.system_capacity = count;
    return day;
}

static uint64_t sys_fnv(uint64_t digest, const void *bytes, uint64_t count) {
    const uint8_t *const at = bytes;
    for (uint64_t i = 0u; i < count; i++) {
        digest ^= at[i];
        digest *= 0x100000001B3ull;
    }
    return digest;
}

static uint64_t sys_list_digest(uint64_t digest, const climate_systems_day_t *day) {
    digest = sys_fnv(digest, &day->system_count, sizeof(day->system_count));
    digest = sys_fnv(digest, day->borrowed_systems, (uint64_t)day->system_count * sizeof(climate_system_t));
    digest = sys_fnv(digest, &day->thunder_count, sizeof(day->thunder_count));
    return sys_fnv(digest, day->borrowed_thunder, (uint64_t)day->thunder_count * sizeof(climate_thunder_t));
}

/* ---- Counts ----------------------------------------------------------------------------------------------------------- */

TEST(poisson_counts_follow_their_rate) {
    CHECK_EQ_U64(climate_systems_poisson(0.5f64, 0.02f64, exp(-0.02f64), 8u), 0u);
    CHECK_EQ_U64(climate_systems_poisson(0.985f64, 0.02f64, exp(-0.02f64), 8u), 1u);
    CHECK_EQ_U64(climate_systems_poisson(0.9999f64, 0.02f64, exp(-0.02f64), 8u), 2u);
    CHECK_EQ_U64(climate_systems_poisson(0.5f64, 9.0f64, exp(-9.0f64), 64u), 9u);
    /* u from the hash's top 53 bits, exactly as terrain_rng_next_f64 makes it from a stream's first number. */
    terrain_rng_t rng = terrain_rng_start(77u);
    const uint64_t raw = terrain_rng_next_u64(&rng);
    rng = terrain_rng_start(77u);
    CHECK(climate_systems_uniform(raw) == terrain_rng_next_f64(&rng));
    static const float64_t LAMBDAS[3] = {0.02f64, 0.08f64, 9.0f64};
    for (uint32_t l = 0u; l < 3u; l++) {
        const float64_t lambda = LAMBDAS[l], e = exp(-lambda);
        uint64_t n = 0u, zeros = 0u;
        float64_t sum = 0.0f64, sum2 = 0.0f64;
        for (int32_t sy = 0; sy < 1000; sy++) {
            for (int32_t d = 0; d < 1000; d++) {
                const uint64_t h = terrain_rng_hash3(0xC0FFEEu + l, 17, sy, d);
                const uint32_t k = climate_systems_poisson(climate_systems_uniform(h), lambda, e, 64u);
                sum += (float64_t)k;
                sum2 += (float64_t)k * (float64_t)k;
                zeros += k == 0u ? 1u : 0u;
                n++;
            }
        }
        const float64_t mean = sum / (float64_t)n, variance = sum2 / (float64_t)n - mean * mean;
        const float64_t zero_share = (float64_t)zeros / (float64_t)n;
        printf("lambda %.2f: mean %.5f, variance %.5f, zeros %.5f (e^-lambda %.5f)\n", (libc_double_t)lambda,
               (libc_double_t)mean, (libc_double_t)variance, (libc_double_t)zero_share, (libc_double_t)e);
        CHECK(fabs(mean - lambda) <= 0.01f64 * lambda);
        CHECK(fabs(variance - lambda) <= 0.02f64 * lambda);
        CHECK(fabs(zero_share - e) <= 0.001f64);
    }
}

/* ---- Rates ------------------------------------------------------------------------------------------------------------ */

TEST(the_machinery_gives_the_plan_s_2_4_hurricanes_a_season) {
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, 1u));
    const ground_t sea = {.kind = GROUND_ALL_SEA};
    REQUIRE(climate_open_ground(rig.climate, SYS_SEED, ground_fn, &sea));
    climate_systems_mask_t *const mask = sys_mask(t, rig.climate);
    climate_system_t *const out = test_alloc(t, CLIMATE_SPAWNS_MAX * sizeof(climate_system_t));
    const climate_systems_hooks_t hooks = {.hurricane_s = 1.0f32, .hurricane_first = 150u, .hurricane_last = 269u};
    uint64_t kept[2] = {0u, 0u};
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        const climate_systems_hooks_t off = {0};
        climate_systems_hooks_set(rig.climate, pass == 0u ? &hooks : &off);
        /* 2000 squares: 1000 along x in the band from the equator to 38.5 N and 1000 from it to 38.5 S. */
        for (int32_t sy = -1; sy <= 0; sy++) {
            for (int32_t sx = 0; sx < 1000; sx++) {
                for (int64_t d = 0; d < 365; d++) {
                    bool full = false;
                    kept[pass] += climate_systems_square_spawns(rig.climate, mask, CLIMATE_SYSTEM_HURRICANE, sx, sy, d,
                                                                out, CLIMATE_SPAWNS_MAX, &full);
                    CHECK(!full);
                }
            }
        }
    }
    const float64_t per_square = (float64_t)kept[0] / 2000.0f64;
    printf("forced season of 120 days: %.3f hurricanes a square (the plan's 2.4 at its 0.02, rate x 120 at ours); the "
           "real sea, no current: %.3f a square a "
           "year\n",
           (libc_double_t)per_square, (libc_double_t)((float64_t)kept[1] / 2000.0f64));
    CHECK_NEAR(per_square, 120.0f64 * (float64_t)CLIMATE_HURRICANE_RATE,
               0.06f64 * 120.0f64 * (float64_t)CLIMATE_HURRICANE_RATE);
    sys_rig_stop(&rig);
}

TEST(hurricane_season_sea_is_32_with_a_warm_current_and_27_95_without) {
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, 1u));
    const ground_t sea = {.kind = GROUND_ALL_SEA};
    REQUIRE(climate_open_ground(rig.climate, SYS_SEED, ground_fn, &sea));
    const float32_t bare = climate_systems_sea_temperature(rig.climate, SYS_Y15, 215.0f64);
    const float32_t s_bare = climate_systems_hurricane_s(rig.climate, SYS_Y15, 215.0f64);
    const climate_systems_hooks_t hooks = {.hurricane_current_c = CLIMATE_CURRENT_WARM_C};
    climate_systems_hooks_set(rig.climate, &hooks);
    const float32_t warm = climate_systems_sea_temperature(rig.climate, SYS_Y15, 215.0f64);
    const float32_t s_warm = climate_systems_hurricane_s(rig.climate, SYS_Y15, 215.0f64);
    climate_cell_t cell;
    memset(&cell, 0, sizeof(cell));
    cell.lat_deg = 15.0f32;
    cell.flags = CLIMATE_CELL_SEA;
    cell.current = CLIMATE_CURRENT_WARM;
    cell.current_strength = 255u;
    const float32_t phase05 = climate_sea_temperature(rig.climate, &cell, 215.0f32);
    printf("15 N day 215: %.3f C (s %.4f); with a +4 current %.3f (s %.4f), phase 05's cell %.4f\n",
           (libc_double_t)bare, (libc_double_t)s_bare, (libc_double_t)warm, (libc_double_t)s_warm,
           (libc_double_t)phase05);
    CHECK_NEAR(bare, 27.95f32, 0.01f32);
    CHECK_NEAR(s_bare, 0.725f32, 0.005f32);
    CHECK_NEAR(warm, 31.95f32, 0.01f32);
    CHECK_NEAR(s_warm, 1.0f32, 0.0f32);
    CHECK_NEAR(warm, phase05, 1.0e-4f32);
    sys_rig_stop(&rig);
}

TEST(hurricanes_only_spawn_in_season_and_never_within_5_degrees) {
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, 1u));
    const ground_t sea = {.kind = GROUND_ALL_SEA};
    REQUIRE(climate_open_ground(rig.climate, SYS_SEED, ground_fn, &sea));
    climate_systems_mask_t *const mask = sys_mask(t, rig.climate);
    climate_system_t *const out = test_alloc(t, CLIMATE_SPAWNS_MAX * sizeof(climate_system_t));
    CHECK_NEAR(climate_systems_hurricane_s(rig.climate, SYS_Y15, 32.0f64), 0.0f32, 0.0f32);
    uint64_t kept = 0u, samples = 0u, bad_season = 0u, bad_start = 0u, bad_sample = 0u, day_32 = 0u;
    float64_t lowest = 90.0f64;
    /* y from -32768 to 32768: squares -2 to 1, 8 wide, 20 years. */
    for (int64_t d = 0; d < 20 * 365; d++) {
        for (int32_t sy = -2; sy <= 1; sy++) {
            for (int32_t sx = 0; sx < 8; sx++) {
                bool full = false;
                const uint32_t n = climate_systems_square_spawns(rig.climate, mask, CLIMATE_SYSTEM_HURRICANE, sx, sy, d,
                                                                 out, CLIMATE_SPAWNS_MAX, &full);
                for (uint32_t i = 0u; i < n; i++) {
                    const climate_system_t *const h = &out[i];
                    kept++;
                    const float64_t phi = fabs(climate_latitude_deg(h->start_y));
                    bad_season += climate_systems_hurricane_s(rig.climate, h->start_y, h->spawn_day) > 0.0f32 ? 0u : 1u;
                    bad_start += (phi >= 5.0f64 && phi <= 25.0f64) ? 0u : 1u;
                    const float64_t doy = climate_systems_day_of_year(h->spawn_day);
                    /* North of the equator only: day 32 is the southern summer. */
                    day_32 +=
                        (doy >= 32.0f64 && doy < 33.0f64 && h->start_y > 0.0f64 && phi >= 10.0f64 && phi <= 20.0f64)
                            ? 1u
                            : 0u;
                    for (uint32_t k = 0u; k < h->sample_count; k++) {
                        const float64_t at = fabs(climate_latitude_deg(h->samples[k].y));
                        lowest = at < lowest ? at : lowest;
                        bad_sample += at >= 5.0f64 ? 0u : 1u;
                        samples++;
                    }
                }
            }
        }
    }
    printf("20 years, 32 squares of sea: %" PRIu64 " hurricanes, %" PRIu64 " samples, lowest |phi| %.3f\n", kept,
           samples, (libc_double_t)lowest);
    CHECK(kept > 0u);
    CHECK_EQ_U64(bad_season, 0u);
    CHECK_EQ_U64(bad_start, 0u);
    CHECK_EQ_U64(bad_sample, 0u);
    CHECK_EQ_U64(day_32, 0u);
    sys_rig_stop(&rig);
}

/* The poleward part of a step, in world terms: dy times e s_h at the step's start. */
static float64_t sys_poleward(float64_t y, float64_t dy) {
    const float64_t phi = climate_latitude_deg(y);
    return dy * (float64_t)climate_air_mirror_sign(y) * (phi >= 0.0f64 ? 1.0f64 : -1.0f64);
}

TEST(hurricanes_turn_poleward_and_then_east) {
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, 1u));
    const ground_t sea = {.kind = GROUND_ALL_SEA};
    REQUIRE(climate_open_ground(rig.climate, SYS_SEED, ground_fn, &sea));
    climate_systems_mask_t *const mask = sys_mask(t, rig.climate);
    climate_system_t *const out = test_alloc(t, CLIMATE_SPAWNS_MAX * sizeof(climate_system_t));
    /* The season forced on, so many hurricanes form: it is the steering that is tested here. */
    const climate_systems_hooks_t hooks = {.hurricane_s = 1.0f32, .hurricane_first = 0u, .hurricane_last = 364u};
    climate_systems_hooks_set(rig.climate, &hooks);
    uint64_t turned = 0u, west_bad = 0u, turn_bad = 0u, east_bad = 0u;
    float64_t worst_turn = 0.0f64;
    for (int64_t d = 0; d < 20 * 365; d += 3) {
        for (int32_t sy = -1; sy <= 0; sy++) {
            for (int32_t sx = 0; sx < 4; sx++) {
                bool full = false;
                const uint32_t n = climate_systems_square_spawns(rig.climate, mask, CLIMATE_SYSTEM_HURRICANE, sx, sy, d,
                                                                 out, CLIMATE_SPAWNS_MAX, &full);
                for (uint32_t i = 0u; i < n; i++) {
                    const climate_system_t *const h = &out[i];
                    uint32_t first_turn = UINT32_MAX; /* none yet: a turn may begin at the spawn itself */
                    bool reaches_east = false;
                    for (uint32_t k = 0u; k + 1u < h->sample_count; k++) {
                        const float64_t dx = h->samples[k + 1u].x - h->samples[k].x;
                        const float64_t dy = h->samples[k + 1u].y - h->samples[k].y;
                        const bool polar = fabs(climate_latitude_deg(h->samples[k].y)) > 85.0f64;
                        if (h->samples[k].stage == 0u &&
                            (dx >= 0.0f64 || (!polar && !(sys_poleward(h->samples[k].y, dy) > 0.0f64)))) {
                            west_bad++;
                        }
                        if (h->samples[k].stage >= 1u && first_turn == UINT32_MAX) {
                            first_turn = k;
                        }
                        reaches_east = reaches_east || h->samples[k].stage == 2u;
                    }
                    if (!reaches_east || first_turn == UINT32_MAX || first_turn + 9u >= h->sample_count) {
                        continue;
                    }
                    turned++;
                    /* One day after the turn begins, heading within 15 degrees of due poleward. */
                    const climate_track_sample_t *const a = &h->samples[first_turn + 4u];
                    const climate_track_sample_t *const b = &h->samples[first_turn + 5u];
                    const float64_t dx = b->x - a->x, pole = sys_poleward(a->y, b->y - a->y);
                    const float64_t off = fabs(atan2(dx, pole)) * 180.0f64 / CLIMATE_PI;
                    worst_turn = off > worst_turn ? off : worst_turn;
                    turn_bad += off <= 15.0f64 ? 0u : 1u;
                    /* Two days after, east. */
                    const climate_track_sample_t *const c = &h->samples[first_turn + 8u];
                    const climate_track_sample_t *const e = &h->samples[first_turn + 9u];
                    east_bad += e->x - c->x > 0.0f64 ? 0u : 1u;
                }
            }
        }
    }
    printf("hurricanes that reached stage 2: %" PRIu64
           "; worst heading a day into the turn %.2f degrees off poleward\n",
           turned, (libc_double_t)worst_turn);
    CHECK(turned > 20u);
    CHECK_EQ_U64(west_bad, 0u);
    CHECK_EQ_U64(turn_bad, 0u);
    CHECK_EQ_U64(east_bad, 0u);
    /* Poleward in world terms: +y at 15 N, -y at 15 S, and -y at 15 N in a strip mirrored past a pole. */
    static const float64_t STARTS[3] = {6382.978723404255f64, -6382.978723404255f64, 70212.765957446809f64};
    static const float64_t SIGNS[3] = {1.0f64, -1.0f64, -1.0f64};
    for (uint32_t s = 0u; s < 3u; s++) {
        climate_system_t h;
        sys_hand(&h, CLIMATE_SYSTEM_HURRICANE, 1000.0f64, STARTS[s], 200.0f64, 6.0f32, -60.0f32, 2500.0f32, 100.0f32);
        h.speed = 1.0f32;
        h.turn_psi_deg = 89.0f32; /* never turns */
        climate_systems_track(rig.climate, mask, &h);
        CHECK(h.samples[1].x < h.samples[0].x);
        CHECK((h.samples[1].y - h.samples[0].y) * SIGNS[s] > 0.0f64);
    }
    /* And a low's drift at 45 S and 45 N mirrored: -y both. */
    static const float64_t LOWS[2] = {-19148.936170212766f64, 57446.808510638298f64};
    for (uint32_t s = 0u; s < 2u; s++) {
        climate_system_t low;
        sys_hand(&low, CLIMATE_SYSTEM_LOW, 0.0f64, LOWS[s], 10.0f64, 4.0f32, -20.0f32, 5000.0f32, 2500.0f32);
        low.speed = 6000.0f32;
        climate_systems_track(rig.climate, mask, &low);
        CHECK(low.samples[1].y < low.samples[0].y);
        CHECK(low.samples[1].x > low.samples[0].x);
    }
    sys_rig_stop(&rig);
}

TEST(hurricanes_weaken_by_half_a_day_over_land) {
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, 1u));
    /* Sea, with a land square 40 km across round (0, 15 N). */
    const ground_t land = ground_continent(-20000, (int32_t)SYS_Y15 - 20000, 20000, (int32_t)SYS_Y15 + 20000);
    REQUIRE(climate_open_ground(rig.climate, SYS_SEED, ground_fn, &land));
    climate_systems_mask_t *const mask = sys_mask(t, rig.climate);
    climate_system_t h;
    sys_hand(&h, CLIMATE_SYSTEM_HURRICANE, 0.0f64, SYS_Y15, 215.0f64, 9.0f32, -90.0f32, 2500.0f32, 100.0f32);
    h.speed = 1.0f32;
    h.turn_psi_deg = 28.0f32;
    climate_systems_track(rig.climate, mask, &h);
    REQUIRE(h.sample_count > 5u);
    for (uint32_t k = 0u; k < 5u; k++) {
        CHECK_EQ_U64(h.samples[k].over_land, 1u);
    }
    printf("over land: weaken %.7f after 4 samples, %u samples\n", (libc_double_t)h.samples[4].weaken, h.sample_count);
    CHECK_NEAR(h.samples[4].weaken / h.samples[0].weaken, 0.5f32, 1.0e-6f32);
    /* Over open sea at 15 N in season it stays. */
    sys_hand(&h, CLIMATE_SYSTEM_HURRICANE, 60000.0f64, SYS_Y15, 215.0f64, 9.0f32, -90.0f32, 2500.0f32, 100.0f32);
    h.speed = 1.0f32;
    h.turn_psi_deg = 28.0f32;
    climate_systems_track(rig.climate, mask, &h);
    REQUIRE(h.sample_count > 5u);
    CHECK_EQ_U64(h.samples[4].over_land, 0u);
    CHECK_NEAR(h.samples[4].weaken, 1.0f32, 0.0f32);
    /* Over sea under 26 C, late in the season at 25 N: halves the same way. */
    const float64_t y25 = 25.0f64 * CLIMATE_POLE_Y / 90.0f64;
    const float32_t cool = climate_systems_sea_temperature(rig.climate, y25, 330.0f64);
    printf("25 N on day 330: %.2f C\n", (libc_double_t)cool);
    REQUIRE(cool < CLIMATE_HURRICANE_COOL_SEA_C);
    sys_hand(&h, CLIMATE_SYSTEM_HURRICANE, 60000.0f64, y25, 330.0f64, 9.0f32, -90.0f32, 2500.0f32, 100.0f32);
    h.speed = 1.0f32;
    h.turn_psi_deg = 89.0f32;
    climate_systems_track(rig.climate, mask, &h);
    REQUIRE(h.sample_count > 5u);
    CHECK_NEAR(h.samples[4].weaken / h.samples[0].weaken, 0.5f32, 1.0e-6f32);
    sys_rig_stop(&rig);
}

/* ---- Fields ----------------------------------------------------------------------------------------------------------- */

/* The 2D cross product of the outward unit vector and the wind: positive is anticlockwise. */
static void sys_spin_four(test_context_t *t, const climate_systems_day_t *day, float64_t cx, float64_t cy, float64_t r,
                          float64_t sense, float32_t speed) {
    static const float64_t OUT[4][2] = {{1.0f64, 0.0f64}, {0.0f64, 1.0f64}, {-1.0f64, 0.0f64}, {0.0f64, -1.0f64}};
    for (uint32_t k = 0u; k < 4u; k++) {
        float32_t w[2];
        climate_systems_wind_at(day, cx + r * OUT[k][0], cy + r * OUT[k][1], 3.0f64, w);
        const float64_t cross = OUT[k][0] * (float64_t)w[1] - OUT[k][1] * (float64_t)w[0];
        CHECK(cross * sense > 0.0f64);
        CHECK_NEAR(sqrtf(w[0] * w[0] + w[1] * w[1]), speed, 1.0e-4f32);
    }
}

TEST(winds_circle_lows_anticlockwise_in_the_north_and_clockwise_in_the_south) {
    static const float64_t PLACES[3] = {19148.936170212766f64, -19148.936170212766f64, 57446.808510638298f64};
    /* Anticlockwise (+1) round a low in the real north; the south and the mirrored north reverse it. */
    static const float64_t LOW_SENSE[3] = {1.0f64, -1.0f64, -1.0f64};
    for (uint32_t p = 0u; p < 3u; p++) {
        climate_system_t systems[1];
        sys_hand(&systems[0], CLIMATE_SYSTEM_LOW, 0.0f64, PLACES[p], 0.0f64, 6.0f32, -20.0f32, 5000.0f32, 2500.0f32);
        climate_systems_day_t day = sys_day_of(systems, 1u);
        sys_spin_four(t, &day, 0.0f64, PLACES[p], 2500.0f64, LOW_SENSE[p], 8.0f32);
        /* East of a northern low the wind blows north. */
        if (p == 0u) {
            float32_t w[2];
            climate_systems_wind_at(&day, 2500.0f64, PLACES[p], 3.0f64, w);
            CHECK(w[1] > 7.99f32 && fabsf(w[0]) < 1.0e-4f32);
        }
        sys_hand(&systems[0], CLIMATE_SYSTEM_HIGH, 0.0f64, PLACES[p], 0.0f64, 6.0f32, 14.0f32, 5000.0f32, 2500.0f32);
        day = sys_day_of(systems, 1u);
        sys_spin_four(t, &day, 0.0f64, PLACES[p], 2500.0f64, -LOW_SENSE[p], 5.6f32);
    }
    CHECK_NEAR(climate_air_mirror_sign(SYS_Y45_MIRRORED), -1.0f32, 0.0f32);
    CHECK_NEAR(climate_air_mirror_sign(SYS_Y45), 1.0f32, 0.0f32);
}

TEST(the_hurricane_wind_and_pressure_are_the_worked_examples) {
    climate_system_t systems[1];
    sys_hand(&systems[0], CLIMATE_SYSTEM_HURRICANE, 0.0f64, SYS_Y15, 0.0f64, 9.0f32, -60.0f32, 2500.0f32, 100.0f32);
    const climate_systems_day_t day = sys_day_of(systems, 1u);
    climate_system_state_t state;
    climate_system_state_at(&systems[0], 4.5f64, &state);
    CHECK_NEAR(state.pressure_hpa, -60.0f32, 1.0e-5f32);
    CHECK_NEAR(state.wind_max, 51.5f32, 1.0e-4f32);
    float32_t w[2];
    climate_systems_wind_at(&day, 50.0f64, SYS_Y15, 4.5f64, w);
    CHECK_NEAR(sqrtf(w[0] * w[0] + w[1] * w[1]), 25.75f32, 1.0e-3f32);
    climate_systems_wind_at(&day, 100.0f64, SYS_Y15, 4.5f64, w);
    CHECK_NEAR(sqrtf(w[0] * w[0] + w[1] * w[1]), 51.5f32, 1.0e-3f32);
    climate_systems_wind_at(&day, 1000.0f64, SYS_Y15, 4.5f64, w);
    const float32_t at_1000 = sqrtf(w[0] * w[0] + w[1] * w[1]);
    const float32_t p1250 = climate_systems_pressure_at(&day, 1250.0f64, SYS_Y15, 4.5f64);
    const float32_t p2500 = climate_systems_pressure_at(&day, 0.0f64, SYS_Y15 + 2500.0f64, 4.5f64);
    printf("hurricane -60 hPa: wind %.3f at 1000; pressure %.3f at 1250, %.3f at 2500\n", (libc_double_t)at_1000,
           (libc_double_t)p1250, (libc_double_t)p2500);
    CHECK_NEAR(at_1000, 12.94f32, 0.01f32);
    CHECK_NEAR(p1250, -46.73f32, 0.01f32);
    CHECK_NEAR(p2500, -22.07f32, 0.01f32);
    CHECK_NEAR(climate_systems_pressure_at(&day, 7600.0f64, SYS_Y15, 4.5f64), 0.0f32, 0.0f32);
    climate_systems_wind_at(&day, 7600.0f64, SYS_Y15, 4.5f64, w);
    CHECK_NEAR(w[0], 0.0f32, 0.0f32);
    CHECK_NEAR(w[1], 0.0f32, 0.0f32);
    CHECK_NEAR(climate_systems_hurricane_vmax(90.0f32), 70.0f32, 1.0e-4f32);
    CHECK_NEAR(climate_systems_hurricane_vmax(15.0f32), 16.5f32, 1.0e-4f32);
    /* Eyewall rain, 40 x 60 / 90 at r_m times its band factor (0 to 1), and nearly dry in the middle of the eye. */
    const float32_t eyewall = climate_systems_rain_at(&day, 100.0f64, SYS_Y15, 4.5f64);
    CHECK(eyewall >= 0.0f32 && eyewall <= 26.667f32);
    CHECK(climate_systems_rain_at(&day, 5.0f64, SYS_Y15, 4.5f64) < 0.1f32);
    char title[CLIMATE_SYSTEM_TITLE_BYTES];
    climate_system_title(&systems[0], 4.5f64, title);
    CHECK(strncmp(title, "Hurricane ", 10u) == 0);
    sys_hand(&systems[0], CLIMATE_SYSTEM_HURRICANE, 0.0f64, SYS_Y15, 0.0f64, 9.0f32, -15.0f32, 2500.0f32, 100.0f32);
    climate_system_state_at(&systems[0], 4.5f64, &state);
    CHECK_NEAR(state.wind_max, 16.5f32, 1.0e-4f32);
    climate_system_title(&systems[0], 4.5f64, title);
    CHECK(strncmp(title, "Tropical storm ", 15u) == 0);
}

TEST(lows_carry_a_front_toward_the_equator) {
    static const float64_t PLACES[2] = {19148.936170212766f64, -19148.936170212766f64};
    for (uint32_t p = 0u; p < 2u; p++) {
        climate_system_t systems[1];
        sys_hand(&systems[0], CLIMATE_SYSTEM_LOW, 0.0f64, PLACES[p], 0.0f64, 6.0f32, -20.0f32, 5000.0f32, 2500.0f32);
        const climate_systems_day_t day = sys_day_of(systems, 1u);
        float64_t from[2], to[2];
        REQUIRE(climate_systems_front(&systems[0], 3.0f64, from, to));
        CHECK(to[0] < from[0]);
        CHECK(p == 0u ? to[1] < from[1] : to[1] > from[1]); /* south-west in the north, north-west in the south */
        /* 300 blocks off the line's middle is on it, 500 is not. */
        const float64_t mx = 0.5f64 * (from[0] + to[0]), my = 0.5f64 * (from[1] + to[1]);
        const float64_t dx = to[0] - from[0], dy = to[1] - from[1], length = sqrt(dx * dx + dy * dy);
        const float64_t nx = -dy / length, ny = dx / length;
        CHECK(climate_systems_on_front(&day, mx + 300.0f64 * nx, my + 300.0f64 * ny, 3.0f64));
        CHECK(!climate_systems_on_front(&day, mx + 500.0f64 * nx, my + 500.0f64 * ny, 3.0f64));
        CHECK(!climate_systems_on_front(&day, mx, my, 7.0f64)); /* the low is gone */
    }
    CHECK_NEAR(climate_systems_rain_share(0.0f32), 0.3f32, 1.0e-6f32);
    CHECK_NEAR(climate_systems_rain_share(-50.0f32), 0.5f32, 1.0e-6f32);
    CHECK_NEAR(climate_systems_rain_share(37.5f32), 0.4f32, 1.0e-6f32);
    CHECK_NEAR(climate_systems_rain_share(65.0f32), 0.45f32, 1.0e-6f32);
    CHECK_NEAR(climate_systems_rain_share(80.0f32), 0.4f32, 1.0e-6f32);
    /* The day's share: the whole of it when cells can form or lows come by, none when neither can. */
    CHECK_NEAR(climate_systems_rain_share_today(0.0f32, 100.0f32, 26.0f32, 0.0f32), 0.3f32, 1.0e-6f32);
    CHECK_NEAR(climate_systems_rain_share_today(45.0f32, 10.0f32, 2.0f32, 0.0f32), climate_systems_rain_share(45.0f32),
               0.0f32);
    CHECK_NEAR(climate_systems_rain_share_today(10.0f32, 100.0f32, 15.0f32, 0.0f32), 0.0f32, 0.0f32);
    /* Hurricane season adds its share, times s_hur (the final check, Issues found 22). */
    CHECK_NEAR(climate_systems_rain_share_today(15.0f32, 200.0f32, 26.0f32, 1.0f32),
               0.3f32 + CLIMATE_SYSTEM_RAIN_SHARE_HURRICANE, 1.0e-6f32);
    CHECK_NEAR(climate_systems_rain_share_today(10.0f32, 100.0f32, 15.0f32, 0.5f32),
               0.5f32 * CLIMATE_SYSTEM_RAIN_SHARE_HURRICANE, 1.0e-6f32);
    /* Wetness and a cell's rain scale: a hot, wet, non-monsoon day's cells at the base; a monsoon's three times lighter. */
    CHECK_NEAR(climate_systems_wetness(10.0f32), 2.0f32, 1.0e-6f32);
    CHECK_NEAR(climate_systems_wetness(-1.0f32), 0.0f32, 0.0f32);
    CHECK_NEAR(climate_systems_thunder_rain_scale(30.0f32, 1.0f32, false), CLIMATE_THUNDER_RAIN_SCALE_BASE, 1.0e-6f32);
    CHECK_NEAR(climate_systems_thunder_rain_scale(30.0f32, 1.0f32, true), CLIMATE_THUNDER_RAIN_SCALE_BASE / 3.0f32,
               1.0e-6f32);
    CHECK_NEAR(climate_systems_thunder_rain_scale(18.5f32, 0.5f32, false), CLIMATE_THUNDER_RAIN_SCALE_BASE * 10.0f32,
               1.0e-5f32);
    CHECK_NEAR(climate_systems_thunder_rain_scale(30.0f32, 0.0f32, false), 0.0f32, 0.0f32);
}

/* ---- Names ------------------------------------------------------------------------------------------------------------ */

TEST(names_are_made_from_syllables) {
    char name[CLIMATE_SYSTEM_NAME_BYTES];
    climate_system_name(4161u, name);
    CHECK_STR_EQ(name, "Tavela");
    climate_system_name(262u, name);
    CHECK_STR_EQ(name, "Orin");
    uint64_t long_names = 0u, lower = 0u, too_long_titles = 0u, three = 0u;
    climate_system_t storm;
    sys_hand(&storm, CLIMATE_SYSTEM_HURRICANE, 0.0f64, SYS_Y15, 0.0f64, 9.0f32, -15.0f32, 2500.0f32, 100.0f32);
    terrain_rng_t rng = terrain_rng_start(5u);
    for (uint32_t i = 0u; i < 1000000u; i++) {
        storm.name_seed = terrain_rng_next_u32(&rng);
        climate_system_name(storm.name_seed, name);
        const uint64_t length = strlen(name);
        long_names += length > 8u ? 1u : 0u;
        three += length > 6u ? 1u : 0u;
        lower += (name[0] >= 'A' && name[0] <= 'Z') ? 0u : 1u;
        char title[CLIMATE_SYSTEM_TITLE_BYTES + 8u];
        memset(title, 0, sizeof(title));
        climate_system_title(&storm, 4.5f64, title);
        too_long_titles += strlen(title) + 1u > CLIMATE_SYSTEM_TITLE_BYTES ? 1u : 0u;
    }
    printf("10^6 names: %" PRIu64 " over 6 letters\n", three);
    CHECK_EQ_U64(long_names, 0u);
    CHECK_EQ_U64(lower, 0u);
    CHECK_EQ_U64(too_long_titles, 0u);
}

/* ---- The list --------------------------------------------------------------------------------------------------------- */

static bool sys_list_sorted(const climate_systems_day_t *day) {
    for (uint32_t i = 1u; i < day->system_count; i++) {
        const climate_system_t *const a = &day->borrowed_systems[i - 1u], *const b = &day->borrowed_systems[i];
        const bool before =
            a->kind < b->kind ||
            (a->kind == b->kind &&
             (a->spawn_day < b->spawn_day ||
              (a->spawn_day == b->spawn_day &&
               (a->square_y < b->square_y || (a->square_y == b->square_y && a->square_x < b->square_x)))));
        if (!before) {
            return false;
        }
    }
    for (uint32_t i = 1u; i < day->thunder_count; i++) {
        const climate_thunder_t *const a = &day->borrowed_thunder[i - 1u], *const b = &day->borrowed_thunder[i];
        if (!(a->spawn_day < b->spawn_day || (a->spawn_day == b->spawn_day && a->id < b->id))) {
            return false;
        }
    }
    return true;
}

TEST(a_system_s_track_is_the_same_whichever_day_it_s_first_asked_for) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, SYS_SEED));
    sys_rig_t a, b;
    REQUIRE(sys_rig_start(t, &a, 1u));
    REQUIRE(sys_rig_start(t, &b, 1u));
    REQUIRE(climate_open(a.climate, &terrain));
    climate_systems_day_t one, other;
    arena_t *const arena_a = sys_arena(t, SYS_ARENA_BYTES);
    arena_t *const arena_b = sys_arena(t, SYS_ARENA_BYTES);
    REQUIRE(climate_systems_day_build(a.climate, arena_a, 0.0f64, 15000.0f64, 20000.0f32, 0.0f32, 1005,
                                      CLIMATE_SYSTEMS_BIG, &one));
    printf("day 1005 round (0, 15000): %u big systems\n", one.system_count);
    CHECK(one.system_count > 0u);
    CHECK(sys_list_sorted(&one));
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        /* Fresh each pass: forwards from 1000 to 1005, then backwards from 1010 to 1005. */
        climate_close(b.climate, b.job);
        REQUIRE(climate_open(b.climate, &terrain));
        for (int64_t step = 0; step <= 5; step++) {
            const int64_t d = pass == 0u ? 1000 + step : 1010 - step;
            arena_reset(arena_b);
            REQUIRE(climate_systems_day_build(b.climate, arena_b, 0.0f64, 15000.0f64, 20000.0f32, 0.0f32, d,
                                              CLIMATE_SYSTEMS_BIG, &other));
        }
        CHECK_EQ_I64(other.day, 1005);
        CHECK_EQ_U64(other.system_count, one.system_count);
        for (uint32_t i = 0u; i < one.system_count; i++) {
            bool found = false;
            for (uint32_t j = 0u; j < other.system_count; j++) {
                if (other.borrowed_systems[j].id == one.borrowed_systems[i].id) {
                    found = true;
                    CHECK_MEM_EQ(&other.borrowed_systems[j], &one.borrowed_systems[i], sizeof(climate_system_t));
                }
            }
            CHECK(found);
        }
    }
    sys_rig_stop(&b);
    sys_rig_stop(&a);
    terrain_deinit(&terrain);
}

enum : uint32_t {
    SYS_PLACES = 64u,
    SYS_DAYS = 100u,
};

typedef struct sys_work_t {
    const climate_t *borrowed_climate; /* 0 */
    uint64_t *borrowed_digests;        /* 8: SYS_DAYS of them */
    uint32_t place;                    /* 16 */
    uint32_t overflowed;               /* 20 */
} sys_work_t;
static_assert(sizeof(sys_work_t) == 24);

static void sys_place_xy(uint32_t place, float64_t *x, float64_t *y) {
    *x = -30000.0f64 + 7500.0f64 * (float64_t)(place % 8u) + 13.0f64 * (float64_t)place;
    *y = -30000.0f64 + 8500.0f64 * (float64_t)(place / 8u) - 7.0f64 * (float64_t)place;
}

static void sys_place_days(arena_t *arena, sys_work_t *work) {
    float64_t x = 0.0f64, y = 0.0f64;
    sys_place_xy(work->place, &x, &y);
    for (uint32_t d = 0u; d < SYS_DAYS; d++) {
        arena_reset(arena);
        climate_systems_day_t day;
        if (!climate_systems_day_build(work->borrowed_climate, arena, x, y, 8000.0f32, 0.0f32, (int64_t)d,
                                       CLIMATE_SYSTEMS_BIG, &day)) {
            work->borrowed_digests[d] = 0u;
            continue;
        }
        work->overflowed |= day.overflowed;
        work->borrowed_digests[d] = sys_list_digest(0xCBF29CE484222325ull, &day);
    }
}

static void sys_place_job(job_worker_t *worker, void *borrowed_argument) {
    sys_place_days(&worker->arena_job, borrowed_argument);
}

TEST(one_and_sixteen_workers_give_the_same_lists) {
    terrain_t terrain;
    REQUIRE(terrain_init(&terrain, SYS_SEED));
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, MC_THREADS_WORKERS));
    uint64_t *const one = test_alloc(t, (uint64_t)SYS_PLACES * SYS_DAYS * sizeof(uint64_t));
    uint64_t *const many = test_alloc(t, (uint64_t)SYS_PLACES * SYS_DAYS * sizeof(uint64_t));
    sys_work_t *const works = test_alloc(t, SYS_PLACES * sizeof(sys_work_t));
    arena_t *const arena = sys_arena(t, SYS_ARENA_BYTES);
    /* One worker, in order, in a fresh climate. */
    REQUIRE(climate_open(rig.climate, &terrain));
    const int64_t started = time_now_ns();
    for (uint32_t p = 0u; p < SYS_PLACES; p++) {
        works[p] = (sys_work_t){.borrowed_climate = rig.climate, .borrowed_digests = one + p * SYS_DAYS, .place = p};
        sys_place_days(arena, &works[p]);
        CHECK_EQ_U64(works[p].overflowed, 0u);
    }
    const int64_t one_ns = time_now_ns() - started;
    /* Sixteen, as jobs, in another fresh climate. */
    climate_close(rig.climate, rig.job);
    REQUIRE(climate_open(rig.climate, &terrain));
    job_group_t *const group = sys_aligned(t, sizeof(job_group_t));
    job_group_init(group);
    const int64_t many_started = time_now_ns();
    for (uint32_t p = 0u; p < SYS_PLACES; p++) {
        works[p] = (sys_work_t){.borrowed_climate = rig.climate, .borrowed_digests = many + p * SYS_DAYS, .place = p};
        job_submit(rig.job, group, sys_place_job, &works[p]);
    }
    job_group_wait(rig.job, group);
    job_group_deinit(group);
    const int64_t many_ns = time_now_ns() - many_started;
    CHECK_MEM_EQ(one, many, (uint64_t)SYS_PLACES * SYS_DAYS * sizeof(uint64_t));
    uint64_t digest = 0xCBF29CE484222325ull;
    digest = sys_fnv(digest, one, (uint64_t)SYS_PLACES * SYS_DAYS * sizeof(uint64_t));
    printf("systems digest 0x%016" PRIX64 " (pinned 0x%016" PRIX64 "); 6400 lists: 1 worker %.1f ms, %u workers %.1f "
           "ms\n",
           digest, SYS_DIGEST_PIN, (libc_double_t)time_ns_to_ms(one_ns), (uint32_t)MC_THREADS_WORKERS,
           (libc_double_t)time_ns_to_ms(many_ns));
    CHECK_EQ_U64(digest, SYS_DIGEST_PIN);
    sys_rig_stop(&rig);
    terrain_deinit(&terrain);
}

TEST(negative_coordinates_find_their_squares) {
    CHECK_EQ_I64((int64_t)floor(-1.0f64 / CLIMATE_SQUARE_HURRICANE), -1);
    CHECK_EQ_I64((int64_t)floor(-16384.0f64 / CLIMATE_SQUARE_HURRICANE), -1);
    CHECK_EQ_I64((int64_t)floor(-16385.0f64 / CLIMATE_SQUARE_HURRICANE), -2);
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, 1u));
    const ground_t island = {.kind = GROUND_ISLAND, .radius = 9000, .centre_x = -30000, .centre_y = -24000};
    REQUIRE(climate_open_ground(rig.climate, SYS_SEED, ground_fn, &island));
    arena_t *const arena = sys_arena(t, SYS_ARENA_BYTES);
    climate_systems_mask_t *const mask = sys_mask(t, rig.climate);
    climate_system_t *const square = test_alloc(t, CLIMATE_SPAWNS_MAX * sizeof(climate_system_t));
    uint64_t compared = 0u;
    for (int64_t d = 300; d < 320; d++) {
        climate_systems_day_t day;
        arena_reset(arena);
        REQUIRE(climate_systems_day_build(rig.climate, arena, -20000.0f64, -20000.0f64, 0.0f32, 0.0f32, d,
                                          CLIMATE_SYSTEMS_BIG, &day));
        /* By hand: every square of every kind whose area is within reach, every day of the longest life, and the
         * closest approach on day d sampled every 1/2000 of a day. */
        uint32_t by_hand = 0u, matched = 0u;
        for (uint8_t kind = 0u; kind < CLIMATE_SYSTEM_THUNDER; kind++) {
            const float64_t q = climate_systems_square(kind), reach = climate_systems_reach(kind);
            for (int32_t sy = (int32_t)floor((-20000.0f64 - reach) / q);
                 sy <= (int32_t)floor((-20000.0f64 + reach) / q); sy++) {
                for (int32_t sx = (int32_t)floor((-20000.0f64 - reach) / q);
                     sx <= (int32_t)floor((-20000.0f64 + reach) / q); sx++) {
                    for (int64_t back = 0; back <= 14; back++) {
                        bool full = false;
                        const uint32_t n = climate_systems_square_spawns(rig.climate, mask, kind, sx, sy, d - back,
                                                                         square, CLIMATE_SPAWNS_MAX, &full);
                        for (uint32_t i = 0u; i < n; i++) {
                            float64_t closest = 1.0e30f64;
                            for (uint32_t s = 0u; s < 2000u; s++) {
                                climate_system_state_t state;
                                climate_system_state_at(&square[i], (float64_t)d + (float64_t)s / 2000.0f64, &state);
                                if (state.alive != 0u) {
                                    const float64_t dx = state.x + 20000.0f64, dy = state.y + 20000.0f64;
                                    const float64_t r = sqrt(dx * dx + dy * dy);
                                    closest = r < closest ? r : closest;
                                }
                            }
                            const float64_t limit = 3.0f64 * (float64_t)square[i].radius;
                            bool listed = false;
                            for (uint32_t j = 0u; j < day.system_count; j++) {
                                listed = listed || (day.borrowed_systems[j].id == square[i].id &&
                                                    day.borrowed_systems[j].kind == kind);
                            }
                            if (closest <= limit - 2.0f64) {
                                by_hand++;
                                matched += listed ? 1u : 0u;
                            } else if (closest > limit + 2.0f64) {
                                CHECK(!listed);
                            }
                        }
                    }
                }
            }
        }
        CHECK_EQ_U64(matched, by_hand);
        compared += by_hand;
    }
    printf("systems near (-20000, -20000) over 20 days, by hand: %" PRIu64 "\n", compared);
    CHECK(compared > 0u);
    sys_rig_stop(&rig);
}

TEST(no_track_reaches_a_pole_and_no_high_reaches_the_equator) {
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, 1u));
    climate_system_t *const out = test_alloc(t, CLIMATE_SPAWNS_MAX * sizeof(climate_system_t));
    const float64_t pole_limit = 85.0f64 + 0.25f64 * 7204.0f64 / 425.5f64;
    const float64_t equator_limit = 5.0f64 - 0.25f64 * 2512.0f64 * 0.1f64 / 425.5f64;
    static const uint32_t GROUNDS[2] = {GROUND_ALL_SEA, GROUND_ALL_LAND};
    uint64_t counts[3] = {0u, 0u, 0u}, bad_pole = 0u, bad_high = 0u;
    float64_t highest = 0.0f64;
    for (uint32_t g = 0u; g < 2u; g++) {
        const ground_t ground = {.kind = GROUNDS[g]};
        climate_close(rig.climate, rig.job);
        REQUIRE(climate_open_ground(rig.climate, SYS_SEED + g, ground_fn, &ground));
        climate_systems_mask_t *const mask = sys_mask(t, rig.climate);
        for (uint8_t kind = 0u; kind < CLIMATE_SYSTEM_THUNDER; kind++) {
            const float64_t q = climate_systems_square(kind);
            const int32_t rows = (int32_t)ceil(CLIMATE_LATITUDE_PERIOD / q);
            for (int64_t d = 0; d < 20 * 365; d += 2) {
                for (int32_t sy = 0; sy < rows; sy++) {
                    for (int32_t sx = 0; sx < 2; sx++) {
                        bool full = false;
                        const uint32_t n = climate_systems_square_spawns(rig.climate, mask, kind, sx, sy, d, out,
                                                                         CLIMATE_SPAWNS_MAX, &full);
                        counts[kind] += n;
                        for (uint32_t i = 0u; i < n; i++) {
                            for (uint32_t k = 0u; k < out[i].sample_count; k++) {
                                const float64_t phi = fabs(climate_latitude_deg(out[i].samples[k].y));
                                highest = phi > highest ? phi : highest;
                                bad_pole += phi <= pole_limit ? 0u : 1u;
                                bad_high += (kind != CLIMATE_SYSTEM_HIGH || phi >= equator_limit) ? 0u : 1u;
                            }
                        }
                    }
                }
            }
        }
    }
    printf("20 years on sea and land: %" PRIu64 " hurricanes, %" PRIu64 " lows, %" PRIu64
           " highs; highest |phi| %.3f\n",
           counts[0], counts[1], counts[2], (libc_double_t)highest);
    CHECK(counts[1] > 0u && counts[2] > 0u);
    CHECK_EQ_U64(bad_pole, 0u);
    CHECK_EQ_U64(bad_high, 0u);
    sys_rig_stop(&rig);
}

TEST(lists_never_overflow) {
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, MC_THREADS_WORKERS));
    arena_t *const arena = sys_arena(t, SYS_ARENA_BYTES);
    uint32_t most_big = 0u, most_cells = 0u;
    uint64_t overflowed = 0u;
    static const uint32_t GROUNDS[4] = {GROUND_ALL_SEA, GROUND_ALL_LAND, GROUND_ALL_SEA, GROUND_ALL_LAND};
    for (uint32_t s = 0u; s < 4u; s++) {
        const ground_t ground = {.kind = GROUNDS[s]};
        climate_close(rig.climate, rig.job);
        REQUIRE(climate_open_ground(rig.climate, SYS_SEED + 17u * s, ground_fn, &ground));
        terrain_rng_t rng = terrain_rng_start(1000u + s);
        for (uint32_t i = 0u; i < 250u; i++) {
            const float64_t x = (terrain_rng_next_f64(&rng) - 0.5f64) * 300000.0f64;
            const float64_t y = (terrain_rng_next_f64(&rng) - 0.5f64) * 160000.0f64;
            const int64_t d = (int64_t)(terrain_rng_next_f64(&rng) * 3650.0f64);
            climate_systems_day_t day;
            arena_reset(arena);
            REQUIRE(
                climate_systems_day_build(rig.climate, arena, x, y, 40000.0f32, 0.0f32, d, CLIMATE_SYSTEMS_BIG, &day));
            overflowed += day.overflowed;
            most_big = day.system_count > most_big ? day.system_count : most_big;
        }
    }
    /* Cells at 8 km round a hot, wet equatorial sea, every tile they need built. */
    const ground_t sea = {.kind = GROUND_ALL_SEA};
    climate_close(rig.climate, rig.job);
    REQUIRE(climate_open_ground(rig.climate, SYS_SEED, ground_fn, &sea));
    climate_prepare_area(rig.climate, rig.job, -13000, -13000, 13000, 13000, nullptr, nullptr);
    uint32_t incomplete = 0u;
    for (int64_t d = 100; d < 120; d++) {
        climate_systems_day_t day;
        arena_reset(arena);
        REQUIRE(climate_systems_day_build(rig.climate, arena, 0.0f64, 0.0f64, 40000.0f32, 8000.0f32, d,
                                          CLIMATE_SYSTEMS_ALL, &day));
        overflowed += day.overflowed;
        incomplete += day.thunder_complete == 0u ? 1u : 0u;
        most_big = day.system_count > most_big ? day.system_count : most_big;
        most_cells = day.thunder_count > most_cells ? day.thunder_count : most_cells;
        CHECK(sys_list_sorted(&day));
    }
    printf("largest lists: %u big systems at 40 km (capacity %u), %u cells at 8 km (capacity %u)\n", most_big,
           CLIMATE_SYSTEMS_LIST_MAX, most_cells, CLIMATE_THUNDER_LIST_MAX);
    CHECK_EQ_U64(overflowed, 0u);
    CHECK_EQ_U64(incomplete, 0u);
    CHECK(most_cells > 0u);
    sys_rig_stop(&rig);
}

TEST(the_mask_point_read_matches_the_window) {
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, 1u));
    const ground_t plane = {.kind = GROUND_PLANE};
    REQUIRE(climate_open_ground(rig.climate, SYS_SEED, ground_fn, &plane));
    arena_t *const arena = sys_arena(t, SYS_ARENA_BYTES);
    climate_systems_mask_t *const mask = sys_mask(t, rig.climate);
    float32_t page[CLIMATE_TILE_CELL_COUNT];
    terrain_rng_t rng = terrain_rng_start(31u);
    uint64_t wrong = 0u;
    for (uint32_t i = 0u; i < 1000u; i++) {
        /* Half inside the keep square (cached once claimed), half far outside it (private). */
        const float64_t spread = i % 2u == 0u ? 60000.0f64 : 2000000.0f64;
        const float64_t x = (terrain_rng_next_f64(&rng) - 0.5f64) * spread;
        const float64_t y = (terrain_rng_next_f64(&rng) - 0.5f64) * spread;
        const int64_t cell_x = (int64_t)floor(x / 64.0f64), cell_y = (int64_t)floor(y / 64.0f64);
        const int32_t tile_x = (int32_t)(cell_x >> 4), tile_y = (int32_t)(cell_y >> 4);
        climate_mask_page_build(ground_fn, &plane, tile_x, tile_y, page);
        const float32_t want = page[(cell_y & 15) * 16 + (cell_x & 15)];
        const float32_t claimed = climate_mask_height_at(rig.climate, arena, x, y);
        const float32_t again = climate_mask_height_at(rig.climate, arena, x, y);
        const float32_t cached = climate_systems_mask_height(mask, x, y);
        wrong += (claimed == want && again == want && cached == want) ? 0u : 1u;
        CHECK_NEAR(want, (float32_t)((cell_x * 64 + 32) - 3 * (cell_y * 64 + 32)), 0.0f32);
    }
    CHECK_EQ_U64(wrong, 0u);
    sys_rig_stop(&rig);
}

/* ---- Thunderstorm cells ----------------------------------------------------------------------------------------------- */

typedef struct sys_cells_t {
    uint64_t afternoon;   /* 0 */
    uint64_t front;       /* 8 */
    uint64_t square_days; /* 16 */
    uint64_t late;        /* 24: afternoon cells outside 13:00 to 19:00 */
    uint64_t bins[6];     /* 32: front cells by 4-hour bin of the day */
    uint64_t fast;        /* 80: cells faster than 18000 blocks a day */
    uint64_t wrong_wind;  /* 88: cells whose velocity is not the wind at noon */
} sys_cells_t;
static_assert(sizeof(sys_cells_t) == 96);

/* Cells of squares (0..3, sy0..sy0+3) for days 0 to days - 1 at lambda_base, thinned by day's systems. */
static void sys_cells_run(test_context_t *t, const climate_t *climate, climate_systems_day_t *day, int32_t sy0,
                          uint32_t days, float32_t lambda, sys_cells_t *out) {
    climate_thunder_t *const cells = test_alloc(t, 2u * CLIMATE_THUNDER_SPAWNS_MAX * sizeof(climate_thunder_t));
    memset(out, 0, sizeof(*out));
    for (uint32_t d = 0u; d < days; d++) {
        for (uint32_t i = 0u; i < day->system_count; i++) {
            day->borrowed_systems[i].spawn_day = (float64_t)d - 1.0f64; /* alive all day */
        }
        for (int32_t sy = sy0; sy < sy0 + 4; sy++) {
            for (int32_t sx = 0; sx < 4; sx++) {
                bool ready = false, full = false;
                const uint32_t n =
                    climate_systems_thunder_square(climate, day, sx, sy, (int64_t)d, lambda, 1.0f32, cells,
                                                   2u * CLIMATE_THUNDER_SPAWNS_MAX, &ready, &full);
                CHECK(ready && !full);
                out->square_days++;
                for (uint32_t c = 0u; c < n; c++) {
                    const float64_t f = cells[c].spawn_day - (float64_t)d;
                    if (cells[c].on_front != 0u) {
                        out->front++;
                        out->bins[(uint32_t)(f * 6.0f64)]++;
                    } else {
                        out->afternoon++;
                        out->late += (f >= 7.0f64 / 24.0f64 && f <= 13.0f64 / 24.0f64) ? 0u : 1u;
                    }
                    const float64_t speed = sqrt((float64_t)cells[c].velocity[0] * cells[c].velocity[0] +
                                                 (float64_t)cells[c].velocity[1] * cells[c].velocity[1]);
                    out->fast += speed <= 18000.0f64 + 0.01f64 ? 0u : 1u;
                    if (c == 0u) {
                        climate_air_t air;
                        const float32_t noon = (float32_t)(climate_systems_day_of_year((float64_t)d) + 0.25f64);
                        if (climate_air_at(climate, cells[c].start_x, cells[c].start_y, noon, &air)) {
                            const float32_t w = sqrtf(air.wind[0] * air.wind[0] + air.wind[1] * air.wind[1]);
                            const float32_t scale = w > 15.0f32 ? 15.0f32 / w : 1.0f32;
                            out->wrong_wind +=
                                fabsf(cells[c].velocity[0] - air.wind[0] * scale * 1200.0f32) < 0.01f32 &&
                                        fabsf(cells[c].velocity[1] - air.wind[1] * scale * 1200.0f32) < 0.01f32
                                    ? 0u
                                    : 1u;
                        }
                    }
                }
            }
        }
    }
}

TEST(thunderstorm_rate_is_the_worked_example) {
    CHECK_NEAR(climate_systems_thunder_base(30.0f32, 1.0f32, true), 9.0f32 * CLIMATE_THUNDER_RATE_GAIN, 1.0e-5f32);
    CHECK_NEAR(climate_systems_thunder_base(15.0f32, 1.0f32, true), 0.0f32, 0.0f32);
    CHECK_NEAR(climate_systems_thunder_base(23.0f32, 0.5f32, false), 0.75f32 * CLIMATE_THUNDER_RATE_GAIN, 1.0e-5f32);
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, MC_THREADS_WORKERS));
    const ground_t sea = {.kind = GROUND_ALL_SEA};
    REQUIRE(climate_open_ground(rig.climate, SYS_SEED, ground_fn, &sea));
    climate_prepare_area(rig.climate, rig.job, -1024, -1024, 5 * 1024, 5 * 1024, nullptr, nullptr);
    sys_cells_t cells;
    /* Nothing over the squares: afternoon cells only, at 9 a day. 6250 days x 16 squares = 10^5 square-days. */
    climate_systems_day_t none = sys_day_of(nullptr, 0u);
    sys_cells_run(t, rig.climate, &none, 0, 6250u, 9.0f32, &cells);
    const float64_t afternoon = (float64_t)cells.afternoon / (float64_t)cells.square_days;
    printf("afternoon cells: %.4f a square-day, %" PRIu64 " front cells off any front\n", (libc_double_t)afternoon,
           cells.front);
    CHECK_NEAR(afternoon, 9.0f64, 0.05f64);
    CHECK_EQ_U64(cells.front, 0u);
    CHECK_EQ_U64(cells.late, 0u);
    CHECK_EQ_U64(cells.fast, 0u);
    CHECK_EQ_U64(cells.wrong_wind, 0u);
    /* Under a still high covering every square: 9 x 0.2. */
    climate_system_t high[1];
    sys_hand(&high[0], CLIMATE_SYSTEM_HIGH, 2048.0f64, 2048.0f64, 0.0f64, 13.0f32, 14.0f32, 12000.0f32, 6000.0f32);
    climate_systems_day_t under = sys_day_of(high, 1u);
    sys_cells_run(t, rig.climate, &under, 0, 6250u, 9.0f32, &cells);
    const float64_t high_rate = (float64_t)cells.afternoon / (float64_t)cells.square_days;
    printf("under a high: %.4f a square-day\n", (libc_double_t)high_rate);
    CHECK_NEAR(high_rate, 1.8f64, 0.02f64);
    /* On a fixed front: a still low whose front runs through the squares; front cells at 36 a day on the front's share of
     * each square, found on a 256 by 256 grid of start points. */
    climate_system_t low[1];
    sys_hand(&low[0], CLIMATE_SYSTEM_LOW, 4096.0f64, 4096.0f64 + 3500.0f64, 0.0f64, 6.0f32, -20.0f32, 5000.0f32,
             2500.0f32);
    climate_systems_day_t front = sys_day_of(low, 1u);
    float64_t share = 0.0f64;
    for (int32_t sy = 0; sy < 4; sy++) {
        for (int32_t sx = 0; sx < 4; sx++) {
            uint32_t on = 0u;
            for (uint32_t j = 0u; j < 256u; j++) {
                for (uint32_t i = 0u; i < 256u; i++) {
                    const float64_t x = ((float64_t)sx + ((float64_t)i + 0.5f64) / 256.0f64) * 1024.0f64;
                    const float64_t y = ((float64_t)sy + ((float64_t)j + 0.5f64) / 256.0f64) * 1024.0f64;
                    on += climate_systems_on_front(&front, x, y, 0.5f64) ? 1u : 0u;
                }
            }
            share += (float64_t)on / 65536.0f64;
        }
    }
    sys_cells_run(t, rig.climate, &front, 0, 6250u, 9.0f32, &cells);
    const float64_t front_rate = (float64_t)cells.front / (6250.0f64 * share);
    printf("front: %.3f of the 16 squares on it; front cells %.3f a day on it\n", (libc_double_t)share,
           (libc_double_t)front_rate);
    CHECK(share > 1.0f64);
    CHECK_NEAR(front_rate, 36.0f64, 0.2f64);
    CHECK_NEAR((float64_t)cells.afternoon / (float64_t)cells.square_days, 9.0f64, 0.05f64);
    /* Front cells spread over the whole day: each 4-hour bin within 20% of a sixth. */
    for (uint32_t b = 0u; b < 6u; b++) {
        CHECK(fabs((float64_t)cells.bins[b] / (float64_t)cells.front - 1.0f64 / 6.0f64) <= 0.2f64 / 6.0f64);
    }
    CHECK_EQ_U64(cells.late, 0u);
    sys_rig_stop(&rig);
}

TEST(cells_under_a_high_s_centre_are_a_fifth_of_the_rate_over_1000_days) {
    sys_rig_t rig;
    REQUIRE(sys_rig_start(t, &rig, MC_THREADS_WORKERS));
    const ground_t sea = {.kind = GROUND_ALL_SEA};
    REQUIRE(climate_open_ground(rig.climate, SYS_SEED, ground_fn, &sea));
    climate_prepare_area(rig.climate, rig.job, -4096, -4096, 4096, 4096, nullptr, nullptr);
    climate_thunder_t *const cells = test_alloc(t, 2u * CLIMATE_THUNDER_SPAWNS_MAX * sizeof(climate_thunder_t));
    climate_system_t high[1];
    sys_hand(&high[0], CLIMATE_SYSTEM_HIGH, 0.0f64, 0.0f64, 0.0f64, 13.0f32, 14.0f32, 8000.0f32, 4000.0f32);
    const float64_t near = 0.3f64 * 8000.0f64;
    const float64_t area_km2 = CLIMATE_PI * near * near / (1024.0f64 * 1024.0f64);
    uint64_t counts[2] = {0u, 0u};
    for (uint32_t pass = 0u; pass < 2u; pass++) {
        climate_systems_day_t day = sys_day_of(high, pass == 0u ? 1u : 0u);
        for (uint32_t d = 0u; d < 1000u; d++) {
            high[0].spawn_day = (float64_t)d - 1.0f64;
            for (int32_t sy = -3; sy < 3; sy++) {
                for (int32_t sx = -3; sx < 3; sx++) {
                    bool ready = false, full = false;
                    const uint32_t n =
                        climate_systems_thunder_square(rig.climate, &day, sx, sy, (int64_t)d, 9.0f32, 1.0f32, cells,
                                                       2u * CLIMATE_THUNDER_SPAWNS_MAX, &ready, &full);
                    CHECK(ready);
                    for (uint32_t c = 0u; c < n; c++) {
                        const float64_t r =
                            sqrt(cells[c].start_x * cells[c].start_x + cells[c].start_y * cells[c].start_y);
                        counts[pass] += r < near ? 1u : 0u;
                    }
                }
            }
        }
    }
    const float64_t under = (float64_t)counts[0] / area_km2 / 1000.0f64;
    const float64_t open = (float64_t)counts[1] / area_km2 / 1000.0f64;
    printf("cells within 0.3 R of a high's centre: %.3f per km^2 a day; without the high %.3f\n", (libc_double_t)under,
           (libc_double_t)open);
    CHECK_NEAR(under, 1.8f64, 0.1f64);
    CHECK_NEAR(open, 9.0f64, 0.2f64);
    sys_rig_stop(&rig);
}

TEST(a_cell_s_speed_is_capped_at_15_blocks_a_second) {
    /* Issues found 19: real winds stay under 15, so the cap is held here on the pure velocity rule. */
    float32_t v[2];
    climate_systems_thunder_velocity(6.0f32, -8.0f32, v); /* 10 blocks a second: under the cap, times 1200 */
    CHECK_NEAR(v[0], 7200.0f32, 1.0e-3f32);
    CHECK_NEAR(v[1], -9600.0f32, 1.0e-3f32);
    climate_systems_thunder_velocity(-12.0f32, 16.0f32, v); /* 20: capped to 15, direction kept */
    CHECK_NEAR(v[0], -10800.0f32, 1.0e-2f32);
    CHECK_NEAR(v[1], 14400.0f32, 1.0e-2f32);
    CHECK_NEAR(sqrtf(v[0] * v[0] + v[1] * v[1]), 18000.0f32, 1.0e-2f32);
    climate_systems_thunder_velocity(40.0f32, 0.0f32, v); /* a hurricane-strength wind */
    CHECK_NEAR(v[0], 18000.0f32, 1.0e-3f32);
    CHECK_NEAR(v[1], 0.0f32, 0.0f32);
    climate_systems_thunder_velocity(15.0f32, 0.0f32, v); /* exactly the cap */
    CHECK_NEAR(v[0], 18000.0f32, 1.0e-3f32);
    climate_systems_thunder_velocity(0.0f32, 0.0f32, v); /* still air: no division by zero */
    CHECK_NEAR(v[0], 0.0f32, 0.0f32);
    CHECK_NEAR(v[1], 0.0f32, 0.0f32);
}

TEST_MAIN()
