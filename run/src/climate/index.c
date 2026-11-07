/* See README.md. The Owner of plan 004's climate: latitude from y alone (master plan 5.1, phase 02), and the climate cells
 * of phase 03: the region, the warp, the tile jobs, the scheduler and the reads. */
#include "climate/index.h"

#include "assert/index.h"
#include "climate/systems/index.h"
#include "climate/weather/index.h"
#include "log/index.h"
#include "terrain/rng/index.h"
#include "time/index.h"

#include <math.h>
#include <string.h>

float64_t climate_latitude_deg(float64_t y) {
    /* Into one period first, [-2Y, 2Y): sin of an angle in the millions keeps fewer digits than sin of one under 2 pi. */
    const float64_t period = CLIMATE_LATITUDE_PERIOD;
    const float64_t near = y - period * floor((y + 0.5f64 * period) / period);
    /* asin undoes sin while the angle is within a quarter turn, and folds it back past one: a triangle wave. */
    const float64_t degrees = asin(sin(CLIMATE_PI * near / (2.0f64 * CLIMATE_POLE_Y))) * (180.0f64 / CLIMATE_PI);
    return degrees > 90.0f64 ? 90.0f64 : (degrees < -90.0f64 ? -90.0f64 : degrees);
}

/* ---- The region ---------------------------------------------------------------------------------------------------- */

/* The warp's two fields: names apart from the terrain's "climate/..." fields, as a field comes from its name and the seed. */
static const terrain_noise_shape_t CLIMATE_WARP_SHAPE = {
    .frequency_x = (float64_t)CLIMATE_WARP_FREQUENCY,
    .frequency_y = (float64_t)CLIMATE_WARP_FREQUENCY,
    .lacunarity = 2.0f32,
    .persistence = 0.5f32,
    .octaves = 2u,
};
/* The same spread as the density's detail field: a raw value this far from 0 is -1 or 1. */
static const float64_t CLIMATE_WARP_LIMIT = 0.35f64;
/* How often prepare_area looks at its jobs, to tell its progress. */
static const int64_t CLIMATE_PREPARE_POLL_NS = TIME_NS_PER_MS;

bool climate_init(climate_t *climate) {
    memset(climate, 0, sizeof(*climate));
    if (!heap_reserve(CLIMATE_REGION_BYTES, &climate->owned_region)) {
        return false; /* it has said why */
    }
    uint8_t *const base = climate->owned_region.owned_base;
    climate_state_t *const state = (climate_state_t *)(void *)base;
    memset(state, 0, sizeof(*state));
    arena_init(&state->arena_tables, "climate_tables", base + CLIMATE_TABLES_OFFSET,
               CLIMATE_STATE_BYTES - CLIMATE_TABLES_OFFSET);
    job_group_init(&state->group);
    arena_init(&state->arena_normals, "climate_normals", base + CLIMATE_NORMALS_OFFSET,
               CLIMATE_STATE_BYTES - CLIMATE_NORMALS_OFFSET);
    state->borrowed_normals = climate_normals_build(&state->arena_normals);
    state->borrowed_tile_slots = (climate_slot_t *)(void *)(base + CLIMATE_TILE_SLOTS_OFFSET);
    state->borrowed_mask_slots = (climate_slot_t *)(void *)(base + CLIMATE_MASK_SLOTS_OFFSET);
    state->borrowed_cells = (climate_cell_t *)(void *)(base + CLIMATE_CELLS_OFFSET);
    state->borrowed_mask_heights = (float32_t *)(void *)(base + CLIMATE_MASK_OFFSET);
    climate->borrowed_state = state;
    /* Phase 07: the weather's own region, its store's tiles touched only as tiles are kept. */
    if (!heap_reserve(CLIMATE_WEEK_REGION_BYTES, &climate->owned_week_region)) {
        climate_deinit(climate);
        return false; /* it has said why */
    }
    climate->borrowed_weather = (climate_weather_t *)(void *)climate->owned_week_region.owned_base;
    memset(climate->borrowed_weather, 0, sizeof(climate_weather_t));
    return true;
}

void climate_deinit(climate_t *climate) {
    climate_state_t *const state = climate->borrowed_state;
    if (state != nullptr) {
        ASSERT_ALWAYS(state->open == 0u);
        job_group_deinit(&state->group);
        state->borrowed_normals = nullptr;
        arena_deinit(&state->arena_normals);
        arena_deinit(&state->arena_tables);
    }
    climate->borrowed_weather = nullptr;
    heap_release(&climate->owned_week_region);
    heap_release(&climate->owned_region);
    memset(climate, 0, sizeof(*climate));
}

static void climate_terrain_ground(const void *borrowed_context, int32_t origin_x, int32_t origin_y, int32_t step,
                                   uint32_t count_x, uint32_t count_y, float32_t *heights) {
    terrain_height_estimate_area(borrowed_context, origin_x, origin_y, step, count_x, count_y, heights);
}

bool climate_open(climate_t *climate, const terrain_t *terrain) {
    return climate_open_ground(climate, terrain->seed, climate_terrain_ground, terrain);
}

