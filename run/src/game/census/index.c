/* See README.md. A census, run from the sim one step at a time: the weeks owed, oldest first, each week's kept tiles in blocks
 * on the workers, flow accumulation, then the outlook and the report, then the file. Every job's work depends only on pure
 * functions and the tiles it owns, and flow is one job, so the bytes do not depend on the workers. */
#include "game/census/index.h"

#include "assert/index.h"
#include "calendar/index.h"
#include "game/index.h"
#include "log/index.h"
#include "save/climate/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

enum : uint32_t {
    CENSUS_REPORT_NEW = 4u, /* in middle: the buffer there is newer than main's */
    CENSUS_REPORT_INDEX = 3u,
};

static int32_t census_floor_div(int32_t value, int32_t by) {
    const int32_t q = value / by;
    return (value % by != 0 && value < 0) ? q - 1 : q;
}

static climate_week_store_t *census_store(const game_census_t *census) {
    return climate_week_store(census->borrowed_climate);
}

/* ---- The report, from the sim to main ------------------------------------------------------------------------------ */

static void census_publish(game_census_t *census) {
    memcpy(&census->borrowed_reports[census->back], census->borrowed_work, sizeof(ui_census_report_t));
    const uint32_t previous =
        atomic_exchange_explicit(&census->middle, census->back | CENSUS_REPORT_NEW, memory_order_acq_rel);
    census->back = previous & CENSUS_REPORT_INDEX;
}

const ui_census_report_t *game_census_report(game_census_t *census) {
    if ((atomic_load_explicit(&census->middle, memory_order_acquire) & CENSUS_REPORT_NEW) != 0u) {
        const uint32_t previous = atomic_exchange_explicit(&census->middle, census->front, memory_order_acq_rel);
        census->front = previous & CENSUS_REPORT_INDEX;
    }
    return &census->borrowed_reports[census->front];
}

/* ---- Open and close ------------------------------------------------------------------------------------------------ */

void game_census_open(game_census_t *census, arena_t *arena, climate_t *climate, job_system_t *job,
                      const char *directory, uint64_t seed, uint64_t last_census_week) {
    memset(census, 0, sizeof(*census));
    job_group_init(&census->group);
    census->borrowed_climate = climate;
    census->borrowed_job = job;
    census->borrowed_directory = directory;
    census->seed = seed;
    census->borrowed_jobs =
        arena_alloc_array(arena, CLIMATE_WEEK_TILES_MAX, sizeof(game_census_job_t), alignof(game_census_job_t));
    census->borrowed_sorted = arena_alloc_array(arena, CLIMATE_WEEK_TILES_MAX, sizeof(uint32_t), 64u);
    census->borrowed_scratch = arena_alloc_array(arena, CLIMATE_WEEK_TILES_MAX, sizeof(uint32_t), 64u);
    census->borrowed_batches = arena_alloc_array(arena, GAME_CENSUS_BATCHES_MAX + 1u, sizeof(uint32_t), 64u);
    census->borrowed_input = arena_alloc(arena, sizeof(climate_outlook_input_t), alignof(climate_outlook_input_t));
    census->borrowed_reports =
        arena_alloc_zeroed(arena, GAME_CENSUS_REPORTS * sizeof(ui_census_report_t), alignof(ui_census_report_t));
    census->borrowed_work = arena_alloc_zeroed(arena, sizeof(ui_census_report_t), alignof(ui_census_report_t));
    census->back = 0u;
    atomic_store_explicit(&census->middle, 1u, memory_order_relaxed);
    census->front = 2u;
    census->borrowed_work->written = 1u; /* nothing owed yet: nothing to wait for */
    census_publish(census);

    climate_week_store_t *const store = census_store(census);
    uint64_t loaded = 0u;
    const save_climate_status_t status =
        directory != nullptr ? save_climate_open(directory, seed, last_census_week, store, &loaded) : SAVE_CLIMATE_NONE;
    census->refused = status == SAVE_CLIMATE_REFUSED ? 1u : 0u;
    if (status == SAVE_CLIMATE_LOADED && loaded < last_census_week) {
        /* Killed after the trigger moved the week on but before the file was written: the same weeks again give the same
         * bytes, as everything they read is pure. */
        census->next_week = loaded + 1u;
        census->last_week = last_census_week;
        const uint64_t count = last_census_week - loaded;
        census->fast_until = count > CALENDAR_FULL_WEEKS_MAX ? loaded + (count - CALENDAR_FULL_WEEKS_MAX) : 0u;
        LOG_INFO("census: the weekly state is of week %" PRIu64 " and the world of week %" PRIu64 ": %" PRIu64
                 " weeks are worked out again",
                 loaded, last_census_week, count);
    } else if (status != SAVE_CLIMATE_LOADED) {
        store->week = last_census_week; /* tiles start at its spin-up */
    }
}

