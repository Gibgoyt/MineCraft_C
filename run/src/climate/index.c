/* See README.md. The Owner of plan 004's climate: latitude from y alone (master plan 5.1, phase 02), and the climate cells
 * of phase 03: the region, the warp, the tile jobs, the scheduler and the reads. */
#include "climate/index.h"

#include "assert/index.h"
#include "log/index.h"
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
    state->borrowed_tile_slots = (climate_slot_t *)(void *)(base + CLIMATE_TILE_SLOTS_OFFSET);
    state->borrowed_mask_slots = (climate_slot_t *)(void *)(base + CLIMATE_MASK_SLOTS_OFFSET);
    state->borrowed_cells = (climate_cell_t *)(void *)(base + CLIMATE_CELLS_OFFSET);
    state->borrowed_mask_heights = (float32_t *)(void *)(base + CLIMATE_MASK_OFFSET);
    climate->borrowed_state = state;
    return true;
}

void climate_deinit(climate_t *climate) {
    climate_state_t *const state = climate->borrowed_state;
    if (state != nullptr) {
        ASSERT_ALWAYS(state->open == 0u);
        job_group_deinit(&state->group);
        arena_deinit(&state->arena_tables);
    }
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
    atomic_store_explicit(&state->keep_tile_x, 0, memory_order_relaxed);
    atomic_store_explicit(&state->keep_tile_y, 0, memory_order_relaxed);
    atomic_store_explicit(&state->jobs_in_flight, 0u, memory_order_relaxed);
    atomic_store_explicit(&state->jobs_done, 0u, memory_order_relaxed);
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
    terrain_noise_bank_deinit(&state->warp_bank);
    state->ground = nullptr;
    state->borrowed_ground_context = nullptr;
    state->borrowed_warp_x = nullptr;
    state->borrowed_warp_y = nullptr;
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
    climate_cell_build_tile(arena, state->ground, state->borrowed_ground_context, tile_x, tile_y, window,
                            climate_tile_cells(state, slot));
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

/* Until the submitted jobs counted from start have finished, telling progress as they do; then the group's wait, which
 * makes every job's writes this thread's to see. done_before is what earlier rounds did. */
static void climate_prepare_wait(climate_state_t *state, const job_system_t *system, uint32_t start, uint32_t submitted,
                                 uint32_t done_before, uint32_t total,
                                 void (*progress)(void *context, uint32_t done, uint32_t total),
                                 void *progress_context) {
    uint32_t told = UINT32_MAX;
    for (;;) {
        /* Unsigned, so a counter that has wrapped still subtracts right. */
        const uint32_t finished = atomic_load_explicit(&state->jobs_done, memory_order_acquire) - start;
        const uint32_t counted = finished > submitted ? submitted : finished;
        uint32_t done = done_before + counted;
        done = done > total ? total : done;
        if (progress != nullptr && done != told) {
            progress(progress_context, done, total);
            told = done;
        }
        if (finished >= submitted) {
            break;
        }
        time_sleep_ns(CLIMATE_PREPARE_POLL_NS);
    }
    job_group_wait(system, &state->group);
}

void climate_prepare_area(climate_t *climate, job_system_t *system, int32_t x_min, int32_t y_min, int32_t x_max,
                          int32_t y_max, void (*progress)(void *context, uint32_t done, uint32_t total),
                          void *progress_context) {
    climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u) {
        return;
    }
    const int32_t tx0 = climate_tile_low(x_min), tx1 = climate_tile_high(x_max);
    const int32_t ty0 = climate_tile_low(y_min), ty1 = climate_tile_high(y_max);
    const int32_t keep_x = (int32_t)(((int64_t)tx0 + (int64_t)tx1) >> 1);
    const int32_t keep_y = (int32_t)(((int64_t)ty0 + (int64_t)ty1) >> 1);
    atomic_store_explicit(&state->keep_tile_x, keep_x, memory_order_relaxed);
    atomic_store_explicit(&state->keep_tile_y, keep_y, memory_order_relaxed);
    const int32_t reach = CLIMATE_WINDOW_TILES / 2;

    /* How much there is to do, for progress: every page and tile not built yet. */
    uint32_t total = 0u;
    for (int32_t py = ty0 - reach; py <= ty1 + reach; py++) {
        for (int32_t px = tx0 - reach; px <= tx1 + reach; px++) {
            const climate_slot_t *const slot =
                &state->borrowed_mask_slots[climate_cache_slot_index(px, py, CLIMATE_MASK_SIDE)];
            total += climate_cache_ready(slot, climate_cache_key(px, py)) ? 0u : 1u;
        }
    }
    for (int32_t ty = ty0; ty <= ty1; ty++) {
        for (int32_t tx = tx0; tx <= tx1; tx++) {
            const climate_slot_t *const slot =
                &state->borrowed_tile_slots[climate_cache_slot_index(tx, ty, CLIMATE_TILE_SIDE)];
            total += climate_cache_ready(slot, climate_cache_key(tx, ty)) ? 0u : 1u;
        }
    }
    uint32_t start = atomic_load_explicit(&state->jobs_done, memory_order_acquire);
    uint32_t submitted = 0u;

    /* Pages first, one job each, so no tile job builds a page another is building. */
    for (int32_t py = ty0 - reach; py <= ty1 + reach; py++) {
        for (int32_t px = tx0 - reach; px <= tx1 + reach; px++) {
            climate_slot_t *const slot =
                &state->borrowed_mask_slots[climate_cache_slot_index(px, py, CLIMATE_MASK_SIDE)];
            const bool keeps = climate_cache_keeps(px, py, keep_x, keep_y, CLIMATE_MASK_SIDE);
            if (climate_cache_claim(slot, climate_cache_key(px, py), keeps) != CLIMATE_CLAIM_OURS) {
                continue; /* there, being built, or not to be kept: a tile job takes care of it */
            }
            climate_slot_job_set(slot, state, px, py);
            job_submit(system, &state->group, climate_page_job, slot);
            submitted++;
        }
    }
    climate_prepare_wait(state, system, start, submitted, 0u, total, progress, progress_context);
    const uint32_t pages_done = submitted;
    start = atomic_load_explicit(&state->jobs_done, memory_order_acquire);
    submitted = 0u;

    for (int32_t ty = ty0; ty <= ty1; ty++) {
        for (int32_t tx = tx0; tx <= tx1; tx++) {
            climate_slot_t *const slot =
                &state->borrowed_tile_slots[climate_cache_slot_index(tx, ty, CLIMATE_TILE_SIDE)];
            const bool keeps = climate_cache_keeps(tx, ty, keep_x, keep_y, CLIMATE_TILE_SIDE);
            if (climate_cache_claim(slot, climate_cache_key(tx, ty), keeps) != CLIMATE_CLAIM_OURS) {
                continue;
            }
            climate_slot_job_set(slot, state, tx, ty);
            atomic_fetch_add_explicit(&state->jobs_in_flight, 1u, memory_order_relaxed);
            job_submit(system, &state->group, climate_tile_job, slot);
            submitted++;
        }
    }
    climate_prepare_wait(state, system, start, submitted, pages_done, total, progress, progress_context);
    if (progress != nullptr) {
        progress(progress_context, total, total); /* pages a tile job built for itself counted too */
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

bool climate_sample_at(const climate_t *climate, float64_t x, float64_t y, climate_sample_t *out) {
    const climate_state_t *const state = climate->borrowed_state;
    if (state == nullptr || state->open == 0u) {
        return false;
    }
    float64_t read_x = x, read_y = y;
    climate_warp_at(climate, x, y, &read_x, &read_y);
    /* Cell centres are at 64 k + 32: the four round the read point, and how far across them it is. */
    const float64_t u = (read_x - 0.5f64 * (float64_t)CLIMATE_CELL_BLOCKS) / (float64_t)CLIMATE_CELL_BLOCKS;
    const float64_t v = (read_y - 0.5f64 * (float64_t)CLIMATE_CELL_BLOCKS) / (float64_t)CLIMATE_CELL_BLOCKS;
    const float64_t u_floor = floor(u), v_floor = floor(v);
    const int64_t cell_x = (int64_t)u_floor, cell_y = (int64_t)v_floor;
    climate_cell_t cells[4];
    for (uint32_t k = 0u; k < 4u; k++) {
        if (!climate_cell_read(state, cell_x + (int64_t)(k & 1u), cell_y + (int64_t)(k >> 1u), &cells[k])) {
            return false;
        }
    }
    const climate_cell_t *const corners[4] = {&cells[0], &cells[1], &cells[2], &cells[3]};
    climate_cell_blend(corners, (float32_t)(u - u_floor), (float32_t)(v - v_floor), out);
    /* Never blended: at a pole two cells either side both say about 89.6, and a blend of them never reaches 90. */
    out->lat_deg = (float32_t)climate_latitude_deg(y);
    return true;
}