bool climate_open_ground(climate_t *climate, uint64_t seed, climate_ground_fn_t ground, const void *borrowed_context) {
    climate_state_t *const state = climate->borrowed_state;
    ASSERT_ALWAYS(state != nullptr && state->open == 0u);
    arena_reset(&state->arena_tables);
    terrain_noise_bank_init(&state->warp_bank, &state->arena_tables, seed, CLIMATE_WARP_FIELDS);
    if (!terrain_noise_bank_add(&state->warp_bank, "climate_cells/warp_x", &CLIMATE_WARP_SHAPE,
                                &state->borrowed_warp_x) ||
        !terrain_noise_bank_add(&state->warp_bank, "climate_cells/warp_y", &CLIMATE_WARP_SHAPE,
                                &state->borrowed_warp_y)) {
        terrain_noise_bank_deinit(&state->warp_bank);
        return false; /* it has said why */
    }
    climate_cache_empty(state->borrowed_tile_slots, CLIMATE_TILE_SLOTS);
    climate_cache_empty(state->borrowed_mask_slots, CLIMATE_MASK_SLOTS);
    state->seed = seed;
    state->ground = ground;
    state->borrowed_ground_context = borrowed_context;
    state->warp_off = 0u;
    memset(&state->hooks, 0, sizeof(state->hooks));
    memset(&state->systems_hooks, 0, sizeof(state->systems_hooks));
    for (uint8_t kind = 0u; kind < CLIMATE_SYSTEM_KINDS; kind++) {
        state->systems_seed[kind] = terrain_rng_derive(seed, terrain_rng_salt(CLIMATE_SYSTEMS_SALT_NAMES[kind]));
    }
    atomic_store_explicit(&state->keep_tile_x, 0, memory_order_relaxed);
    atomic_store_explicit(&state->keep_tile_y, 0, memory_order_relaxed);
    atomic_store_explicit(&state->jobs_in_flight, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->jobs_done, 0u, memory_order_relaxed);
    memset(&state->prepare, 0, sizeof(state->prepare));
    if (!climate_weather_open(climate->borrowed_weather, climate->owned_week_region.owned_base,
                              climate->owned_week_region.bytes, seed)) {
        terrain_noise_bank_deinit(&state->warp_bank);
        return false; /* it has said why */
    }
    state->open = 1u;
    return true;
}

void climate_close(climate_t *climate, const job_system_t *system) {
    climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u) {
        return;
    }
    /* Nothing may still be writing into the tables of a world about to be forgotten. */
    job_group_wait(system, &state->group);
    memset(&state->prepare, 0, sizeof(state->prepare)); /* a begun precompute is over: its jobs are */
    terrain_noise_bank_deinit(&state->warp_bank);
    state->ground = nullptr;
    state->borrowed_ground_context = nullptr;
    state->borrowed_warp_x = nullptr;
    state->borrowed_warp_y = nullptr;
    climate_weather_close(climate->borrowed_weather);
    state->open = 0u;
}

bool climate_is_open(const climate_t *climate) {
    return climate->borrowed_state != nullptr && climate->borrowed_state->open != 0u;
}

/* ---- Jobs ---------------------------------------------------------------------------------------------------------- */

static climate_mask_t climate_mask_of(const climate_state_t *state) {
    return (climate_mask_t){
        .borrowed_slots = state->borrowed_mask_slots,
        .borrowed_heights = state->borrowed_mask_heights,
        .ground = state->ground,
        .borrowed_ground_context = state->borrowed_ground_context,
    };
}

static climate_cell_t *climate_tile_cells(const climate_state_t *state, const climate_slot_t *slot) {
    const uint64_t index = (uint64_t)(slot - state->borrowed_tile_slots);
    return state->borrowed_cells + index * (uint64_t)CLIMATE_TILE_CELL_COUNT;
}

/* One tile, on a worker, for a slot its claimer has set up: the window, then the cells, then publish. It never waits. */
static void climate_tile_job(job_worker_t *worker, void *borrowed_argument) {
    climate_slot_t *const slot = borrowed_argument;
    climate_state_t *const state = slot->borrowed_job_owner;
    const int32_t tile_x = slot->job_tile_x;
    const int32_t tile_y = slot->job_tile_y;
    arena_t *const arena = &worker->arena_job;
    float32_t *const window =
        arena_alloc_array(arena, (uint64_t)CLIMATE_WINDOW_CELL_COUNT, sizeof(float32_t), alignof(float32_t));
    const climate_mask_t mask = climate_mask_of(state);
    climate_mask_window(&mask, tile_x, tile_y, atomic_load_explicit(&state->keep_tile_x, memory_order_relaxed),
                        atomic_load_explicit(&state->keep_tile_y, memory_order_relaxed), window, nullptr);
    climate_window_t cells_window;
    climate_cell_t *const cells = climate_tile_cells(state, slot);
    climate_cell_build_tile(arena, state->ground, state->borrowed_ground_context, tile_x, tile_y, window, cells,
                            &cells_window);
    /* Phase 04: currents, swing, lag, wind and pressure, from the same window, before anyone can read the tile. */
    climate_air_build_tile(state->borrowed_normals, arena, &cells_window, cells, nullptr);
    /* Phase 05: temperature, then rain, then the seasons and classes that read both. */
    climate_heat_build_tile(state->borrowed_normals, cells);
    climate_rain_build_tile(state->borrowed_normals, &state->hooks, &cells_window, cells);
    climate_season_build_tile(state->borrowed_normals, &state->hooks, &cells_window, cells);
    climate_cache_publish(slot);
    atomic_fetch_sub_explicit(&state->jobs_in_flight, 1u, memory_order_release);
    atomic_fetch_add_explicit(&state->jobs_done, 1u, memory_order_release);
}

/* One mask page, on a worker, into the table, for a slot its claimer has set up. */
static void climate_page_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    climate_slot_t *const slot = borrowed_argument;
    climate_state_t *const state = slot->borrowed_job_owner;
    const uint64_t index = (uint64_t)(slot - state->borrowed_mask_slots);
    climate_mask_page_build(state->ground, state->borrowed_ground_context, slot->job_tile_x, slot->job_tile_y,
                            state->borrowed_mask_heights + index * (uint64_t)CLIMATE_TILE_CELL_COUNT);
    climate_cache_publish(slot);
    atomic_fetch_add_explicit(&state->jobs_done, 1u, memory_order_release);
}