void game_census_close(game_census_t *census) {
    if (census->borrowed_job == nullptr) {
        return;
    }
    job_group_wait(census->borrowed_job, &census->group);
    job_group_deinit(&census->group);
    climate_week_store_t *const store = census_store(census);
    store->busy = 0u;
    store->pending_count = 0u;
    census->borrowed_job = nullptr;
}

/* ---- The weeks owed ------------------------------------------------------------------------------------------------ */

void game_census_week(void *context, uint64_t week, bool fast) {
    game_census_t *const census = context;
    if (census->next_week == 0u) {
        census->next_week = week;
    }
    census->last_week = week > census->last_week ? week : census->last_week;
    if (fast) {
        census->fast_until = week > census->fast_until ? week : census->fast_until;
    }
    /* Before the trigger's latch goes to main: the screen it opens must not show the last census as done. */
    ui_census_report_t *const work = census->borrowed_work;
    work->written = 0u;
    work->failed = 0u;
    const uint64_t first = census->stage == GAME_CENSUS_IDLE ? census->next_week : census->first_week;
    work->weeks_total = (uint32_t)(census->last_week - first + 1u);
    census_publish(census);
}

bool game_census_idle(const game_census_t *census) {
    return census->stage == GAME_CENSUS_IDLE && census->next_week == 0u;
}

bool game_census_quiet(const game_census_t *census) {
    return census->stage == GAME_CENSUS_PREPARE;
}

/* ---- Jobs ---------------------------------------------------------------------------------------------------------- */

static void census_block_job(job_worker_t *worker, void *borrowed_argument) {
    game_census_job_t *const job = borrowed_argument;
    const game_census_t *const census = job->borrowed_census;
    climate_week_block_run(census->borrowed_climate, census_store(census), census->job_week, census->job_fast != 0u,
                           census->job_events != 0u, &worker->arena_job, &job->block);
    atomic_store_explicit(&job->done, 1u, memory_order_release);
}

static void census_flow_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    game_census_t *const census = borrowed_argument;
    climate_week_flow_run(census_store(census));
    atomic_store_explicit(&census->single_done, 1u, memory_order_release);
}

/* The report's events, names and here, from the store and the player's place. */
static void census_report_fill(game_census_t *census, ui_census_report_t *work) {
    const climate_week_store_t *const store = census_store(census);
    memset(work->event_cells, 0, sizeof(work->event_cells));
    memset(work->hurricane_names, 0, sizeof(work->hurricane_names));
    work->hurricane_count = 0u;
    const float64_t radius = (float64_t)census->radius_blocks;
    uint64_t ids[UI_CENSUS_NAMES_MAX] = {0};
    static const uint8_t BITS[UI_CENSUS_EVENT_COUNT] = {
        CLIMATE_WEEK_EVENT_FROST,      CLIMATE_WEEK_EVENT_HEAT_WAVE, CLIMATE_WEEK_EVENT_THUNDERSTORM,
        CLIMATE_WEEK_EVENT_STORM,      CLIMATE_WEEK_EVENT_HURRICANE, 0u,
        CLIMATE_WEEK_EVENT_FIRST_SNOW, CLIMATE_WEEK_EVENT_LAKE_ICE,
    };
    for (uint32_t s = 0u; s < store->tile_count; s++) {
        const uint32_t t = census->borrowed_sorted[s];
        const climate_week_tile_t *const tile = &store->borrowed_tiles[t];
        const climate_week_side_t *const side = &store->borrowed_sides[t];
        bool near = false;
        for (uint32_t c = 0u; c < CLIMATE_TILE_CELL_COUNT; c++) {
            const float64_t x =
                ((float64_t)(tile->tile_x * CLIMATE_TILE_CELLS) + (float64_t)(c % 16u)) * 64.0f64 + 32.0f64;
            const float64_t y =
                ((float64_t)(tile->tile_y * CLIMATE_TILE_CELLS) + (float64_t)(c / 16u)) * 64.0f64 + 32.0f64;
            const float64_t dx = x - census->player_x, dy = y - census->player_y;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            near = true;
            for (uint32_t e = 0u; e < UI_CENSUS_EVENT_COUNT; e++) {
                const bool flood = e == UI_CENSUS_EVENT_FLOOD && tile->cells[c].flood > 1.0f32;
                work->event_cells[e] += flood || (BITS[e] != 0u && (side->events[c] & BITS[e]) != 0u) ? 1u : 0u;
            }
        }
        for (uint32_t h = 0u; near && h < side->hurricane_count; h++) {
            bool seen = false;
            for (uint32_t i = 0u; i < work->hurricane_count; i++) {
                seen = seen || ids[i] == side->hurricane_ids[h];
            }
            if (!seen && work->hurricane_count < UI_CENSUS_NAMES_MAX) {
                ids[work->hurricane_count] = side->hurricane_ids[h];
                memcpy(work->hurricane_names[work->hurricane_count], side->hurricanes[h], UI_CENSUS_NAME_BYTES);
                work->hurricane_names[work->hurricane_count][UI_CENSUS_NAME_BYTES - 1u] = '\0';
                work->hurricane_count++;
            }
        }
    }
    const int32_t cell_x = (int32_t)floor(census->player_x / 64.0f64);
    const int32_t cell_y = (int32_t)floor(census->player_y / 64.0f64);
    const climate_week_t *const here = climate_week_store_cell(store, cell_x, cell_y);
    work->here_known = here != nullptr ? 1u : 0u;
    if (here != nullptr) {
        work->here = *here;
    } else {
        memset(&work->here, 0, sizeof(work->here));
    }
    work->south = climate_latitude_deg(census->player_y) < 0.0f64 ? 1u : 0u;
    work->tiles_full = store->full;
    work->refused = census->refused;
    work->week = census->last_week;
}

static void census_outlook_job(job_worker_t *worker, void *borrowed_argument) {
    game_census_t *const census = borrowed_argument;
    ui_census_report_t *const work = census->borrowed_work;
    census_report_fill(census, work);
    /* The census moment: 06:00 of the first day of the week shown, day 7 W. */
    const float64_t census_t = 7.0f64 * (float64_t)census->last_week;
    const bool known =
        climate_outlook_input_fill(census->borrowed_climate, census->player_x, census->player_y, census_t,
                                   census->last_week, &worker->arena_job, census->borrowed_input);
    if (known) {
        climate_outlook_run(census->borrowed_input, &work->outlook);
    } else {
        memset(&work->outlook, 0, sizeof(work->outlook));
    }
    work->outlook_known = known ? 1u : 0u;
    atomic_store_explicit(&census->single_done, 1u, memory_order_release);
}

static void census_file_job(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    game_census_t *const census = borrowed_argument;
    census->file_status = (uint8_t)save_climate_write(census->borrowed_directory, census->seed, census_store(census),
                                                      census->borrowed_sorted, census->borrowed_scratch);
    atomic_store_explicit(&census->single_done, 1u, memory_order_release);
}

/* ---- The plan of a run --------------------------------------------------------------------------------------------- */