static void climate_slot_job_set(climate_slot_t *slot, climate_state_t *state, int32_t tile_x, int32_t tile_y) {
    slot->borrowed_job_owner = state;
    slot->job_tile_x = tile_x;
    slot->job_tile_y = tile_y;
}

/* The tiles a read anywhere in a block range touches, both ends included: a read reaches CLIMATE_READ_REACH_BLOCKS. */
static int32_t climate_tile_low(int32_t block) {
    return (int32_t)(((int64_t)block - CLIMATE_READ_REACH_BLOCKS) >> CLIMATE_TILE_SHIFT);
}
static int32_t climate_tile_high(int32_t block) {
    return (int32_t)(((int64_t)block + CLIMATE_READ_REACH_BLOCKS) >> CLIMATE_TILE_SHIFT);
}

/* ---- The sim's scheduling ------------------------------------------------------------------------------------------ */

void climate_schedule(climate_t *climate, job_system_t *system, int32_t player_x, int32_t player_y,
                      uint32_t radius_blocks) {
    climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u) {
        return;
    }
    const int32_t keep_x = (int32_t)((int64_t)player_x >> CLIMATE_TILE_SHIFT);
    const int32_t keep_y = (int32_t)((int64_t)player_y >> CLIMATE_TILE_SHIFT);
    atomic_store_explicit(&state->keep_tile_x, keep_x, memory_order_relaxed);
    atomic_store_explicit(&state->keep_tile_y, keep_y, memory_order_relaxed);
    const int64_t reach = (int64_t)radius_blocks;
    const int64_t clamp_low = INT32_MIN + 2 * CLIMATE_TILE_BLOCKS, clamp_high = INT32_MAX - 2 * CLIMATE_TILE_BLOCKS;
    const int64_t x_lo = (int64_t)player_x - reach < clamp_low ? clamp_low : (int64_t)player_x - reach;
    const int64_t x_hi = (int64_t)player_x + reach > clamp_high ? clamp_high : (int64_t)player_x + reach;
    const int64_t y_lo = (int64_t)player_y - reach < clamp_low ? clamp_low : (int64_t)player_y - reach;
    const int64_t y_hi = (int64_t)player_y + reach > clamp_high ? clamp_high : (int64_t)player_y + reach;
    const int32_t tx0 = climate_tile_low((int32_t)x_lo), tx1 = climate_tile_high((int32_t)x_hi);
    const int32_t ty0 = climate_tile_low((int32_t)y_lo), ty1 = climate_tile_high((int32_t)y_hi);
    /* Tiles that would not fit in the keep square cannot be kept, so are not scheduled. */
    uint64_t passed[CLIMATE_JOBS_IN_FLIGHT_MAX];
    uint32_t passed_count = 0u;
    while (atomic_load_explicit(&state->jobs_in_flight, memory_order_acquire) < CLIMATE_JOBS_IN_FLIGHT_MAX &&
           passed_count < CLIMATE_JOBS_IN_FLIGHT_MAX) {
        /* The nearest tile not yet built or being built, by the squared distance from the player to its centre, ties by y
         * then x. A scan, not a sort: there are at most a few hundred, and most steps find none. */
        bool found = false;
        int64_t best = 0;
        int32_t best_x = 0, best_y = 0;
        for (int32_t ty = ty0; ty <= ty1; ty++) {
            for (int32_t tx = tx0; tx <= tx1; tx++) {
                if (!climate_cache_keeps(tx, ty, keep_x, keep_y, CLIMATE_TILE_SIDE)) {
                    continue;
                }
                const uint64_t key = climate_cache_key(tx, ty);
                const climate_slot_t *const slot =
                    &state->borrowed_tile_slots[climate_cache_slot_index(tx, ty, CLIMATE_TILE_SIDE)];
                const uint32_t slot_state = atomic_load_explicit(&slot->state, memory_order_acquire);
                if (slot_state == CLIMATE_SLOT_CLAIMED || slot_state == CLIMATE_SLOT_EVICTING ||
                    climate_cache_ready(slot, key)) {
                    continue;
                }
                bool skipped = false;
                for (uint32_t p = 0u; p < passed_count; p++) {
                    skipped = skipped || passed[p] == key;
                }
                if (skipped) {
                    continue;
                }
                const int64_t dx = (int64_t)tx * CLIMATE_TILE_BLOCKS + CLIMATE_TILE_BLOCKS / 2 - (int64_t)player_x;
                const int64_t dy = (int64_t)ty * CLIMATE_TILE_BLOCKS + CLIMATE_TILE_BLOCKS / 2 - (int64_t)player_y;
                const int64_t distance = dx * dx + dy * dy;
                /* The scan goes y then x, so the first of equals is the one with the lower y, then x. */
                if (!found || distance < best) {
                    found = true;
                    best = distance;
                    best_x = tx;
                    best_y = ty;
                }
            }
        }
        if (!found) {
            return;
        }
        climate_slot_t *const slot =
            &state->borrowed_tile_slots[climate_cache_slot_index(best_x, best_y, CLIMATE_TILE_SIDE)];
        if (climate_cache_claim(slot, climate_cache_key(best_x, best_y), true) != CLIMATE_CLAIM_OURS) {
            passed[passed_count++] = climate_cache_key(best_x, best_y); /* a reader holds it: next step */
            continue;
        }
        climate_slot_job_set(slot, state, best_x, best_y);
        atomic_fetch_add_explicit(&state->jobs_in_flight, 1u, memory_order_relaxed);
        if (!job_try_submit(system, &state->group, climate_tile_job, slot)) {
            /* The shared queue is full: undone, and tried again next step. */
            atomic_fetch_sub_explicit(&state->jobs_in_flight, 1u, memory_order_relaxed);
            climate_cache_unclaim(slot);
            return;
        }
    }
}

/* ---- Precompute ---------------------------------------------------------------------------------------------------- */

/* Builds the pages (round 1) or the tiles (round 2) of the begun rectangle not built yet, a job each, and starts counting
 * them. Pages first, so no tile job builds a page another job is building. */
static void climate_prepare_submit(climate_state_t *state, job_system_t *system, bool tiles) {
    climate_prepare_t *const prepare = &state->prepare;
    const int32_t reach = tiles ? 0 : CLIMATE_WINDOW_TILES / 2;
    const uint32_t side = tiles ? CLIMATE_TILE_SIDE : CLIMATE_MASK_SIDE;
    climate_slot_t *const slots = tiles ? state->borrowed_tile_slots : state->borrowed_mask_slots;
    prepare->start = atomic_load_explicit(&state->jobs_done, memory_order_acquire);
    prepare->submitted = 0u;
    for (int32_t y = prepare->tile_y_min - reach; y <= prepare->tile_y_max + reach; y++) {
        for (int32_t x = prepare->tile_x_min - reach; x <= prepare->tile_x_max + reach; x++) {
            climate_slot_t *const slot = &slots[climate_cache_slot_index(x, y, side)];
            const bool keeps = climate_cache_keeps(x, y, prepare->keep_x, prepare->keep_y, side);
            if (climate_cache_claim(slot, climate_cache_key(x, y), keeps) != CLIMATE_CLAIM_OURS) {
                continue; /* there, being built, or not to be kept: a tile job takes care of a page like that */
            }
            climate_slot_job_set(slot, state, x, y);
            if (tiles) {
                atomic_fetch_add_explicit(&state->jobs_in_flight, 1u, memory_order_relaxed);
            }
            job_submit(system, &state->group, tiles ? climate_tile_job : climate_page_job, slot);
            prepare->submitted++;
        }
    }
    prepare->phase = tiles ? CLIMATE_PREPARE_TILES : CLIMATE_PREPARE_PAGES;
}

void climate_prepare_begin(climate_t *climate, job_system_t *system, int32_t x_min, int32_t y_min, int32_t x_max,
                           int32_t y_max) {
    climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u) {
        return;
    }
    uint32_t done = 0u, total = 0u;
    while (!climate_prepare_step(climate, system, &done, &total)) {
        time_sleep_ns(CLIMATE_PREPARE_POLL_NS);
    }
    climate_prepare_t *const prepare = &state->prepare;
    memset(prepare, 0, sizeof(*prepare));
    prepare->tile_x_min = climate_tile_low(x_min);
    prepare->tile_x_max = climate_tile_high(x_max);
    prepare->tile_y_min = climate_tile_low(y_min);
    prepare->tile_y_max = climate_tile_high(y_max);
    prepare->keep_x = (int32_t)(((int64_t)prepare->tile_x_min + (int64_t)prepare->tile_x_max) >> 1);
    prepare->keep_y = (int32_t)(((int64_t)prepare->tile_y_min + (int64_t)prepare->tile_y_max) >> 1);
    atomic_store_explicit(&state->keep_tile_x, prepare->keep_x, memory_order_relaxed);
    atomic_store_explicit(&state->keep_tile_y, prepare->keep_y, memory_order_relaxed);

    /* How much there is to do, for progress: every page and tile not built yet. */
    const int32_t reach = CLIMATE_WINDOW_TILES / 2;
    for (int32_t py = prepare->tile_y_min - reach; py <= prepare->tile_y_max + reach; py++) {
        for (int32_t px = prepare->tile_x_min - reach; px <= prepare->tile_x_max + reach; px++) {
            const climate_slot_t *const slot =
                &state->borrowed_mask_slots[climate_cache_slot_index(px, py, CLIMATE_MASK_SIDE)];
            prepare->total += climate_cache_ready(slot, climate_cache_key(px, py)) ? 0u : 1u;
        }
    }
    for (int32_t ty = prepare->tile_y_min; ty <= prepare->tile_y_max; ty++) {
        for (int32_t tx = prepare->tile_x_min; tx <= prepare->tile_x_max; tx++) {
            const climate_slot_t *const slot =
                &state->borrowed_tile_slots[climate_cache_slot_index(tx, ty, CLIMATE_TILE_SIDE)];
            prepare->total += climate_cache_ready(slot, climate_cache_key(tx, ty)) ? 0u : 1u;
        }
    }
    climate_prepare_submit(state, system, false);
}

bool climate_prepare_step(climate_t *climate, job_system_t *system, uint32_t *out_done, uint32_t *out_total) {
    climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u || state->prepare.phase == CLIMATE_PREPARE_NONE) {
        *out_done = 0u;
        *out_total = 0u;
        return true;
    }
    climate_prepare_t *const prepare = &state->prepare;
    /* Unsigned, so a counter that has wrapped still subtracts right. */
    const uint32_t finished = atomic_load_explicit(&state->jobs_done, memory_order_acquire) - prepare->start;
    const uint32_t counted = finished > prepare->submitted ? prepare->submitted : finished;
    const uint32_t done = prepare->done_before + counted;
    *out_done = done > prepare->total ? prepare->total : done;
    *out_total = prepare->total;
    if (finished < prepare->submitted) {
        return false;
    }
    /* Every job of the round has counted itself, so this wait is only for each to have returned: short, and it makes
     * every job's writes this thread's to see. */
    job_group_wait(system, &state->group);
    if (prepare->phase == CLIMATE_PREPARE_PAGES) {
        prepare->done_before += prepare->submitted;
        climate_prepare_submit(state, system, true);
        return false;
    }
    prepare->phase = CLIMATE_PREPARE_NONE;
    *out_done = prepare->total; /* pages a tile job built for itself counted too */
    return true;
}