/* Tiles in batch order, then block order, then (tile_y, tile_x). */
static int32_t census_tile_compare(const climate_week_tile_t *a, const climate_week_tile_t *b) {
    const int32_t keys_a[6] = {census_floor_div(a->tile_y, (int32_t)GAME_CENSUS_BATCH_TILES),
                               census_floor_div(a->tile_x, (int32_t)GAME_CENSUS_BATCH_TILES),
                               census_floor_div(a->tile_y, (int32_t)CLIMATE_WEEK_BLOCK_SIDE),
                               census_floor_div(a->tile_x, (int32_t)CLIMATE_WEEK_BLOCK_SIDE),
                               a->tile_y,
                               a->tile_x};
    const int32_t keys_b[6] = {census_floor_div(b->tile_y, (int32_t)GAME_CENSUS_BATCH_TILES),
                               census_floor_div(b->tile_x, (int32_t)GAME_CENSUS_BATCH_TILES),
                               census_floor_div(b->tile_y, (int32_t)CLIMATE_WEEK_BLOCK_SIDE),
                               census_floor_div(b->tile_x, (int32_t)CLIMATE_WEEK_BLOCK_SIDE),
                               b->tile_y,
                               b->tile_x};
    for (uint32_t k = 0u; k < 6u; k++) {
        if (keys_a[k] != keys_b[k]) {
            return keys_a[k] < keys_b[k] ? -1 : 1;
        }
    }
    return 0;
}

static void census_sort(const climate_week_store_t *store, uint32_t *index, uint32_t *scratch, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        index[i] = i;
    }
    uint32_t *from = index, *to = scratch;
    for (uint32_t width = 1u; width < count; width *= 2u) {
        for (uint32_t left = 0u; left < count; left += 2u * width) {
            const uint32_t middle = left + width < count ? left + width : count;
            const uint32_t right = left + 2u * width < count ? left + 2u * width : count;
            uint32_t a = left, b = middle, o = left;
            while (a < middle && b < right) {
                to[o++] = census_tile_compare(&store->borrowed_tiles[from[b]], &store->borrowed_tiles[from[a]]) < 0
                              ? from[b++]
                              : from[a++];
            }
            while (a < middle) {
                to[o++] = from[a++];
            }
            while (b < right) {
                to[o++] = from[b++];
            }
        }
        uint32_t *const swap = from;
        from = to;
        to = swap;
    }
    if (from != index) {
        memcpy(index, from, (uint64_t)count * sizeof(uint32_t));
    }
}

static void census_plan(game_census_t *census) {
    climate_week_store_t *const store = census_store(census);
    const uint32_t count = store->tile_count;
    census_sort(store, census->borrowed_sorted, census->borrowed_scratch, count);
    census->job_count = 0u;
    census->batch_count = 0u;
    int32_t batch_x = 0, batch_y = 0, block_x = 0, block_y = 0;
    for (uint32_t s = 0u; s < count; s++) {
        const climate_week_tile_t *const tile = &store->borrowed_tiles[census->borrowed_sorted[s]];
        const int32_t bx = census_floor_div(tile->tile_x, (int32_t)GAME_CENSUS_BATCH_TILES);
        const int32_t by = census_floor_div(tile->tile_y, (int32_t)GAME_CENSUS_BATCH_TILES);
        const int32_t kx = census_floor_div(tile->tile_x, (int32_t)CLIMATE_WEEK_BLOCK_SIDE);
        const int32_t ky = census_floor_div(tile->tile_y, (int32_t)CLIMATE_WEEK_BLOCK_SIDE);
        const bool new_batch = s == 0u || bx != batch_x || by != batch_y;
        const bool new_block = new_batch || kx != block_x || ky != block_y;
        if (new_batch) {
            ASSERT_ALWAYS(census->batch_count < GAME_CENSUS_BATCHES_MAX);
            census->borrowed_batches[census->batch_count++] = census->job_count;
        }
        if (new_block) {
            game_census_job_t *const job = &census->borrowed_jobs[census->job_count++];
            memset(job, 0, sizeof(*job));
            job->borrowed_census = census;
        }
        climate_week_block_t *const block = &census->borrowed_jobs[census->job_count - 1u].block;
        block->slots[block->count++] = census->borrowed_sorted[s];
        batch_x = bx;
        batch_y = by;
        block_x = kx;
        block_y = ky;
    }
    census->borrowed_batches[census->batch_count] = census->job_count;
}