void climate_prepare_area(climate_t *climate, job_system_t *system, int32_t x_min, int32_t y_min, int32_t x_max,
                          int32_t y_max, void (*progress)(void *context, uint32_t done, uint32_t total),
                          void *progress_context) {
    climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u) {
        return;
    }
    climate_prepare_begin(climate, system, x_min, y_min, x_max, y_max);
    uint32_t told = UINT32_MAX;
    for (;;) {
        uint32_t done = 0u, total = 0u;
        const bool finished = climate_prepare_step(climate, system, &done, &total);
        if (progress != nullptr && (done != told || finished)) {
            progress(progress_context, done, total);
            told = done;
        }
        if (finished) {
            return;
        }
        time_sleep_ns(CLIMATE_PREPARE_POLL_NS);
    }
}

void climate_prepare_tile(climate_t *climate, job_system_t *system, int32_t tile_x, int32_t tile_y) {
    climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u) {
        return;
    }
    atomic_store_explicit(&state->keep_tile_x, tile_x, memory_order_relaxed);
    atomic_store_explicit(&state->keep_tile_y, tile_y, memory_order_relaxed);
    climate_slot_t *const slot =
        &state->borrowed_tile_slots[climate_cache_slot_index(tile_x, tile_y, CLIMATE_TILE_SIDE)];
    if (climate_cache_claim(slot, climate_cache_key(tile_x, tile_y), true) == CLIMATE_CLAIM_OURS) {
        climate_slot_job_set(slot, state, tile_x, tile_y);
        atomic_fetch_add_explicit(&state->jobs_in_flight, 1u, memory_order_relaxed);
        job_submit(system, &state->group, climate_tile_job, slot);
    }
    job_group_wait(system, &state->group);
}

/* ---- Reads --------------------------------------------------------------------------------------------------------- */

bool climate_area_ready(const climate_t *climate, int32_t x_min, int32_t y_min, int32_t x_max, int32_t y_max) {
    const climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u) {
        return false;
    }
    const int32_t tx0 = climate_tile_low(x_min), tx1 = climate_tile_high(x_max);
    const int32_t ty0 = climate_tile_low(y_min), ty1 = climate_tile_high(y_max);
    for (int32_t ty = ty0; ty <= ty1; ty++) {
        for (int32_t tx = tx0; tx <= tx1; tx++) {
            const climate_slot_t *const slot =
                &state->borrowed_tile_slots[climate_cache_slot_index(tx, ty, CLIMATE_TILE_SIDE)];
            if (!climate_cache_ready(slot, climate_cache_key(tx, ty))) {
                return false;
            }
        }
    }
    return true;
}

/* A cell by its cell coordinates, under the read protocol. */
static bool climate_cell_read(const climate_state_t *state, int64_t cell_x, int64_t cell_y, climate_cell_t *out) {
    const int32_t tile_x = (int32_t)(cell_x >> (CLIMATE_TILE_SHIFT - CLIMATE_CELL_SHIFT));
    const int32_t tile_y = (int32_t)(cell_y >> (CLIMATE_TILE_SHIFT - CLIMATE_CELL_SHIFT));
    climate_slot_t *const slot =
        &state->borrowed_tile_slots[climate_cache_slot_index(tile_x, tile_y, CLIMATE_TILE_SIDE)];
    if (!climate_cache_read_begin(slot, climate_cache_key(tile_x, tile_y))) {
        return false;
    }
    const int64_t in_tile = (int64_t)CLIMATE_TILE_CELLS - 1;
    const uint64_t index = (uint64_t)((cell_y & in_tile) * CLIMATE_TILE_CELLS + (cell_x & in_tile));
    *out = climate_tile_cells(state, slot)[index];
    climate_cache_read_end(slot);
    return true;
}

bool climate_cell_get(const climate_t *climate, int32_t x, int32_t y, climate_cell_t *out) {
    const climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u) {
        return false;
    }
    return climate_cell_read(state, (int64_t)x >> CLIMATE_CELL_SHIFT, (int64_t)y >> CLIMATE_CELL_SHIFT, out);
}

/* As the terrain spreads its fields: a smooth clamp to -1 to 1. */
static float64_t climate_warp_spread(float32_t raw) {
    float64_t unit = (float64_t)raw / CLIMATE_WARP_LIMIT;
    unit = unit > 1.0f64 ? 1.0f64 : unit;
    unit = unit < -1.0f64 ? -1.0f64 : unit;
    return unit * (1.5f64 - 0.5f64 * unit * unit);
}

void climate_warp_at(const climate_t *climate, float64_t x, float64_t y, float64_t *out_x, float64_t *out_y) {
    const climate_state_t *const state = climate->borrowed_state;
    if (state->warp_off != 0u) {
        *out_x = x;
        *out_y = y;
        return;
    }
    const float64_t z = 0.0f64;
    float32_t raw_x = 0.0f32, raw_y = 0.0f32;
    terrain_noise_sample_points(state->borrowed_warp_x, &x, &y, &z, 1u, &raw_x);
    terrain_noise_sample_points(state->borrowed_warp_y, &x, &y, &z, 1u, &raw_y);
    *out_x = x + (float64_t)CLIMATE_WARP_BLOCKS * climate_warp_spread(raw_x);
    *out_y = y + (float64_t)CLIMATE_WARP_BLOCKS * climate_warp_spread(raw_y);
}

void climate_warp_set(climate_t *climate, bool on) {
    climate->borrowed_state->warp_off = (uint8_t)(on ? 0u : 1u);
}

/* The four cells round a read at (read_x, read_y), already warped, south-west, south-east, north-west, north-east, and
 * how far across them it is. False if a tile is not built. */
static bool climate_four_cells(const climate_state_t *state, float64_t read_x, float64_t read_y,
                               climate_cell_t cells[4], float32_t *out_t_x, float32_t *out_t_y) {
    /* Cell centres are at 64 k + 32: the four round the read point, and how far across them it is. */
    const float64_t u = (read_x - 0.5f64 * (float64_t)CLIMATE_CELL_BLOCKS) / (float64_t)CLIMATE_CELL_BLOCKS;
    const float64_t v = (read_y - 0.5f64 * (float64_t)CLIMATE_CELL_BLOCKS) / (float64_t)CLIMATE_CELL_BLOCKS;
    const float64_t u_floor = floor(u), v_floor = floor(v);
    const int64_t cell_x = (int64_t)u_floor, cell_y = (int64_t)v_floor;
    for (uint32_t k = 0u; k < 4u; k++) {
        if (!climate_cell_read(state, cell_x + (int64_t)(k & 1u), cell_y + (int64_t)(k >> 1u), &cells[k])) {
            return false;
        }
    }
    *out_t_x = (float32_t)(u - u_floor);
    *out_t_y = (float32_t)(v - v_floor);
    return true;
}

bool climate_sample_at(const climate_t *climate, float64_t x, float64_t y, climate_sample_t *out) {
    const climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u) {
        return false;
    }
    float64_t read_x = x, read_y = y;
    climate_warp_at(climate, x, y, &read_x, &read_y);
    climate_cell_t cells[4];
    float32_t t_x = 0.0f32, t_y = 0.0f32;
    if (!climate_four_cells(state, read_x, read_y, cells, &t_x, &t_y)) {
        return false;
    }
    const climate_cell_t *const corners[4] = {&cells[0], &cells[1], &cells[2], &cells[3]};
    climate_cell_blend(corners, t_x, t_y, out);
    /* Never blended: at a pole two cells either side both say about 89.6, and a blend of them never reaches 90. */
    out->lat_deg = (float32_t)climate_latitude_deg(y);
    return true;
}

/* ---- Air: phase 04 ------------------------------------------------------------------------------------------------- */

/* Along x, then y, as climate_cell_blend does. */
static float32_t climate_lerp4(float32_t south_west, float32_t south_east, float32_t north_west, float32_t north_east,
                               float32_t t_x, float32_t t_y) {
    const float32_t south = south_west + t_x * (south_east - south_west);
    const float32_t north = north_west + t_x * (north_east - north_west);
    return south + t_y * (north - south);
}

static float32_t climate_cell_current_c(const climate_cell_t *cell) {
    return climate_air_current_c(cell->current) * (float32_t)cell->current_strength / 255.0f32;
}

bool climate_air_at(const climate_t *climate, float64_t x, float64_t y, float32_t day, climate_air_t *out) {
    const climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u) {
        return false;
    }
    float64_t read_x = x, read_y = y;
    climate_warp_at(climate, x, y, &read_x, &read_y);
    climate_cell_t cells[4];
    float32_t t_x = 0.0f32, t_y = 0.0f32;
    if (!climate_four_cells(state, read_x, read_y, cells, &t_x, &t_y)) {
        return false;
    }
    /* Evaluating a wave and blending are both linear, so the coefficients are blended, then the wave evaluated once. */
    float32_t wind[6], pressure[3];
    for (uint32_t i = 0u; i < 6u; i++) {
        wind[i] = climate_lerp4(cells[0].wind[i], cells[1].wind[i], cells[2].wind[i], cells[3].wind[i], t_x, t_y);
    }
    for (uint32_t i = 0u; i < 3u; i++) {
        pressure[i] = climate_lerp4(cells[0].pressure[i], cells[1].pressure[i], cells[2].pressure[i],
                                    cells[3].pressure[i], t_x, t_y);
    }
    memset(out, 0, sizeof(*out));
    out->wind[0] = climate_harmonic_at1(&wind[0], day);
    out->wind[1] = climate_harmonic_at1(&wind[3], day);
    out->pressure_heat_hpa = climate_harmonic_at1(pressure, day);
    const float32_t psi = climate_air_wind_latitude(climate_latitude_deg(y), day);
    out->pressure_belt_hpa = (float32_t)climate_normals_p_belt(state->borrowed_normals, (float64_t)psi);
    out->pressure_hpa = out->pressure_belt_hpa + out->pressure_heat_hpa;
    out->current_c = climate_lerp4(climate_cell_current_c(&cells[0]), climate_cell_current_c(&cells[1]),
                                   climate_cell_current_c(&cells[2]), climate_cell_current_c(&cells[3]), t_x, t_y);
    /* A kind is a category: the nearest cell's, never a blend. */
    const uint32_t nearest = (t_x >= 0.5f32 ? 1u : 0u) + (t_y >= 0.5f32 ? 2u : 0u);
    out->current = cells[nearest].current;
    out->current_strength = cells[nearest].current_strength;
    return true;
}

float32_t climate_sea_temperature(const climate_t *climate, const climate_cell_t *cell, float32_t day) {
    return climate_air_sea_temperature(climate->borrowed_state->borrowed_normals, (float64_t)cell->lat_deg,
                                       climate_cell_current_c(cell), day);
}

void climate_current_flow(const climate_cell_t *cell, float32_t y_centre, float32_t out[2]) {
    if ((cell->flags & CLIMATE_CELL_SEA) != 0u) {
        climate_air_flow(cell->current, cell->land_direction, (float64_t)y_centre, out);
        return;
    }
    const float32_t back[2] = {-cell->sea_direction[0], -cell->sea_direction[1]};
    climate_air_flow(cell->current, back, (float64_t)y_centre, out);
}