/* ---- The steps ----------------------------------------------------------------------------------------------------- */

static void census_week_setup(game_census_t *census) {
    census->batch = 0u;
    census->job_week = (uint32_t)census->week;
    census->job_fast = census->week <= census->fast_until ? 1u : 0u;
    census->job_events = census->week == census->last_week ? 1u : 0u;
    census->prepare_begun = 0u;
    census->stage = census->job_count > 0u ? GAME_CENSUS_PREPARE : GAME_CENSUS_FLOW;
    census->single_submitted = 0u;
    if (census->job_count == 0u) {
        atomic_store_explicit(&census->single_done, 1u, memory_order_relaxed); /* nothing to flow */
        census->single_submitted = 1u;
    }
}

static void census_begin(game_census_t *census) {
    climate_week_store_t *const store = census_store(census);
    store->busy = 1u;
    census->started_ns = time_now_ns();
    census->first_week = census->next_week;
    census->week = census->next_week;
    census_plan(census);
    ui_census_report_t *const work = census->borrowed_work;
    work->written = 0u;
    work->failed = 0u;
    work->weeks_done = 0u;
    work->weeks_total = (uint32_t)(census->last_week - census->first_week + 1u);
    census_week_setup(census);
}

static uint32_t census_batch_tiles(const game_census_t *census, uint32_t *out_done) {
    const uint32_t first = census->borrowed_batches[census->batch], end = census->borrowed_batches[census->batch + 1u];
    uint32_t tiles = 0u, done = 0u;
    for (uint32_t j = first; j < end; j++) {
        const game_census_job_t *const job = &census->borrowed_jobs[j];
        tiles += job->block.count;
        if (j < first + census->submitted && atomic_load_explicit(&job->done, memory_order_acquire) != 0u) {
            done += job->block.count;
        }
    }
    *out_done = done;
    return tiles;
}

static bool census_single(game_census_t *census, job_function_t function) {
    if (census->single_submitted == 0u) {
        atomic_store_explicit(&census->single_done, 0u, memory_order_relaxed);
        if (!job_try_submit(census->borrowed_job, &census->group, function, census)) {
            return false; /* the queue is full: next step */
        }
        census->single_submitted = 1u;
    }
    if (atomic_load_explicit(&census->single_done, memory_order_acquire) == 0u) {
        return false;
    }
    census->single_submitted = 0u;
    return true;
}

static void census_week_done(game_census_t *census) {
    census_store(census)->week = census->week;
    census->borrowed_work->weeks_done++;
    census->week++;
    if (census->week <= census->last_week) {
        census_week_setup(census);
        return;
    }
    census->stage = GAME_CENSUS_OUTLOOK;
    census->single_submitted = 0u;
}

static void census_finish(game_census_t *census) {
    climate_week_store_t *const store = census_store(census);
    ui_census_report_t *const work = census->borrowed_work;
    const bool wrote = census->file_status == FILE_OK;
    work->written = (uint8_t)(wrote ? 1u : 0u);
    work->failed = (uint8_t)(wrote ? 0u : 1u);
    work->failed_status = census->file_status;
    work->tiles_done = work->tiles_total;
    store->busy = 0u;
    store->pending_lost = 0u;
    climate_week_store_pending_apply(store);
    census->runs++;
    census->stage = GAME_CENSUS_IDLE;
    census->next_week = census->last_week >= census->week ? census->week : 0u;
    LOG_INFO("census: weeks %" PRIu64 " to %" PRIu64 " worked out over %" PRIu32 " tiles in %.0f ms%s",
             census->first_week, census->week - 1u, store->tile_count,
             (libc_double_t)time_ns_to_ms(time_now_ns() - census->started_ns),
             wrote ? "" : ", and the file could not be written");
    census_publish(census);
}