const climate_normals_t *climate_normals(const climate_t *climate) {
    return climate->borrowed_state->borrowed_normals;
}

/* ---- Normals: phase 05 --------------------------------------------------------------------------------------------- */

void climate_test_hooks_set(climate_t *climate, const climate_test_hooks_t *hooks) {
    climate->borrowed_state->hooks = *hooks;
}

/* The snow balance height blended over the cells that are not sea. A sea cell holds CLIMATE_SNOW_BALANCE_NONE as "no
 * height", not as a height, so blending it in would lift a coast's snow line by up to 4900 blocks half a cell from the
 * shore (the phase 05 file, Issues found 12). Each corner's bilinear weight, renormalised over the land and lake corners;
 * NONE when all four are sea. */
static float32_t climate_snow_balance_blend(const climate_cell_t cells[4], float32_t t_x, float32_t t_y) {
    const float32_t weight[4] = {(1.0f32 - t_x) * (1.0f32 - t_y), t_x * (1.0f32 - t_y), (1.0f32 - t_x) * t_y,
                                 t_x * t_y};
    float32_t sum = 0.0f32, total = 0.0f32;
    for (uint32_t k = 0u; k < 4u; k++) {
        if ((cells[k].flags & CLIMATE_CELL_SEA) == 0u) {
            sum += weight[k] * cells[k].snow_balance_z;
            total += weight[k];
        }
    }
    if (total <= 0.0f32) {
        return CLIMATE_SNOW_BALANCE_NONE;
    }
    return sum / total;
}

bool climate_normals_at(const climate_t *climate, float64_t x, float64_t y, float32_t z, float32_t day,
                        climate_normals_at_t *out) {
    return climate_normals_days_at(climate, x, y, z, &day, 1u, out);
}

bool climate_normals_days_at(const climate_t *climate, float64_t x, float64_t y, float32_t z, const float32_t *days,
                             uint32_t count, climate_normals_at_t *out) {
    const climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u || count == 0u) {
        return false;
    }
    float64_t read_x = x, read_y = y;
    climate_warp_at(climate, x, y, &read_x, &read_y);
    climate_cell_t cells[4];
    float32_t t_x = 0.0f32, t_y = 0.0f32;
    if (!climate_four_cells(state, read_x, read_y, cells, &t_x, &t_y)) {
        return false;
    }
    /* The stored numbers blended, then the formulas evaluated once: exact for the rain waves, which are linear, and a
     * fraction of a degree from evaluating then blending for the temperature wave, whose lag is inside the cosine. */
#define CLIMATE_BLEND(field) climate_lerp4(cells[0].field, cells[1].field, cells[2].field, cells[3].field, t_x, t_y)
    climate_cell_t blend = cells[0];
    blend.t_mean = CLIMATE_BLEND(t_mean);
    blend.t_swing = CLIMATE_BLEND(t_swing);
    blend.t_lag_days = CLIMATE_BLEND(t_lag_days);
    blend.kappa = CLIMATE_BLEND(kappa);
    blend.z_ground = CLIMATE_BLEND(z_ground);
    blend.snow_balance_z = climate_snow_balance_blend(cells, t_x, t_y);
    for (uint32_t i = 0u; i < 5u; i++) {
        blend.rain_mm_week[i] = CLIMATE_BLEND(rain_mm_week[i]);
    }
#undef CLIMATE_BLEND
    const uint32_t nearest_index = (t_x >= 0.5f32 ? 1u : 0u) + (t_y >= 0.5f32 ? 2u : 0u);
    const climate_cell_t *const nearest = &cells[nearest_index];
    blend.lat_deg = (float32_t)climate_latitude_deg(y);
    const bool sea = (nearest->flags & CLIMATE_CELL_SEA) != 0u;
    const bool land = (nearest->flags & (CLIMATE_CELL_SEA | CLIMATE_CELL_LAKE)) == 0u;
    const float32_t at_z = isnan(z) ? blend.z_ground : z;

    /* What does not change with the day, once: the wettest day, the months, the place's labels. */
    climate_normals_at_t base;
    memset(&base, 0, sizeof(base));
    base.z = at_z;
    base.t_mean_c = climate_heat_base(blend.t_mean, at_z, land);
    const float32_t *const rain = blend.rain_mm_week;
    base.rain_mm_year = fmaxf(0.0f32, rain[0]) * CLIMATE_DAYS_A_YEAR / CLIMATE_DAYS_A_WEEK;
    base.rain_max_mm_day = climate_harmonic_max2(rain) / CLIMATE_DAYS_A_WEEK; /* the wettest day, to within 5 days */
    climate_season_months_t months;
    climate_season_months(&blend, at_z, land, &months);
    base.t_max_month_c = months.t_max_month_c;
    base.t_min_month_c = months.t_min_month_c;
    base.snow_balance_z = blend.snow_balance_z;
    base.sea_temperature_c = nanf("");
    base.climate_class = nearest->climate_class;
    base.flags = nearest->flags;
    for (uint32_t i = 0u; i < count; i++) {
        const float32_t day = days[i];
        climate_normals_at_t *const one = &out[i];
        *one = base;
        one->t_day_c = climate_heat_formula(blend.t_mean, at_z, land, blend.t_swing, blend.t_lag_days, day);
        one->rain_mm_day = fmaxf(0.0f32, climate_harmonic_at2(rain, day)) / CLIMATE_DAYS_A_WEEK;
        one->t_range_c = climate_heat_daily_range(blend.kappa, one->rain_mm_day);
        one->t_high_c = one->t_day_c + 0.5f32 * one->t_range_c;
        one->t_low_c = one->t_day_c - 0.5f32 * one->t_range_c;
        one->snow_share = climate_heat_snow_share(one->t_day_c);
        if (sea) {
            one->sea_temperature_c = climate_sea_temperature(climate, nearest, day);
            one->hurricane_s = climate_season_hurricane_s(nearest->lat_deg, one->sea_temperature_c);
        }
        one->hurricane_season = climate_season_hurricane_today(nearest->hurricane_days, day) ? 1u : 0u;
        one->monsoon_today = climate_season_monsoon_today(nearest, day) ? 1u : 0u;
    }
    return true;
}