void game_census_step(game_census_t *census, float64_t player_x, float64_t player_y, uint32_t radius_blocks) {
    if (census->borrowed_job == nullptr) {
        return;
    }
    if (census->stage == GAME_CENSUS_IDLE) {
        if (census->next_week == 0u) {
            return;
        }
        census_begin(census);
    }
    climate_week_store_t *const store = census_store(census);
    ui_census_report_t *const work = census->borrowed_work;
    if (census->stage == GAME_CENSUS_PREPARE) {
        const uint32_t first = census->borrowed_batches[census->batch],
                       end = census->borrowed_batches[census->batch + 1u];
        if (census->prepare_begun == 0u) {
            int32_t x_min = INT32_MAX, y_min = INT32_MAX, x_max = INT32_MIN, y_max = INT32_MIN;
            for (uint32_t j = first; j < end; j++) {
                const climate_week_block_t *const block = &census->borrowed_jobs[j].block;
                for (uint32_t k = 0u; k < block->count; k++) {
                    const climate_week_tile_t *const tile = &store->borrowed_tiles[block->slots[k]];
                    x_min = tile->tile_x < x_min ? tile->tile_x : x_min;
                    y_min = tile->tile_y < y_min ? tile->tile_y : y_min;
                    x_max = tile->tile_x > x_max ? tile->tile_x : x_max;
                    y_max = tile->tile_y > y_max ? tile->tile_y : y_max;
                }
            }
            const int32_t margin = (int32_t)GAME_CENSUS_PREPARE_TILES;
            climate_prepare_begin(census->borrowed_climate, census->borrowed_job,
                                  (x_min - margin) * CLIMATE_TILE_BLOCKS, (y_min - margin) * CLIMATE_TILE_BLOCKS,
                                  (x_max + margin + 1) * CLIMATE_TILE_BLOCKS - 1,
                                  (y_max + margin + 1) * CLIMATE_TILE_BLOCKS - 1);
            census->prepare_begun = 1u;
        }
        uint32_t done = 0u, total = 0u;
        if (!climate_prepare_step(census->borrowed_climate, census->borrowed_job, &done, &total)) {
            return;
        }
        census->stage = GAME_CENSUS_BLOCKS;
        census->submitted = 0u;
    }
    if (census->stage == GAME_CENSUS_BLOCKS) {
        const uint32_t first = census->borrowed_batches[census->batch],
                       end = census->borrowed_batches[census->batch + 1u];
        while (first + census->submitted < end) {
            game_census_job_t *const job = &census->borrowed_jobs[first + census->submitted];
            job->block.incomplete = 0u;
            job->block.overflowed = 0u;
            job->block.failed = 0u;
            atomic_store_explicit(&job->done, 0u, memory_order_relaxed);
            if (!job_try_submit(census->borrowed_job, &census->group, census_block_job, job)) {
                break; /* the queue is full: next step */
            }
            census->submitted++;
        }
        uint32_t tiles_done = 0u;
        work->tiles_total = census_batch_tiles(census, &tiles_done);
        work->tiles_done = tiles_done;
        census_publish(census);
        if (first + census->submitted < end || tiles_done < work->tiles_total) {
            return;
        }
        uint32_t incomplete = 0u, overflowed = 0u, failed = 0u;
        for (uint32_t j = first; j < end; j++) {
            incomplete += census->borrowed_jobs[j].block.incomplete;
            overflowed += census->borrowed_jobs[j].block.overflowed;
            failed += census->borrowed_jobs[j].block.failed;
        }
        if (incomplete + overflowed + failed > 0u) {
            LOG_ERROR("census: week %" PRIu64 ": %" PRIu32 " days with cells missing, %" PRIu32 " lists full, %" PRIu32
                      " reads of tiles not built",
                      census->week, incomplete, overflowed, failed);
        }
        if (census->batch + 1u < census->batch_count) {
            census->batch++;
            census->prepare_begun = 0u;
            census->stage = GAME_CENSUS_PREPARE;
            return;
        }
        if (census->job_fast != 0u) {
            census_week_done(census);
            census_publish(census);
            return;
        }
        census->stage = GAME_CENSUS_FLOW;
        census->single_submitted = 0u;
    }
    if (census->stage == GAME_CENSUS_FLOW) {
        if (!census_single(census, census_flow_job)) {
            return;
        }
        census_week_done(census);
        census_publish(census);
        return;
    }
    if (census->stage == GAME_CENSUS_OUTLOOK) {
        if (census->single_submitted == 0u) {
            census->player_x = player_x;
            census->player_y = player_y;
            census->radius_blocks = radius_blocks;
        }
        if (!census_single(census, census_outlook_job)) {
            return;
        }
        census->stage = GAME_CENSUS_WRITE;
    }
    if (census->stage == GAME_CENSUS_WRITE) {
        if (census->borrowed_directory == nullptr) {
            census->file_status = FILE_OK;
        } else if (!census_single(census, census_file_job)) {
            return;
        }
        census_finish(census);
    }
}

/* ---- Wiring into a game -------------------------------------------------------------------------------------------- */

game_census_t *game_census_of(const game_t *game) {
    return game->census_week == game_census_week ? game->borrowed_census_context : nullptr;
}

void game_census_attach(game_t *game) {
    climate_t *const climate = game->borrowed_climate;
    if (game->climate_jump_days != 0u) {
        const uint64_t days = game->climate_jump_days;
        const calendar_clock_t clock = atomic_load_explicit(&game->clock_units, memory_order_relaxed);
        game_clock_set(game, (clock >> 32) + days, (float64_t)(uint32_t)clock / 4294967296.0f64);
        LOG_INFO("census: --climate-jump moves this world's clock on %" PRIu64 " days, as playing them would", days);
        game->climate_jump_days = 0u;
    }
    if (climate == nullptr || !climate_is_open(climate) || game->census_week != nullptr) {
        return;
    }
    game_census_t *const census = arena_alloc(&game->arena_world, sizeof(game_census_t), alignof(game_census_t));
    save_t *const save = game->borrowed_save;
    const char *const directory = save != nullptr && save->world.open != 0u ? save->state.borrowed_directory : nullptr;
    game_census_open(census, &game->arena_world, climate, game->borrowed_job, directory, climate_seed(climate),
                     game->last_census_week);
    game->census_week = game_census_week;
    game->borrowed_census_context = census;
}

void game_census_detach(game_t *game) {
    game_census_t *const census = game_census_of(game);
    if (census == nullptr) {
        return;
    }
    game_census_close(census);
    game->census_week = nullptr;
    game->borrowed_census_context = nullptr;
}

bool game_census_sim(game_t *game) {
    game_census_t *const census = game_census_of(game);
    if (census == nullptr) {
        return false;
    }
    float64_t x = 0.0f64, y = 0.0f64;
    if (game->borrowed_player != nullptr) {
        x = game->borrowed_player->movement.position[0];
        y = game->borrowed_player->movement.position[1];
    } else {
        const world_map_t *const map = &game->borrowed_world->map;
        x = (float64_t)map->center_x * (float64_t)COLUMN_SIZE + 0.5f64 * (float64_t)COLUMN_SIZE;
        y = (float64_t)map->center_y * (float64_t)COLUMN_SIZE + 0.5f64 * (float64_t)COLUMN_SIZE;
    }
    game_census_step(census, x, y, game->config.render_distance * (uint32_t)COLUMN_SIZE);
    return game_census_quiet(census);
}

bool game_census_settled(const game_t *game) {
    const game_census_t *const census = game_census_of(game);
    return census == nullptr || game_census_idle(census);
}

void game_census_mark(game_t *game, int32_t chunk_x, int32_t chunk_y) {
    if (game_census_of(game) == nullptr) {
        return;
    }
    climate_week_store_mark(climate_week_store(game->borrowed_climate), chunk_x * (int32_t)COLUMN_SIZE,
                            chunk_y * (int32_t)COLUMN_SIZE);
}