/* ---- Weather systems: phase 06 ------------------------------------------------------------------------------------- */

uint64_t climate_seed(const climate_t *climate) {
    return climate->borrowed_state->seed;
}

uint64_t climate_systems_seed(const climate_t *climate, uint8_t kind) {
    ASSERT(kind < CLIMATE_SYSTEM_KINDS);
    return climate->borrowed_state->systems_seed[kind];
}

void climate_systems_hooks_set(climate_t *climate, const climate_systems_hooks_t *hooks) {
    climate->borrowed_state->systems_hooks = *hooks;
}

climate_systems_hooks_t climate_systems_hooks(const climate_t *climate) {
    return climate->borrowed_state->systems_hooks;
}

climate_mask_source_t climate_mask_page_read(const climate_t *climate, int32_t tile_x, int32_t tile_y, float32_t *out) {
    const climate_state_t *const state = climate->borrowed_state;
    const climate_mask_t mask = climate_mask_of(state);
    return climate_mask_page_get(&mask, tile_x, tile_y, atomic_load_explicit(&state->keep_tile_x, memory_order_relaxed),
                                 atomic_load_explicit(&state->keep_tile_y, memory_order_relaxed), out);
}

/* The cell holding (x, y): its tile, and its index in the tile's page. */
static void climate_mask_cell_of(float64_t x, float64_t y, int64_t *cell_x, int64_t *cell_y, int32_t *tile_x,
                                 int32_t *tile_y, uint64_t *index) {
    *cell_x = (int64_t)floor(x / (float64_t)CLIMATE_CELL_BLOCKS);
    *cell_y = (int64_t)floor(y / (float64_t)CLIMATE_CELL_BLOCKS);
    *tile_x = (int32_t)(*cell_x >> (CLIMATE_TILE_SHIFT - CLIMATE_CELL_SHIFT));
    *tile_y = (int32_t)(*cell_y >> (CLIMATE_TILE_SHIFT - CLIMATE_CELL_SHIFT));
    const int64_t in_tile = (int64_t)CLIMATE_TILE_CELLS - 1;
    *index = (uint64_t)((*cell_y & in_tile) * CLIMATE_TILE_CELLS + (*cell_x & in_tile));
}

bool climate_mask_page_kept(const climate_t *climate, int32_t tile_x, int32_t tile_y) {
    const climate_state_t *const state = climate->borrowed_state;
    return climate_cache_keeps(tile_x, tile_y, atomic_load_explicit(&state->keep_tile_x, memory_order_relaxed),
                               atomic_load_explicit(&state->keep_tile_y, memory_order_relaxed), CLIMATE_MASK_SIDE);
}

float32_t climate_mask_point(const climate_t *climate, float64_t x, float64_t y) {
    const climate_state_t *const state = climate->borrowed_state;
    int64_t cell_x = 0, cell_y = 0;
    int32_t tile_x = 0, tile_y = 0;
    uint64_t index = 0u;
    climate_mask_cell_of(x, y, &cell_x, &cell_y, &tile_x, &tile_y, &index);
    /* The ground is a pure function of each column (climate_ground_fn_t), so one column alone is the page's value: the
     * mask test holds a real page to single estimates bit for bit. */
    float32_t height = 0.0f32;
    const int64_t centre_x = cell_x * CLIMATE_CELL_BLOCKS + CLIMATE_CELL_BLOCKS / 2;
    const int64_t centre_y = cell_y * CLIMATE_CELL_BLOCKS + CLIMATE_CELL_BLOCKS / 2;
    state->ground(state->borrowed_ground_context, (int32_t)centre_x, (int32_t)centre_y, CLIMATE_CELL_BLOCKS, 1u, 1u,
                  &height);
    return height;
}

float32_t climate_mask_height_at(const climate_t *climate, arena_t *arena, float64_t x, float64_t y) {
    const climate_state_t *const state = climate->borrowed_state;
    int64_t cell_x = 0, cell_y = 0;
    int32_t tile_x = 0, tile_y = 0;
    uint64_t index = 0u;
    climate_mask_cell_of(x, y, &cell_x, &cell_y, &tile_x, &tile_y, &index);
    /* The one value, if the page is there: no copy of the other 255. */
    const uint32_t slot_index = climate_cache_slot_index(tile_x, tile_y, CLIMATE_MASK_SIDE);
    climate_slot_t *const slot = &state->borrowed_mask_slots[slot_index];
    if (climate_cache_read_begin(slot, climate_cache_key(tile_x, tile_y))) {
        const float32_t height =
            state->borrowed_mask_heights[(uint64_t)slot_index * (uint64_t)CLIMATE_TILE_CELL_COUNT + index];
        climate_cache_read_end(slot);
        return height;
    }
    if (!climate_mask_page_kept(climate, tile_x, tile_y)) {
        return climate_mask_point(climate, x, y); /* a page could not be kept here: one column, not 256 */
    }
    float32_t *const page =
        arena_alloc_array(arena, (uint64_t)CLIMATE_TILE_CELL_COUNT, sizeof(float32_t), alignof(float32_t));
    (void)climate_mask_page_read(climate, tile_x, tile_y, page);
    return page[index];
}
