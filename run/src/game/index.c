/* See README.md. The Owner of the World, Tick and Frame arenas, and the loop connecting the sim, gpu and main threads. */
#include "game/index.h"

#include "assert/index.h"
#include "climate/sun/index.h"
#include "entity/animation/index.h"
#include "game/atlas/index.h"
#include "game/census/index.h"
#include "game/corner/index.h"
#include "game/cover/index.h"
#include "game/fill/index.h"
#include "game/weather/index.h"
#include "item/stack/index.h"
#include "game/spawn/index.h"
#include "game/view/index.h"
#include "game/watch/index.h"
#include "log/index.h"
#include "render/entities/index.h"
#include "tables/blocks/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* What a world keeps in the World arena with no room for its pointer in game_t, which is at its size (app_t is on main's stack):
 * the column jobs first, so borrowed_jobs is the whole of it, and plan 004 phase 08's weather and cover beside them, and phase
 * 10's HUD corner words on a line of their own. */
typedef struct game_world_parts_t {
    game_column_job_t jobs[GAME_STREAM_JOBS]; /* 0: borrowed_jobs points here */
    game_weather_t weather;                   /* 8192 */
    game_cover_t cover;                       /* 58816 */
    uint8_t _pad0[56];                        /* 58888 */
    game_corner_t corner;                     /* 58944: the sim writes it, main reads its words */
    game_fill_t fill;                         /* 59136: whichever thread uploads writes it (plan 005 phase 14) */
    game_watch_t watch;                       /* 60928: plan 005 phase 7's overlay numbers and slow lines */
} game_world_parts_t;
static_assert(sizeof(game_world_parts_t) == 61120);
static_assert(offsetof(game_world_parts_t, fill) == 59136);
static_assert(offsetof(game_world_parts_t, jobs) == 0);
static_assert(offsetof(game_world_parts_t, weather) == 8192);
static_assert(offsetof(game_world_parts_t, cover) == 58816);
static_assert(offsetof(game_world_parts_t, corner) == 58944);
static_assert(offsetof(game_world_parts_t, watch) == 60928);

enum : uint64_t {
    /* Budgets of the two arenas with a lifetime shorter than a world's. */
    GAME_TICK_ARENA_BYTES = 64ull << 20,
    GAME_FRAME_ARENA_BYTES = 64ull << 20,
    /* A whole column of states, section after section, each in section_voxel_index order. */
    GAME_COLUMN_VOXELS = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS,
    GAME_COLUMN_BYTES = GAME_COLUMN_VOXELS * sizeof(uint16_t),
    GAME_WORLD_ARENA_BYTES =
        GAME_STREAM_JOBS * (GAME_COLUMN_BYTES + GAME_COLUMN_VOXELS + UI_ATLAS_COLUMN_PIXELS * sizeof(uint32_t)) +
        sizeof(game_world_parts_t) + 2ull * 4096ull + MOB_SPAWN_BYTES + GAME_CENSUS_ARENA_BYTES,
};
enum : int64_t {
    GAME_SIM_IDLE_NS = 1000000, /* the sim looks for finished columns this often when it has nothing to do */
    GAME_GPU_IDLE_NS = 100000,  /* the gpu thread waits this long for main to take the picture it just made */
    GAME_GPU_STUCK_NS =
        10000000000, /* no round of its loop for this long: nothing it does takes a hundredth of that, and a card that hangs may say nothing */
    GAME_GPU_WATCH_GAP_NS = 1000000000, /* main itself was away this long, on another VT or stopped */
    GAME_COMMIT_BUDGET_NS =
        4000000, /* the most a sim step spends on finished columns, each with its light, past the first */
    GAME_TICK_NS = 1000000000 / MC_GAME_TICK_HZ,
};
enum : uint32_t {
    GAME_CLIMATE_AHEAD_BLOCKS = 2048u, /* climate tiles are kept this far past the render distance: plan 004, 6.9 */
    /* With a far field, what is edited is captured for the save this often, in ticks, so the far field draws it within a second: plan
     * 005 phase 14. The far field reads only what is captured, and the autosave writes what is captured, as it always has. */
    GAME_FAR_CAPTURE_TICKS = MC_GAME_TICK_HZ,
};

#define GAME_START_EYE_HEIGHT 24.5f64
/* What the debug overlay's frames per second are counted over. */
#define GAME_FPS_WINDOW_SECONDS 0.5f64
#define GAME_START_YAW 0.6f32
#define GAME_START_PITCH (-0.3f32)

static_assert(MC_GAME_RENDER_DISTANCE_MAX < WORLD_MAP_RADIUS_MAX);

void game_init(game_t *game, world_t *world, terrain_t *terrain, render_t *render, job_system_t *job,
               const texture_t *texture, const thread_topology_t *topology) {
    memset(game, 0, sizeof(*game));
    game->borrowed_world = world;
    game->borrowed_terrain = terrain;
    game->borrowed_render = render;
    game->borrowed_job = job;
    game->borrowed_texture = texture;
    game->borrowed_topology = topology;
}

void game_player_set(game_t *game, player_t *player) {
    ASSERT_ALWAYS(game->open == 0u);
    game->borrowed_player = player;
}

void game_entity_set(game_t *game, entity_t *entity) {
    ASSERT_ALWAYS(game->open == 0u);
    game->borrowed_entity = entity;
}

void game_ui_set(game_t *game, ui_t *ui) {
    ASSERT_ALWAYS(game->open == 0u);
    game->borrowed_ui = ui;
}

void game_audio_set(game_t *game, audio_t *audio) {
    ASSERT_ALWAYS(game->open == 0u);
    game->borrowed_audio = audio;
}

void game_climate_set(game_t *game, climate_t *climate) {
    ASSERT_ALWAYS(game->open == 0u);
    game->borrowed_climate = climate;
}

/* The climate, if this world has one open. */
game_weather_t *game_weather_of(const game_t *game) {
    return game->borrowed_jobs != nullptr ? &((game_world_parts_t *)(void *)game->borrowed_jobs)->weather : nullptr;
}

game_cover_t *game_cover_of(const game_t *game) {
    return game->borrowed_jobs != nullptr ? &((game_world_parts_t *)(void *)game->borrowed_jobs)->cover : nullptr;
}

game_corner_t *game_corner_of(const game_t *game) {
    return game->borrowed_jobs != nullptr ? &((game_world_parts_t *)(void *)game->borrowed_jobs)->corner : nullptr;
}

game_fill_t *game_fill_of(const game_t *game) {
    return game->borrowed_jobs != nullptr ? &((game_world_parts_t *)(void *)game->borrowed_jobs)->fill : nullptr;
}

game_watch_t *game_watch_of(const game_t *game) {
    return game->borrowed_jobs != nullptr ? &((game_world_parts_t *)(void *)game->borrowed_jobs)->watch : nullptr;
}

static climate_t *game_climate(const game_t *game) {
    climate_t *const climate = game->borrowed_climate;
    return climate != nullptr && climate_is_open(climate) ? climate : nullptr;
}

/* Tiles kept round the player: render distance plus 2 km (6.9), or a thunder cell's reach and square (phase 10, 14). */
static uint32_t game_climate_radius(const world_map_t *map) {
    const float64_t reach = climate_systems_reach(CLIMATE_SYSTEM_THUNDER) + CLIMATE_SQUARE_THUNDER; /* 5224 */
    return map->view_radius * (uint32_t)COLUMN_SIZE + (uint32_t)ceil(fmax(reach, (float64_t)GAME_CLIMATE_AHEAD_BLOCKS));
}

void game_save_set(game_t *game, save_t *save) {
    ASSERT_ALWAYS(game->open == 0u);
    game->borrowed_save = save;
}

void game_mob_set(game_t *game, mob_t *mob) {
    ASSERT_ALWAYS(game->open == 0u);
    game->borrowed_mob = mob;
}

void game_item_set(game_t *game, item_t *item) {
    ASSERT_ALWAYS(game->open == 0u);
    game->borrowed_item = item;
}

void game_census_screens_set(game_t *game, bool screens) {
    ASSERT_ALWAYS(game->open == 0u);
    game->census_screens = screens ? 1u : 0u;
}

void game_census_hook_set(game_t *game, game_census_week_fn *week, void *context) {
    ASSERT_ALWAYS(game->open == 0u);
    game->census_week = week;
    game->borrowed_census_context = context;
}

void game_climate_jump_set(game_t *game, uint16_t days) {
    ASSERT_ALWAYS(game->open == 0u);
    game->climate_jump_days = days;
}

void game_profiler_set(game_t *game, profiler_t *profiler) {
    ASSERT_ALWAYS(game->open == 0u);
    game->borrowed_lane_main = profiler_lane_claim(profiler, "main");
    game->borrowed_lane_sim_thread = profiler_lane_claim(profiler, "sim");
    game->borrowed_lane_gpu_thread = profiler_lane_claim(profiler, "gpu");
    game->borrowed_lane_card = profiler_lane_claim(profiler, "card");
    /* Until there are threads, main does what they do. */
    game->borrowed_lane_sim = game->borrowed_lane_main;
    game->borrowed_lane_gpu = game->borrowed_lane_main;
}

/* What a save is of, in this game. */
static void game_save_parts(game_t *game, save_state_parts_t *out) {
    player_t *const player = game->borrowed_player;
    const uint64_t look = player != nullptr ? atomic_load_explicit(&player->look_word, memory_order_relaxed) : 0u;
    const uint32_t yaw_bits = (uint32_t)look, pitch_bits = (uint32_t)(look >> 32u);
    *out = (save_state_parts_t){
        .borrowed_player = player,
        .borrowed_item = game->borrowed_item,
        .borrowed_entity = game->borrowed_entity,
        .borrowed_mob = game->borrowed_mob,
        .borrowed_spawn = game->borrowed_entity != nullptr ? &game->mob_spawn : nullptr,
        .borrowed_held = game->borrowed_ui != nullptr ? &game->borrowed_ui->borrowed_screens->cursor : nullptr,
        .time = {.world_ticks = game->tick_count,
                 .day_units = atomic_load_explicit(&game->day_units, memory_order_relaxed),
                 .yaw = 0.0f32,
                 .pitch = 0.0f32,
                 ._pad0 = {0}},
        /* The days from the clock main last published, as day_units is: a save on the sim thread never reads main's own. */
        .calendar = {.clock_days = atomic_load_explicit(&game->clock_units, memory_order_relaxed) >> 32,
                     .last_census_week = game->last_census_week,
                     .day_zero = game->day_zero,
                     ._pad0 = {0}},
    };
    memcpy(&out->time.yaw, &yaw_bits, sizeof(yaw_bits));
    memcpy(&out->time.pitch, &pitch_bits, sizeof(pitch_bits));
}

/* A chest or furnace an edit has taken since item_world_tick last looked has its items spilled now, and not in the next tick, which comes too late for what
 * is about to be saved. */
static void game_save_items_settle(game_t *game) {
    if (game->borrowed_item != nullptr) {
        item_world_edits_read(game->borrowed_item, &game->borrowed_world->edit, game_spill, game);
    }
}

save_world_status_t game_save_finish(game_t *game) {
    save_t *const save = game->borrowed_save;
    if (save == nullptr || save->world.open == 0u) {
        return SAVE_WORLD_OK;
    }
    game_save_items_settle(game);
    save_state_parts_t parts;
    game_save_parts(game, &parts);
    return save_autosave_finish(&save->autosave, &game->borrowed_world->edit, &parts);
}

void game_deinit(game_t *game) {
    if (game->threads_running != 0u) {
        (void)game_loop_stop(game); /* it has said what went wrong, and there is no one left to tell */
    }
    game_close(game);
}

/* ---- When the card goes ------------------------------------------------------------------------------------------- */

void game_gpu_failed_note(game_t *game, gpu_status_t status) {
    atomic_store_explicit(&game->gpu_failure, (uint32_t)status, memory_order_relaxed);
    if (status == GPU_STATUS_DEVICE_LOST) {
        device_memory_lost_set(game->borrowed_render->borrowed_memory);
    }
}

bool game_gpu_alive(game_t *game) {
    if (game->threads_running == 0u || game->gpu_stuck != 0u ||
        atomic_load_explicit(&game->failed, memory_order_acquire) != 0u) {
        return game->gpu_stuck == 0u; /* a thread that failed has stopped going round, and has said why */
    }
    const int64_t now_ns = time_now_ns();
    const int64_t gap_ns = now_ns - game->gpu_watched_ns;
    game->gpu_watched_ns = now_ns;
    const uint64_t beats = atomic_load_explicit(&game->gpu_beats, memory_order_relaxed);
    /* If this thread has been away itself, on another VT or stopped, what it did not see proves nothing. */
    if (beats != game->gpu_beats_seen || gap_ns > GAME_GPU_WATCH_GAP_NS) {
        game->gpu_beats_seen = beats;
        game->gpu_beats_seen_ns = now_ns;
        return true;
    }
    if (now_ns - game->gpu_beats_seen_ns <= GAME_GPU_STUCK_NS) {
        return true;
    }
    LOG_ERROR("gpu: the card has not answered for %.1f s, inside a call that has not come back",
              (libc_double_t)time_ns_to_seconds(now_ns - game->gpu_beats_seen_ns));
    game->gpu_stuck = 1u;
    game_gpu_failed_note(game, GPU_STATUS_DEVICE_LOST);
    atomic_store_explicit(&game->failed, 1u, memory_order_release);
    return false;
}

bool game_gpu_lost(const game_t *game) {
    const render_t *const render = game->borrowed_render;
    if (render != nullptr && render->borrowed_memory != nullptr && render->borrowed_memory->lost != 0u) {
        return true;
    }
    switch ((gpu_status_t)atomic_load_explicit(&game->gpu_failure, memory_order_relaxed)) {
    case GPU_STATUS_DEVICE_LOST:
    case GPU_STATUS_NO_DEVICE:
    case GPU_STATUS_DRIVER:
    case GPU_STATUS_CUDA_OTHER:
        return true;
    default:
        return false;
    }
}

bool game_gpu_stuck(const game_t *game) {
    return game->gpu_stuck != 0u;
}

void game_gpu_pretend_set(game_t *game, uint32_t pictures, bool stuck) {
    game->gpu_pretend_pictures = pictures;
    game->gpu_pretend_stuck = stuck ? 1u : 0u;
}

/* What --pretend-gpu-lost asked of this picture. The card's own answer, so everything after this is as it would be. */
static gpu_status_t game_gpu_pretend_step(game_t *game) {
    if (game->gpu_pretend_pictures == 0u) {
        return GPU_STATUS_OK;
    }
    game->gpu_pretend_pictures--;
    if (game->gpu_pretend_pictures != 0u) {
        return GPU_STATUS_OK;
    }
    while (game->gpu_pretend_stuck != 0u) {
        time_sleep_ns(GAME_GPU_IDLE_NS); /* as a call into a driver that has gone: nothing brings it back */
    }
    render_t *const render = game->borrowed_render;
    const gpu_status_t status = device_memory_fault_now(render->borrowed_memory);
    render->frame.gpu.last_error = render->borrowed_memory->last_error;
    return status;
}

/* ---- Opening and closing a world ---------------------------------------------------------------------------------- */

static void game_arenas_start(game_t *game) {
    uint8_t *const base = game->owned_region.owned_base;
    arena_init(&game->arena_world, "game_world", base, GAME_WORLD_ARENA_BYTES);
    arena_init(&game->arena_tick, "game_tick", base + GAME_WORLD_ARENA_BYTES, GAME_TICK_ARENA_BYTES);
    arena_init(&game->arena_frame, "game_frame", base + GAME_WORLD_ARENA_BYTES + GAME_TICK_ARENA_BYTES,
               GAME_FRAME_ARENA_BYTES);
}

static void game_arenas_stop(game_t *game) {
    arena_deinit(&game->arena_frame);
    arena_deinit(&game->arena_tick);
    arena_deinit(&game->arena_world);
}

/* A picture at the size it is shown, for one drawn smaller, or one kept while the pictures change. */
static bool game_picture_full_room(game_t *game) {
    const uint64_t bytes = (uint64_t)game->config.width * game->config.height * sizeof(uint32_t);
    return game->owned_view.bytes >= bytes || heap_reserve(bytes, &game->owned_view);
}

/* The far field, which shows what a save holds over what generates. */
static gpu_status_t game_lod_open(game_t *game, const render_lod_plan_t *plan) {
    render_t *const render = game->borrowed_render;
    const gpu_status_t status = render_open_lod(render, plan, render_lod_sample_terrain, game->borrowed_terrain);
    save_t *const save = game->borrowed_save;
    if (status == GPU_STATUS_OK && save != nullptr && save->world.open != 0u) {
        render_lod_overlay_set(render->borrowed_lod, save_world_far_line, &save->world, SAVE_WORLD_FAR_SCRATCH_BYTES);
    }
    return status;
}

void game_far_capture_tick(game_t *game) {
    save_t *const save = game->borrowed_save;
    if (save == nullptr || save->world.open == 0u || game->view.lod.level_count == 0u ||
        game->tick_count % GAME_FAR_CAPTURE_TICKS != 0u) {
        return;
    }
    /* What does not fit stays dirty, for the autosave, which says so. */
    (void)save_world_capture_dirty(&save->world, &game->borrowed_world->edit);
}

/* The same, keeping what the far field has of each level that stays the same. */
static gpu_status_t game_lod_replan(game_t *game, const render_lod_plan_t *plan) {
    render_t *const render = game->borrowed_render;
    const gpu_status_t status = render_replan_lod(render, plan, render_lod_sample_terrain, game->borrowed_terrain);
    save_t *const save = game->borrowed_save;
    if (status == GPU_STATUS_OK && render->borrowed_lod != nullptr && save != nullptr && save->world.open != 0u) {
        render_lod_overlay_set(render->borrowed_lod, save_world_far_line, &save->world, SAVE_WORLD_FAR_SCRATCH_BYTES);
    }
    return status;
}

void game_far_changes_take(game_t *game) {
    render_lod_t *const lod = game->borrowed_render->borrowed_lod;
    save_t *const save = game->borrowed_save;
    if (lod == nullptr || save == nullptr || save->world.open == 0u) {
        return;
    }
    int32_t chunk_x = 0, chunk_y = 0;
    while (save_world_far_next(&save->world, &chunk_x, &chunk_y)) {
        render_lod_column_changed(lod, chunk_x, chunk_y);
    }
    if (save_world_far_lost_take(&save->world)) {
        /* Only a column in the map is ever captured, so what was lost is in the map's square: not the whole far field. */
        const world_map_t *const map = &game->borrowed_world->map;
        const int32_t radius = (int32_t)map->table_radius;
        render_lod_area_changed(lod, map->center_x - radius, map->center_y - radius, map->center_x + radius,
                                map->center_y + radius);
    }
}

/* The map, and what the card holds of it. Each says why if it cannot, and whatever came before it is given back. */
static bool game_open_parts(game_t *game, const game_config_t *config, int32_t chunk_x, int32_t chunk_y) {
    world_t *const world = game->borrowed_world;
    render_t *const render = game->borrowed_render;
    /* What the map loads is what is drawn block by block. The far field never touches it. */
    const uint32_t table_chunks = game_view_table_chunks(config);
    game_view_work_out(config, table_chunks, &game->view);
    const game_view_t *const view = &game->view;
    if (!world_open(world, table_chunks + WORLD_MAP_KEEP_EXTRA, chunk_x, chunk_y)) {
        return false;
    }
    world_map_set_view_radius(&world->map, view->full_detail);
    world_tick_seed_set(&world->tick, game->borrowed_terrain->seed);
    const render_upload_budget_t budget = {
        .bytes = RENDER_UPLOAD_BUDGET_BYTES_DEFAULT,
        .sections = RENDER_UPLOAD_BUDGET_SECTIONS_DEFAULT,
        ._pad0 = {0},
    };
    game_open_alive_tell(game);
    if (render_open(render, &world->snapshot, render_pool_bytes_for(render, world->map.slot_count), budget) !=
        GPU_STATUS_OK) {
        world_close(world);
        return false;
    }
    game_open_alive_tell(game);
    if (render_open_frame(render, game->borrowed_texture, view->width, view->height, view->fog_end) != GPU_STATUS_OK) {
        render_close(render);
        world_close(world);
        return false;
    }
    game_open_alive_tell(game);
    if (view->lod.level_count != 0u) {
        const gpu_status_t status = game_lod_open(game, &view->lod);
        if (status != GPU_STATUS_OK) {
            render_close(render);
            world_close(world);
            return false;
        }
    }
    render_frame_features_set(&render->frame, view->features | SHADE_FEATURE_SUN_APART);
    if ((view->width != config->width || view->height != config->height) && !game_picture_full_room(game)) {
        render_close(render);
        world_close(world);
        return false; /* it has said why */
    }
    return true;
}

/* A day's fraction as main hands it to the gpu thread, and back. */
static uint32_t game_day_units_of(float64_t day_fraction) {
    return (uint32_t)(day_fraction * 4294967296.0f64);
}

void game_day_set(game_t *game, float64_t day_fraction) {
    game->day_fraction = day_fraction - floor(day_fraction);
    const uint32_t units = game_day_units_of(game->day_fraction);
    atomic_store_explicit(&game->day_units, units, memory_order_relaxed);
    atomic_store_explicit(&game->clock_units, game->clock_days << 32 | units, memory_order_relaxed);
}

void game_clock_set(game_t *game, uint64_t clock_days, float64_t day_fraction) {
    ASSERT_ALWAYS(clock_days < CALENDAR_CLOCK_DAY);
    /* A fraction of 1 or more carries into the days, as the clock moving on does. */
    const float64_t whole = floor(day_fraction);
    ASSERT_ALWAYS(whole >= 0.0f64);
    game->clock_days = clock_days + (uint64_t)whole;
    game_day_set(game, day_fraction - whole);
}

/* Main's: the world clock moves on by what it is told has passed, keeping the whole days, which the date is made of. Standing
 * still while paused, as a census pauses the world before main has seen it. */
static void game_day_advance(game_t *game, float32_t seconds) {
    if (atomic_exchange_explicit(&game->morning_latch, 0u, memory_order_relaxed) != 0u) {
        /* To the next 06:00: from 23:00 a day on, from 01:00 the same day. At 06:00 itself a whole day, as plan 004 phase 02's
         * polar night can be slept through there. */
        if (game->day_fraction >= PLAYER_SURVIVAL_MORNING) {
            game->clock_days++;
        }
        game_day_set(game, PLAYER_SURVIVAL_MORNING);
    }
    if (atomic_load_explicit(&game->paused, memory_order_relaxed) != 0u) {
        return;
    }
    if ((game->borrowed_render->frame.scene.features & SHADE_FEATURE_DAY_CYCLE) != 0u) {
        const float64_t moved = game->day_fraction + (float64_t)seconds / GAME_DAY_SECONDS;
        const float64_t whole = floor(moved);
        game->clock_days += (uint64_t)whole;
        game_day_set(game, moved - whole);
    }
}

/* On whichever thread draws: the sun of the clock main last said, at the latitude of the eye this picture is drawn from (plan 004
 * phase 02). The whole view takes the eye's own latitude. */
static void game_day_apply(game_t *game, const render_camera_t *camera) {
    climate_sun_t sun;
    climate_sun_of_clock(game->day_zero, atomic_load_explicit(&game->clock_units, memory_order_relaxed),
                         camera->position[1], &sun);
    const float32_t direction[3] = {(float32_t)sun.direction[0], (float32_t)sun.direction[1],
                                    (float32_t)sun.direction[2]};
    render_frame_sun_set(&game->borrowed_render->frame, direction);
}

/* All of a column's storage, at a place: terrain_generate_box's order is section_voxel_index's. */
static terrain_density_box_t game_column_box(int32_t x, int32_t y, uint32_t across) {
    return (terrain_density_box_t){
        .origin_x = x,
        .origin_y = y,
        .origin_z = COLUMN_Z_STORAGE_MIN,
        .count_x = across,
        .count_y = across,
        .count_z = (uint32_t)(COLUMN_Z_STORAGE_END - COLUMN_Z_STORAGE_MIN),
    };
}

/* A new world's HUD: nothing carried, and nothing timed yet. The overlay stays as the player left it. */
static void game_hud_start(game_t *game) {
    for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
        atomic_store_explicit(&game->hud_hotbar[slot], 0u, memory_order_relaxed);
    }
    atomic_store_explicit(&game->hud_selected, 0u, memory_order_relaxed);
    game_hud_position_publish(game);
    atomic_store_explicit(&game->hud_tick_us, 0u, memory_order_relaxed);
    for (uint32_t pass = 0u; pass < FRAME_PASS_COUNT; pass++) {
        atomic_store_explicit(&game->hud_pass_us[pass], 0u, memory_order_relaxed);
    }
    atomic_store_explicit(&game->hud_total_us, 0u, memory_order_relaxed);
    game->hud_fps_seconds = 0.0f64;
    game->hud_fps = 0.0f32;
    game->hud_fps_pictures = 0u;
    game->hud_compose_us = 0u;
    game->hud_present_us = 0u;
}

void game_open_alive_set(game_t *game, void (*alive)(void *context), void *context) {
    game->open_alive = alive;
    game->borrowed_open_alive_context = context;
}

void game_open_alive_tell(const game_t *game) {
    if (game->open_alive != nullptr) {
        game->open_alive(game->borrowed_open_alive_context);
    }
}

/* The climate's tiles round where the eye starts, before any column is asked for, so the first ones are not held back
 * by the gate: plan 004 phase 03, "Precompute at world creation". On every worker; game_open_climate_step takes it on, so
 * a loading screen can be drawn meanwhile. */
/* A save made while zombies, skeletons and creepers still came keeps them: they go as the world opens, each kind with a line in the log
 * (plan 005 phase 3). Any mob whose spawn.weight is 0 goes the same way. */
static void game_open_retire_mobs(game_t *game) {
    if (game->borrowed_entity == nullptr) {
        return;
    }
    uint32_t removed[MOB_COUNT] = {0};
    if (mob_spawn_retire(&game->borrowed_entity->store, removed) == 0u) {
        return;
    }
    for (uint32_t i = 1u; i < MOB_COUNT; i++) {
        if (removed[i] != 0u) {
            LOG_INFO("game: took %" PRIu32 " of mob %s out of the saved world, as it is turned off", removed[i],
                     mob_defs[i].name);
        }
    }
}

static void game_open_climate(game_t *game, int32_t start_x, int32_t start_y) {
    climate_t *const climate = game_climate(game);
    game->open_percent = 0u;
    game->open_climate_frames = 0u;
    if (climate == nullptr) {
        return;
    }
    const int32_t reach = (int32_t)game_climate_radius(&game->borrowed_world->map);
    climate_prepare_begin(climate, game->borrowed_job, start_x - reach, start_y - reach, start_x + reach,
                          start_y + reach);
    LOG_INFO("game: working out the climate within %d blocks of %d, %d", reach, start_x, start_y);
}

bool game_open_climate_step(game_t *game) {
    climate_t *const climate = game_climate(game);
    uint32_t done = 0u, total = 0u;
    if (climate == nullptr || climate_prepare_step(climate, game->borrowed_job, &done, &total)) {
        if (game->open_percent != 100u) {
            /* The first picture has the HUD's climate words (plan 004 phase 10): its tiles are built now, and no thread runs. */
            game_hud_climate_publish(game, true);
        }
        game->open_percent = 100u;
        return true;
    }
    game->open_percent = (uint8_t)(total == 0u ? 0u : (uint64_t)done * 100u / total);
    game->open_climate_frames++;
    return false;
}

bool game_open(game_t *game, const game_config_t *config) {
    ASSERT_ALWAYS(game->open == 0u);
    if (config->render_distance == 0u || config->render_distance > MC_GAME_RENDER_DISTANCE_MAX) {
        LOG_ERROR("game: a render distance of %" PRIu32 " is not from 1 to %" PRIu32, config->render_distance,
                  (uint32_t)MC_GAME_RENDER_DISTANCE_MAX);
        return false;
    }
    if (!heap_reserve(GAME_WORLD_ARENA_BYTES + GAME_TICK_ARENA_BYTES + GAME_FRAME_ARENA_BYTES, &game->owned_region)) {
        return false; /* it has said why */
    }
    game_arenas_start(game);
    job_group_init(&game->group);
    game_world_parts_t *const world_parts =
        arena_alloc(&game->arena_world, sizeof(game_world_parts_t), alignof(game_world_parts_t));
    game->borrowed_jobs = world_parts->jobs;
    game_weather_start(&world_parts->weather, GAME_WEATHER_SAMPLED);
    memset(&world_parts->cover, 0, sizeof(world_parts->cover));
    game_corner_start(&world_parts->corner);
    game_fill_reset(&world_parts->fill);
    game_watch_start(&world_parts->watch);
    const terrain_t *const terrain = game->borrowed_terrain;
    for (uint32_t i = 0u; i < GAME_STREAM_JOBS; i++) {
        game_column_job_t *const job = &game->borrowed_jobs[i];
        memset(job, 0, sizeof(*job));
        job->borrowed_terrain = terrain;
        job->borrowed_save = game->borrowed_save != nullptr ? &game->borrowed_save->world : nullptr;
        job->borrowed_states = arena_alloc(&game->arena_world, GAME_COLUMN_BYTES, alignof(uint64_t));
        job->borrowed_opened = arena_alloc(&game->arena_world, GAME_COLUMN_VOXELS, alignof(uint64_t));
        job->borrowed_atlas_pixels =
            arena_alloc(&game->arena_world, UI_ATLAS_COLUMN_PIXELS * sizeof(uint32_t), alignof(uint32_t));
        job->borrowed_atlas_colours = game->borrowed_ui != nullptr ? ui_atlas_colours(game->borrowed_ui) : nullptr;
    }

    if (game->borrowed_player != nullptr) {
        game->borrowed_felling = arena_alloc(&game->arena_world, sizeof(player_felling_t), alignof(player_felling_t));
        player_felling_start(game->borrowed_felling);
        game->borrowed_sounds =
            arena_alloc_zeroed(&game->arena_world, sizeof(player_sounds_t), alignof(player_sounds_t));
    }

    /* Flying: a little above whatever is at the world's middle, water included, looking out and down. Walking: on dry land near it. */
    int32_t start_x = GAME_WORLD_MIDDLE, start_y = GAME_WORLD_MIDDLE;
    /* A world that has been saved takes up where the player was, with no looking for land. */
    float64_t kept_feet[3] = {0.0f64, 0.0f64, 0.0f64};
    float32_t kept_yaw = 0.0f32, kept_pitch = 0.0f32;
    save_t *const save =
        game->borrowed_save != nullptr && game->borrowed_save->world.open != 0u ? game->borrowed_save : nullptr;
    if (game->borrowed_player != nullptr && save != nullptr &&
        save_state_kept_start(&save->state, kept_feet, &kept_yaw, &kept_pitch)) {
        start_x = (int32_t)floor(kept_feet[0]);
        start_y = (int32_t)floor(kept_feet[1]);
        player_spawn(game->borrowed_player, kept_feet[0], kept_feet[1], kept_feet[2]);
        player_look_send(game->borrowed_player, kept_yaw, kept_pitch);
        render_camera_start(&game->camera, kept_feet[0], kept_feet[1], kept_feet[2] + PLAYER_MOVEMENT_EYE_STANDING,
                            kept_yaw, kept_pitch);
        LOG_INFO("game: the player goes on from %.1f, %.1f, %.1f", (libc_double_t)kept_feet[0],
                 (libc_double_t)kept_feet[1], (libc_double_t)kept_feet[2]);
    } else {
        /* A flying eye starts where a body would, over ground worth seeing, and not inside a range that stands at the middle. */
        if (!game_spawn_find(game, terrain, &game->borrowed_jobs[0], start_x, start_y, &start_x, &start_y)) {
            LOG_WARN("game: no dry land within %d blocks of the world's middle, so the body starts at the middle",
                     GAME_SPAWN_REACH);
        }
        const float64_t ground = (float64_t)game_ground_z(terrain, &game->borrowed_jobs[0], start_x, start_y);
        const float64_t middle_x = (float64_t)start_x + 0.5f64, middle_y = (float64_t)start_y + 0.5f64;
        float64_t eye_z = ground + GAME_START_EYE_HEIGHT;
        if (game->borrowed_player != nullptr) {
            /* Feet on top of whatever that is. Nothing is above it, so the body fits. */
            player_spawn(game->borrowed_player, middle_x, middle_y, ground + 1.0f64);
            eye_z = ground + 1.0f64 + PLAYER_MOVEMENT_EYE_STANDING;
            LOG_INFO("game: the player starts at %.1f, %.1f, %.1f", (libc_double_t)middle_x, (libc_double_t)middle_y,
                     (libc_double_t)(ground + 1.0f64));
        }
        render_camera_start(&game->camera, middle_x, middle_y, eye_z, GAME_START_YAW, GAME_START_PITCH);
    }
    const int32_t chunk_x = column_chunk_of(start_x), chunk_y = column_chunk_of(start_y);
    atomic_store_explicit(&game->center_x, chunk_x, memory_order_relaxed);
    atomic_store_explicit(&game->center_y, chunk_y, memory_order_relaxed);
    atomic_store_explicit(&game->stop, 0u, memory_order_relaxed);
    atomic_store_explicit(&game->failed, 0u, memory_order_relaxed);
    atomic_store_explicit(&game->paused, 0u, memory_order_relaxed);

    game->config = *config;
    game->look_rest[0] = 0.0f64;
    game->look_rest[1] = 0.0f64;
    game->view_held = 0u;
    game->view_stale = 0u;
    atomic_store_explicit(&game->uncapped, config->uncapped, memory_order_relaxed);
    game->camera.half_fov_tangent = game_view_half_fov_tangent(config->fov_degrees);
    if (!game_open_parts(game, config, chunk_x, chunk_y)) {
        heap_release(&game->owned_view);
        job_group_deinit(&game->group);
        game_arenas_stop(game);
        heap_release(&game->owned_region);
        return false;
    }
    game_open_alive_tell(game);
    game_fill_start(&world_parts->fill, GAME_FILL_OPEN, config->render_distance, game->view.full_detail,
                    game->borrowed_render);
    game_open_climate(game, start_x, start_y);
    /* A new world's calendar: Year 1, Day 80, week 11, with no census owed. A saved one replaces it below. */
    game->day_zero = CALENDAR_DAY_ZERO;
    game_clock_set(game, 0u, GAME_DAY_START);
    game->last_census_week =
        calendar_week_of(game->day_zero, atomic_load_explicit(&game->clock_units, memory_order_relaxed));
    game->census_runs = 0u;
    atomic_store_explicit(&game->census_latch, 0u, memory_order_relaxed);
    game_day_apply(game, &game->camera);
    game_hud_start(game);
    if (game->borrowed_entity != nullptr) {
        entity_open(game->borrowed_entity, terrain_rng_derive(terrain->seed, terrain_rng_salt("entity")));
        /* A stream of its own, so that what falls and scatters is the same whether or not a mob came. */
        mob_spawn_start(&game->mob_spawn, &game->arena_world,
                        terrain_rng_derive(terrain->seed, terrain_rng_salt("mob/spawn")));
    }
    if (game->borrowed_mob != nullptr) {
        mob_open(game->borrowed_mob, game->borrowed_world, terrain->seed);
    }
    if (game->borrowed_item != nullptr) {
        item_open(game->borrowed_item);
        item_world_begin(game->borrowed_item, &game->borrowed_world->edit);
    }
    if (game->borrowed_ui != nullptr) {
        ui_screen_world_begin(game->borrowed_ui);
    }
    game->tick_count = 0u;
    if (save != nullptr) {
        save_state_parts_t parts;
        game_save_parts(game, &parts);
        save_state_apply(&save->state, &parts);
        game_open_retire_mobs(game);
        game->tick_count = parts.time.world_ticks;
        world_tick_now_set(&game->borrowed_world->tick, game->tick_count);
        save_world_levels_from(&save->world, &game->borrowed_world->tick);
        game->day_zero = parts.calendar.day_zero;
        game->last_census_week = parts.calendar.last_census_week;
        game_clock_set(game, parts.calendar.clock_days, (float64_t)parts.time.day_units / 4294967296.0f64);
        game_day_apply(game, &game->camera);
        save_autosave_start(&save->autosave, game->borrowed_job, &save->world, &save->state, game->tick_count);
    }
    game_census_attach(game); /* plan 004 phase 07: with the calendar known */
    game_atlas_open(game);    /* plan 004 phase 09 */
    game->open = 1u;
    return true;
}

void game_close(game_t *game) {
    if (game->open == 0u) {
        return;
    }
    ASSERT_ALWAYS(game->threads_running == 0u);
    /* Nothing may be writing into the jobs' memory when it goes. */
    job_group_wait(game->borrowed_job, &game->group);
    job_group_deinit(&game->group);
    game_corner_stop(game_corner_of(game), game->borrowed_job); /* the storm warning's job reads the climate */
    game_census_detach(game);
    game_atlas_close(game); /* its drawing reads the climate */
    if (game->borrowed_climate != nullptr) {
        climate_close(game->borrowed_climate, game->borrowed_job); /* waits for the tiles the sim queued */
    }
    if (game->borrowed_item != nullptr) {
        item_close(game->borrowed_item);
    }
    if (game->borrowed_mob != nullptr) {
        mob_close(game->borrowed_mob);
    }
    if (game->borrowed_entity != nullptr) {
        entity_close(game->borrowed_entity);
        memset(&game->mob_spawn, 0, sizeof(game->mob_spawn));
    }
    render_close(game->borrowed_render);
    world_close(game->borrowed_world);
    game->borrowed_felling = nullptr;
    game->borrowed_sounds = nullptr;
    game_arenas_stop(game);
    heap_release(&game->owned_region);
    heap_release(&game->owned_view);
    game->view_held = 0u;
    game->open = 0u;
    game->jobs_in_use = 0u;
    game->stream_quiet = 0u;
    game->borrowed_jobs = nullptr;
    game->borrowed_last_picture = nullptr;
}

/* ---- Changing a world's config while it is open ------------------------------------------------------------------- */

static const uint8_t *game_picture_full(game_t *game, const uint8_t *drawn);
static bool game_threads_start(game_t *game);

/* The pictures are about to go, and something may be showing the last one in a pause: it stays, at the size it is shown. */
static bool game_picture_hold(game_t *game) {
    if (game->borrowed_last_picture == nullptr) {
        return true;
    }
    if (!game_picture_full_room(game)) {
        return false; /* it has said why */
    }
    const uint8_t *const full = game_picture_full(game, game->borrowed_last_picture);
    if (full != game->owned_view.owned_base) {
        memcpy(game->owned_view.owned_base, full,
               (uint64_t)game->config.width * game->config.height * sizeof(uint32_t));
    }
    game->borrowed_last_picture = nullptr;
    game->view_held = 1u;
    return true;
}

/* Pictures of the view's size in place of what the frame holds now, and back to what it held if the card will not have them. */
static bool game_frame_resize(game_t *game, const game_view_t *now) {
    render_t *const render = game->borrowed_render;
    if (!game_picture_hold(game)) {
        return false;
    }
    if ((now->width != game->config.width || now->height != game->config.height) && !game_picture_full_room(game)) {
        return false; /* it has said why */
    }
    render_close_frame(render);
    if (render_open_frame(render, game->borrowed_texture, now->width, now->height, now->fog_end) == GPU_STATUS_OK) {
        return true;
    }
    LOG_WARN("game: no room on the card for pictures of %" PRIu32 " by %" PRIu32 ", so they stay as they were",
             now->width, now->height);
    return false;
}

/* Everything that needs the threads stopped. False if a frame or threads could not be had back at all. */
static bool game_view_change(game_t *game, game_view_t *now) {
    render_t *const render = game->borrowed_render;
    const game_view_t *const was = &game->view;
    const bool resize = now->width != was->width || now->height != was->height;
    if (resize) {
        if (!game_frame_resize(game, now)) {
            now->width = was->width;
            now->height = was->height;
            if (render->frame_open == 0u && render_open_frame(render, game->borrowed_texture, now->width, now->height,
                                                              now->fog_end) != GPU_STATUS_OK) {
                return false; /* what it was a moment ago will not come back: it has said why */
            }
        }
    }
    /* A level that stays the same keeps what it has built, so a far column at the old setting goes only when its own replacement has come. */
    gpu_status_t far = GPU_STATUS_OK;
    if (render->borrowed_lod != nullptr && memcmp(&now->lod, &was->lod, sizeof(now->lod)) != 0) {
        far = now->lod.level_count != 0u ? game_lod_replan(game, &now->lod) : GPU_STATUS_OK;
        if (now->lod.level_count == 0u) {
            render_close_lod(render);
        }
    }
    world_map_set_view_radius(&game->borrowed_world->map, now->full_detail);
    if (far == GPU_STATUS_OK && now->lod.level_count != 0u && render->borrowed_lod == nullptr) {
        far = game_lod_open(game, &now->lod);
    }
    if (far != GPU_STATUS_OK) {
        /* The world still works, to where full detail goes, and the fog comes in to meet it. */
        LOG_WARN("game: no room on the card for a far field, so the world stops at %" PRIu32 " chunks",
                 now->full_detail);
        now->lod = (render_lod_plan_t){0};
        now->fog_end = game_view_fog_end(now->full_detail);
    }
    render_frame_fog_set(&render->frame, now->fog_end);
    render_frame_features_set(&render->frame, now->features | SHADE_FEATURE_SUN_APART);
    return true;
}

bool game_reconfigure(game_t *game, const game_config_t *config) {
    ASSERT_ALWAYS(game->open != 0u);
    ASSERT_ALWAYS(config->width == game->config.width && config->height == game->config.height);
    ASSERT_ALWAYS(config->render_distance != 0u && config->render_distance <= MC_GAME_RENDER_DISTANCE_MAX);
    const float32_t half_fov_tangent = game_view_half_fov_tangent(config->fov_degrees);
    const bool fov_changed = half_fov_tangent != game->camera.half_fov_tangent;
    game->camera.half_fov_tangent = half_fov_tangent;
    atomic_store_explicit(&game->uncapped, config->uncapped, memory_order_relaxed);
    game_view_t now;
    const uint32_t table_chunks = game->borrowed_world->map.table_radius - WORLD_MAP_KEEP_EXTRA;
    game_view_work_out(config, table_chunks, &now);
    if (memcmp(&now, &game->view, sizeof(now)) == 0) {
        game->config = *config;
        /* game_loop_frame is what publishes a camera, and it does not run in a pause, where a person is looking behind the screen for this. */
        if (fov_changed && game->threads_running != 0u) {
            game->camera_slots[game->cameras.back] = game->camera;
            render_exchange_publish(&game->cameras);
        } else if (fov_changed) {
            game->view_stale = 1u;
        }
        return true;
    }
    const bool running = game->threads_running != 0u;
    if (running && !game_loop_stop(game)) {
        return false; /* a thread has said why it failed */
    }
    game_open_alive_tell(game);
    const bool changed = game_view_change(game, &now);
    game_open_alive_tell(game);
    game->config = *config;
    game->view = now;
    game->view_stale = 1u;
    if (!changed) {
        atomic_store_explicit(&game->failed, 1u, memory_order_release);
        return false;
    }
    LOG_INFO("game: now drawing %" PRIu32 " chunks, %" PRIu32 " of them block by block, %" PRIu32
             " far levels, at %" PRIu32 " by %" PRIu32 ", with effects %#" PRIx32,
             config->render_distance, now.full_detail, now.lod.level_count, now.width, now.height, now.features);
    game_fill_start(game_fill_of(game), GAME_FILL_CHANGE, config->render_distance, now.full_detail,
                    game->borrowed_render);
    if (running && !game_threads_start(game)) {
        atomic_store_explicit(&game->failed, 1u, memory_order_release);
        return false; /* it has said why */
    }
    return true;
}

/* ---- The sim ------------------------------------------------------------------------------------------------------ */

static void game_column_job_run(job_worker_t *worker, void *borrowed_job) {
    PROFILER_BEGIN(job_worker_lane(worker), "column");
    game_column_job_t *const job = borrowed_job;
    const terrain_density_box_t box = game_column_box(job->request.chunk_x * (int32_t)COLUMN_SIZE,
                                                      job->request.chunk_y * (int32_t)COLUMN_SIZE, COLUMN_SIZE);
    terrain_generate_box(job->borrowed_terrain, &box, job->borrowed_opened, job->borrowed_states);
    if (job->borrowed_save != nullptr) {
        /* Terrain has finished with what it worked in, which is far larger than this needs. */
        static_assert((uint64_t)GAME_COLUMN_VOXELS >=
                      (uint64_t)SAVE_WORLD_OVERLAY_SCRATCH_BYTES + (uint64_t)SAVE_WORLD_OVERLAY_LEVELS_BYTES);
        job->saved_sections = save_world_column_overlay_levels(
            job->borrowed_save, job->request.chunk_x, job->request.chunk_y, job->borrowed_states,
            job->borrowed_opened + SAVE_WORLD_OVERLAY_SCRATCH_BYTES, &job->levels_mask, job->borrowed_opened);
    }
    if (job->borrowed_atlas_colours != nullptr) {
        ui_atlas_column_top(job->borrowed_atlas_colours, BLOCK_COUNT, job->borrowed_states, job->borrowed_atlas_pixels);
    }
    PROFILER_END(job_worker_lane(worker));
    /* Release gives the sim thread the states just written, and pairs with the acquire in game_columns_commit. */
    atomic_store_explicit(&job->done, 1u, memory_order_release);
}

/* A column ready to be filled, from a job that has finished. False if the world has no room for it: it has said so. */
static bool game_column_fill(game_t *game, const game_column_job_t *job) {
    world_t *const world = game->borrowed_world;
    column_t *const column = world_map_stream_commit(&world->map, &job->request);
    if (column == nullptr) {
        return true; /* out of date, so there is nowhere to put it */
    }
    for (uint32_t section = 0u; section < COLUMN_SECTIONS; section++) {
        const uint16_t *const states = job->borrowed_states + (uint64_t)section * SECTION_VOXELS;
        if (!section_blocks_import(&column->sections[section], &world->section_pool, states)) {
            LOG_ERROR("game: the world's section memory is full, at column %" PRId32 ", %" PRId32, job->request.chunk_x,
                      job->request.chunk_y);
            world_map_unload(&world->map, job->request.slot);
            return false;
        }
    }
    world_edit_column_filled(&world->edit, job->request.slot);
    game_census_mark(game, job->request.chunk_x, job->request.chunk_y);
    if (job->borrowed_save != nullptr) {
        for (column_mask_t left = job->levels_mask; column_mask_any(left); left = column_mask_drop_lowest(left)) {
            const uint32_t section = column_mask_lowest(left);
            world_tick_section_levels_restore(&world->tick, job->request.chunk_x, job->request.chunk_y, section,
                                              job->borrowed_opened + SAVE_WORLD_OVERLAY_SCRATCH_BYTES +
                                                  (uint64_t)section * SAVE_WORLD_RECORD_LEVELS_BYTES);
        }
        world_tick_column_saved(&world->tick, job->request.slot, job->request.chunk_x, job->request.chunk_y,
                                job->saved_sections);
    }
    /* Short of memory, it says so, and world_light_update tries again. */
    (void)world_light_column_added(&world->light, job->request.slot);
    game_cover_stream_in(game, job->request.chunk_x,
                         job->request.chunk_y); /* in step with the week: plan 004 phase 08 */
    if (job->borrowed_atlas_colours != nullptr) {
        ui_atlas_column_write(game_atlas(game), job->request.chunk_x, job->request.chunk_y, job->borrowed_atlas_pixels);
    }
    game->columns_made++;
    return true;
}

static void game_columns_commit(game_t *game) {
    /* What is finished past this waits for the next step, so that a snapshot still goes out as often as it should. That step
     * starts where this one stopped, so that no column waits behind others forever. */
    const int64_t deadline_ns = time_now_ns() + GAME_COMMIT_BUDGET_NS;
    for (uint32_t n = 0u; n < GAME_STREAM_JOBS && time_now_ns() < deadline_ns; n++) {
        const uint32_t i = game->commit_next;
        game->commit_next = i + 1u == GAME_STREAM_JOBS ? 0u : i + 1u;
        game_column_job_t *const job = &game->borrowed_jobs[i];
        if (job->in_use != 0u && atomic_load_explicit(&job->done, memory_order_acquire) != 0u) {
            /* A full world asks again for this column later, when something has left. */
            (void)game_column_fill(game, job);
            job->in_use = 0u;
            game->jobs_in_use--;
        }
    }
}

/* The changes of a column on its way out, so that it may go. False if they will not fit, and a save has been asked for: the column stays, keeping what
 * would be lost, until a later step can do better. */
static bool game_column_leaving_save(game_t *game, uint32_t slot) {
    save_t *const save = game->borrowed_save;
    if (save == nullptr || save->world.open == 0u) {
        return true;
    }
    world_edit_t *const edit = &game->borrowed_world->edit;
    const save_world_status_t status = save_world_capture_column(&save->world, edit, slot);
    if (status == SAVE_WORLD_FULL) {
        /* Only the autosave writes, so that there is never a second write and always a state to go with it. The column stays until it has. */
        save_autosave_request(&save->autosave);
        return false;
    }
    if (status != SAVE_WORLD_OK && game->save_failed_said == 0u) {
        LOG_ERROR("game: a column on its way out could not be saved, so it stays: %s", save_world_status_name(status));
        game->save_failed_said = 1u;
    }
    return status == SAVE_WORLD_OK;
}

static void game_columns_request(game_t *game) {
    world_map_t *const map = &game->borrowed_world->map;
    game->stream_quiet = 0u;
    for (uint32_t i = 0u; i < GAME_STREAM_JOBS; i++) {
        game_column_job_t *const job = &game->borrowed_jobs[i];
        if (job->in_use != 0u) {
            continue;
        }
        world_map_request_t request;
        if (world_map_stream_take(map, &request, 1u) == 0u) {
            game->stream_quiet = game->jobs_in_use == 0u ? 1u : 0u;
            return;
        }
        /* A column waits for its climate tiles: a job may not wait, so the waiting is here. The map offers the same nearest
         * column again next step, and the tiles come nearest the player first. */
        const climate_t *const climate = game_climate(game);
        const int32_t west = request.chunk_x * (int32_t)COLUMN_SIZE, south = request.chunk_y * (int32_t)COLUMN_SIZE;
        if (climate != nullptr && !climate_area_ready(climate, west, south, west + (int32_t)COLUMN_SIZE - 1,
                                                      south + (int32_t)COLUMN_SIZE - 1)) {
            (void)world_map_stream_cancel(map, &request);
            return;
        }
        job->request = request;
        atomic_store_explicit(&job->done, 0u, memory_order_relaxed);
        if (!job_try_submit(game->borrowed_job, &game->group, game_column_job_run, job)) {
            (void)world_map_stream_cancel(map, &request); /* the shared queue is full: it is offered again next step */
            return;
        }
        job->in_use = 1u;
        game->jobs_in_use++;
    }
}

void game_sim_step(game_t *game) {
    PROFILER_BEGIN(game->borrowed_lane_sim, "sim_step");
    /* In every step and not only in a tick, so that a click shows in a few milliseconds and not in up to fifty. */
    if (game_has_screens(game)) {
        ui_screen_sim_t sim;
        game_screen_sim(game, &sim);
        ui_screen_sim_step(game->borrowed_ui, &sim, false);
    }
    world_t *const world = game->borrowed_world;
    world_map_t *const map = &world->map;
    const int32_t center_x = atomic_load_explicit(&game->center_x, memory_order_relaxed);
    const int32_t center_y = atomic_load_explicit(&game->center_y, memory_order_relaxed);
    if (center_x != map->center_x || center_y != map->center_y) {
        world_map_set_center(map, center_x, center_y);
    }
    world_map_leaving_t leaving;
    while (world_map_stream_leaving(map, &leaving)) {
        if (!game_column_leaving_save(game, leaving.slot)) {
            break; /* it is offered again next step */
        }
        (void)world_light_column_leaving(&world->light, leaving.slot);
        world_tick_column_leaving(&world->tick, leaving.slot);
        world_map_unload(map, leaving.slot);
    }
    game_columns_commit(game);
    climate_t *const climate = game_climate(game);
    /* Plan 004 phase 07's census moves on here, paused or not; while it builds climate tiles the keep square stays put. */
    if (!game_census_sim(game) && climate != nullptr) {
        const int32_t half = (int32_t)COLUMN_SIZE / 2;
        climate_schedule(climate, game->borrowed_job, map->center_x * (int32_t)COLUMN_SIZE + half,
                         map->center_y * (int32_t)COLUMN_SIZE + half, game_climate_radius(map));
    }
    game_cover_step(game); /* after a census, snow and ice on what is loaded: plan 004 phase 08 */
    game_columns_request(game);
    (void)world_light_update(&world->light);
    (void)render_upload_resend_drain(&game->borrowed_render->upload, &world->edit);
    /* False is only that nothing changed. */
    (void)world_edit_snapshot_publish(&world->snapshot);
    save_t *const save = game->borrowed_save;
    if (save != nullptr && save_autosave_due(&save->autosave, game->tick_count)) {
        game_save_items_settle(game);
        save_state_parts_t parts;
        game_save_parts(game, &parts);
        save_autosave_step(&save->autosave, &world->edit, &parts, game->tick_count);
    }
    ui_atlas_save_step(game_atlas(game), (int64_t)game->tick_count * GAME_TICK_NS);
    PROFILER_END(game->borrowed_lane_sim);
}

/* ---- What is drawn on a picture ------------------------------------------------------------------------------------- */

static_assert(sizeof(item_stack_t) == sizeof(uint64_t));
static_assert((uint32_t)UI_HUD_STAGE_COARSE == (uint32_t)FRAME_PASS_COARSE);
static_assert((uint32_t)UI_HUD_STAGE_PRIMARY == (uint32_t)FRAME_PASS_PRIMARY);
static_assert((uint32_t)UI_HUD_STAGE_SHADE == (uint32_t)FRAME_PASS_SHADE);
static_assert((uint32_t)UI_HUD_STAGE_POST == (uint32_t)FRAME_PASS_POST);
static_assert((uint32_t)UI_HUD_STAGE_READBACK == (uint32_t)FRAME_PASS_READBACK);
static_assert((uint32_t)UI_HUD_STAGE_GPU_TOTAL == (uint32_t)FRAME_PASS_COUNT);

/* Counts new pictures over at least GAME_FPS_WINDOW_SECONDS of what it is told has passed. */
static void game_fps_note(game_t *game, uint32_t pictures, float32_t seconds) {
    game->hud_fps_pictures += pictures;
    game->hud_fps_seconds += (float64_t)seconds;
    if (game->hud_fps_seconds >= GAME_FPS_WINDOW_SECONDS) {
        game->hud_fps = (float32_t)((float64_t)game->hud_fps_pictures / game->hud_fps_seconds);
        game->hud_fps_pictures = 0u;
        game->hud_fps_seconds = 0.0f64;
    }
}

/* One frame, and what each pass of it took, as the difference it made to the frame's books. */
static gpu_status_t game_frame_draw(game_t *game, const render_camera_t *camera) {
    render_t *const render = game->borrowed_render;
    const render_frame_stats_t before = render->frame.stats;
    const gpu_status_t pretended = game_gpu_pretend_step(game);
    if (pretended != GPU_STATUS_OK) {
        return pretended;
    }
    render_frame_under_water_set(&render->frame, game->borrowed_player != nullptr &&
                                                     player_eyes_under_water_read(game->borrowed_player));
    PROFILER_BEGIN(game->borrowed_lane_gpu, "frame_draw");
    /* With the weather where the eye is (plan 004 phase 08): between ticks with threads, as each tick's slot without. */
    const gpu_status_t status = game_weather_draw(game, camera, game->threads_running != 0u ? time_now_ns() : 0);
    PROFILER_END(game->borrowed_lane_gpu);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    game_watch_kernels_note(game, &render->frame.last_timings);
    const render_frame_stats_t *const after = &render->frame.stats;
    for (uint32_t pass = 0u; pass < FRAME_PASS_COUNT; pass++) {
        atomic_store_explicit(&game->hud_pass_us[pass],
                              (uint32_t)((after->sum_ms[pass] - before.sum_ms[pass]) * 1000.0f64),
                              memory_order_relaxed);
    }
    atomic_store_explicit(&game->hud_total_us, (uint32_t)((after->sum_total_ms - before.sum_total_ms) * 1000.0f64),
                          memory_order_relaxed);
    return GPU_STATUS_OK;
}

/* A picture as the frame drew it, at the size of what shows it: itself at a render scale of 100, and owned_view at any other. */
static const uint8_t *game_picture_full(game_t *game, const uint8_t *drawn) {
    const game_view_t *const view = &game->view;
    if (view->width == game->config.width && view->height == game->config.height) {
        return drawn;
    }
    game_view_bring_up(drawn, view->width, view->height, game->owned_view.owned_base, game->config.width,
                       game->config.height);
    return game->owned_view.owned_base;
}

const uint8_t *game_picture_held(game_t *game) {
    if (game->borrowed_last_picture != nullptr) {
        return game_picture_full(game, game->borrowed_last_picture);
    }
    return game->view_held != 0u ? game->owned_view.owned_base : nullptr;
}

/* What goes to the screen: picture itself if there is nothing to draw on it, or if the memory for a copy could not be had, as a picture
 * with no HUD is better than none. The copy leaves picture as it was, since it may be shown again. */
/* Under KMS nobody can read the log, and a person playing on would otherwise find out what the disk has been saying only on leaving. */
static bool game_hud_trouble_draw(game_t *game) {
    const save_t *const save = game->borrowed_save;
    if (save == nullptr || save->world.open == 0u) {
        return false;
    }
    const char *const trouble = save_autosave_trouble(&save->autosave);
    if (trouble != nullptr) {
        char text[UI_HUD_NOTICE_BYTES];
        (void)snprintf(text, sizeof(text), "Not saving: %s", trouble);
        ui_hud_notice_draw(&game->borrowed_ui->canvas, &game->borrowed_ui->font, text);
    }
    return trouble != nullptr;
}

static const uint8_t *game_hud_compose(game_t *game, const uint8_t *drawn) {
    ui_t *const ui = game->borrowed_ui;
    const bool walking = game->borrowed_player != nullptr;
    const bool screen_up = game_has_screens(game) && ui_screen_up(ui);
    const uint8_t *const picture = game_picture_full(game, drawn);
    if (ui != nullptr && game_atlas_up(game)) {
        return game_atlas_compose(game, picture);
    }
    /* Flying shows the date only when asked for it, as --test-world --day-of-year does: every other flying picture stays as it was. */
    const bool date_flying = !walking && game->config.show_date != 0u;
    if (ui == nullptr || (!walking && game->hud_debug == 0u && !screen_up && !date_flying)) {
        return picture;
    }
    if (!ui_frame_begin_over(ui, picture, game->config.width, game->config.height)) {
        return picture;
    }
    if (date_flying) {
        char line[CALENDAR_LINE_BYTES];
        game_calendar_line(game, line, sizeof(line));
        ui_hud_date_draw(&ui->canvas, &ui->font, line, false);
    }
    if (walking) {
        ui_hud_state_t state;
        game_hud_state_read(game, &state);
        ui_hud_draw(&ui->hud, &ui->canvas, &ui->font, &state);
        bool notice = false;
        if (player_vitals_read(game->borrowed_player).state == (uint8_t)PLAYER_SURVIVAL_DEAD) {
            notice = true;
            /* There is no death screen, and the view on the ground says nothing of how to get up. This comes before a full disk: a person
             * who cannot get up can do nothing about that. */
            ui_hud_notice_draw(&ui->canvas, &ui->font, "You died. Press a mouse button or Space to come back.");
        } else {
            notice = game_hud_trouble_draw(game);
        }
        game_hud_corners_draw(game,
                              notice); /* the coordinates, and the date, climate line and warning (plan 004 phase 10) */
    }
    if (game->hud_debug != 0u) {
        ui_hud_debug_t debug;
        game_hud_debug_read(game, &debug);
        ui_hud_debug_draw(&ui->canvas, &ui->font, &debug);
    }
    if (screen_up) {
        ui_screen_draw(ui);
    }
    return ui_frame_end(ui, screen_up);
}

static bool game_scene_tick_timed(game_t *game, const display_headless_tick_t *tick) {
    /* Stopped, by a census or by whoever paused: neither the clock nor the world moves, as with threads. */
    if (atomic_load_explicit(&game->paused, memory_order_relaxed) != 0u) {
        return true;
    }
    render_fly_input_t input;
    game_fly_from_script(tick->state, &input);
    /* Time here is the count of ticks, so one script gives one path. */
    const int64_t tick_ns = (int64_t)(game->tick_count + 1u) * GAME_TICK_NS;
    game_day_advance(game, 1.0f32 / (float32_t)MC_GAME_TICK_HZ);
    game_fps_note(game, 0u, 1.0f32 / (float32_t)MC_GAME_TICK_HZ);
    game_debug_send(game, display_script_state_is_down(tick->state, KEY_F3));
    input_frame_t frame;
    game_input_from_script(tick, &frame);
    if (game->borrowed_player == nullptr) {
        if (game_screen_input(game, &frame)) {
            memset(&input, 0, sizeof(input)); /* flying, only the Atlas comes up: the eye stays where it is */
        }
        game_fly(game, &input, 1.0f32 / (float32_t)MC_GAME_TICK_HZ);
        game_sim_tick(game, tick_ns);
        return true;
    }
    /* A draw settles too, but a script need not draw every tick, and the body would then wait for ground that never comes. */
    if (!game_settle(game)) {
        return false;
    }
    if (game_screen_input(game, &frame)) {
        /* The same as a real frame with a screen up: the body stands, and looks where it looked. */
        memset(&input, 0, sizeof(input));
        game_walk_send(game, &input, false, false, tick_ns);
        game_hands_send(game, false, false);
    } else {
        game_walk_send(game, &input, display_script_state_is_down(tick->state, KEY_SPACE),
                       display_script_state_is_down(tick->state, KEY_LEFTSHIFT), tick_ns);
        game_hands_send(game, display_script_state_is_down(tick->state, BTN_LEFT),
                        display_script_state_is_down(tick->state, BTN_RIGHT));
        bool hotbar[INVENTORY_HOTBAR_SLOTS];
        for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
            hotbar[slot] = display_script_state_is_down(tick->state, KEY_1 + slot);
        }
        game_hotbar_send(game, hotbar, 0);
    }
    game_sim_tick(game, tick_ns);
    /* A whole tick on from when it was due: where this tick ended. */
    game_walk_view(game, tick_ns + GAME_TICK_NS);
    return true;
}

static bool game_scene_tick(void *context, const display_headless_tick_t *tick) {
    game_t *const game = context;
    PROFILER_BEGIN(game->borrowed_lane_main, "scene_tick");
    const bool ok = game_scene_tick_timed(game, tick);
    PROFILER_END(game->borrowed_lane_main);
    return ok;
}

static bool game_scene_draw_timed(game_t *game, display_buffer_t *frame) {
    render_t *const render = game->borrowed_render;
    arena_reset(&game->arena_frame);
    PROFILER_BEGIN(game->borrowed_lane_main, "settle");
    const bool settled = game_settle(game);
    PROFILER_END(game->borrowed_lane_main);
    if (!settled) {
        return false;
    }
    /* What later stages draw each frame goes in here. */
    game_day_apply(game, &game->camera);
    const gpu_status_t status = game_frame_draw(game, &game->camera);
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&render->frame.gpu.last_error);
        game_gpu_failed_note(game, status);
        return false;
    }
    const uint8_t *const picture = render_frame_take(&render->frame);
    ASSERT_ALWAYS(picture != nullptr);
    game_fps_note(game, 1u, 0.0f32);
    game->borrowed_last_picture = picture; /* what a pause screen goes over */
    game->view_held = 0u;
    game->view_stale = 0u;
    PROFILER_BEGIN(game->borrowed_lane_main, "hud_compose");
    const uint8_t *const shown = game_hud_compose(game, picture);
    PROFILER_END(game->borrowed_lane_main);
    PROFILER_BEGIN(game->borrowed_lane_main, "present");
    render_frame_present_full(shown, frame);
    PROFILER_END(game->borrowed_lane_main);
    return true;
}

static bool game_scene_draw(void *context, const display_headless_tick_t *tick, display_buffer_t *frame) {
    (void)tick;
    game_t *const game = context;
    PROFILER_BEGIN(game->borrowed_lane_main, "scene_draw");
    const int64_t started_ns = time_now_ns();
    const bool ok = game_scene_draw_timed(game, frame);
    /* With no threads a frame is this draw: the tick before it is timed as a tick. */
    game_watch_frame_look(game, time_now_ns() - started_ns);
    PROFILER_END(game->borrowed_lane_main);
    return ok;
}

bool game_view_refresh(game_t *game) {
    render_t *const render = game->borrowed_render;
    if (game->threads_running != 0u) {
        const uint8_t *const newest = render_frame_take(&render->frame);
        if (newest != nullptr) {
            game->borrowed_last_picture = newest;
            game->view_held = 0u;
            game->shown_camera = game->picture_cameras[render->frame.pictures.front];
        }
        (void)game_gpu_alive(game); /* it says so, and that is failed */
        return atomic_load_explicit(&game->failed, memory_order_acquire) == 0u;
    }
    if (game->view_stale == 0u) {
        return true;
    }
    game->view_stale = 0u;
    if (!game_settle(game)) {
        return false;
    }
    game_day_apply(game, &game->camera);
    const gpu_status_t status = game_frame_draw(game, &game->camera);
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&render->frame.gpu.last_error);
        game_gpu_failed_note(game, status);
        return false;
    }
    game->borrowed_last_picture = render_frame_take(&render->frame);
    ASSERT_ALWAYS(game->borrowed_last_picture != nullptr);
    game->view_held = 0u;
    const world_map_t *const map = &game->borrowed_world->map;
    LOG_INFO(
        "game: settled on %" PRIu32 " columns, with the map's view at %" PRIu32 " chunks and %" PRIu32 " far levels",
        map->ready_count, map->view_radius, render->borrowed_lod != nullptr ? render->borrowed_lod->level_count : 0u);
    return true;
}

void game_headless_scene(game_t *game, display_headless_scene_t *out) {
    *out = (display_headless_scene_t){.borrowed_context = game, .tick = game_scene_tick, .draw = game_scene_draw};
}

/* ---- The real loop's threads --------------------------------------------------------------------------------------- */

static void game_sim_thread(void *borrowed_argument) {
    game_t *const game = borrowed_argument;
    int64_t next_tick_ns = time_now_ns() + GAME_TICK_NS;
    while (atomic_load_explicit(&game->stop, memory_order_acquire) == 0u) {
        game_sim_step(game);
        const int64_t now_ns = time_now_ns();
        if (now_ns >= next_tick_ns && atomic_load_explicit(&game->paused, memory_order_relaxed) != 0u) {
            next_tick_ns = now_ns + GAME_TICK_NS; /* the time paused is not owed afterwards */
        } else if (now_ns >= next_tick_ns) {
            game_sim_tick(game, next_tick_ns);
            next_tick_ns += GAME_TICK_NS;
            atomic_store_explicit(&game->hud_tick_us, (uint32_t)((time_now_ns() - now_ns) / 1000),
                                  memory_order_relaxed);
        }
        if (game->stream_quiet != 0u) {
            time_sleep_ns(GAME_SIM_IDLE_NS);
        }
    }
}

static void game_gpu_thread(void *borrowed_argument) {
    game_t *const game = borrowed_argument;
    render_t *const render = game->borrowed_render;
    const render_camera_t *camera = &game->camera_slots[game->cameras.front];
    while (atomic_load_explicit(&game->stop, memory_order_acquire) == 0u) {
        atomic_fetch_add_explicit(&game->gpu_beats, 1u, memory_order_relaxed);
        /* A picture no one has taken yet is plenty: drawing past the display's rate would only heat the card. Uncapped is someone asking for
         * that, for as long as anyone is looking. */
        const bool draw_on = atomic_load_explicit(&game->uncapped, memory_order_relaxed) != 0u &&
                             atomic_load_explicit(&game->paused, memory_order_relaxed) == 0u;
        if (!draw_on && render_exchange_waiting(&render->frame.pictures)) {
            time_sleep_ns(GAME_GPU_IDLE_NS);
            continue;
        }
        uint32_t slot = 0u;
        if (render_exchange_take(&game->cameras, &slot)) {
            camera = &game->camera_slots[slot];
        }
        PROFILER_BEGIN(game->borrowed_lane_gpu, "upload");
        gpu_status_t status = render_upload_frame(&render->upload);
        PROFILER_END(game->borrowed_lane_gpu);
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&render->buffers.last_error);
            game_gpu_failed_note(game, status);
            atomic_store_explicit(&game->failed, 1u, memory_order_release);
            return;
        }
        game_watch_gpu_look(game);
        if (render->borrowed_lod != nullptr) {
            PROFILER_BEGIN(game->borrowed_lane_gpu, "lod_frame");
            game_far_changes_take(game);
            status = render_lod_frame(render->borrowed_lod, camera->position);
            PROFILER_END(game->borrowed_lane_gpu);
            if (status != GPU_STATUS_OK) {
                game_gpu_failed_note(game, status);
                atomic_store_explicit(&game->failed, 1u, memory_order_release);
                return; /* it has said why */
            }
        }
        game_fill_step(game_fill_of(game), render);
        game_day_apply(game, camera);
        /* In the slot this picture is going into, for whoever asks what it was drawn from. The publish in the draw hands main both. */
        game->picture_cameras[render->frame.pictures.back] = *camera;
        status = game_frame_draw(game, camera);
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&render->frame.gpu.last_error);
            game_gpu_failed_note(game, status);
            atomic_store_explicit(&game->failed, 1u, memory_order_release);
            return;
        }
    }
}

/* Whose lanes the sim's and the gpu's work goes on. Written only with neither thread running, so their start and join order it. */
static void game_lanes_threads(game_t *game, bool threads) {
    game->borrowed_lane_sim = threads ? game->borrowed_lane_sim_thread : game->borrowed_lane_main;
    game->borrowed_lane_gpu = threads ? game->borrowed_lane_gpu_thread : game->borrowed_lane_main;
}

/* Both threads, from the camera as it stands. False, having said why, with neither running. */
static bool game_threads_start(game_t *game) {
    render_exchange_start(&game->cameras);
    /* Before the threads, so the first camera the gpu thread reads is a real one. */
    for (uint32_t i = 0u; i < RENDER_EXCHANGE_SLOTS; i++) {
        game->camera_slots[i] = game->camera;
    }
    atomic_store_explicit(&game->stop, 0u, memory_order_relaxed);
    game->gpu_beats_seen = atomic_load_explicit(&game->gpu_beats, memory_order_relaxed);
    game->gpu_beats_seen_ns = time_now_ns();
    game->gpu_watched_ns = game->gpu_beats_seen_ns;
    game_lanes_threads(game, true);
    game->threads_running = 1u; /* before the starts, which give it to both threads: the gpu's draw reads it */
    if (!thread_start(&game->sim_thread, game->borrowed_topology, THREAD_ROLE_SIM, 0u, game_sim_thread, game)) {
        game->threads_running = 0u;
        game_lanes_threads(game, false);
        return false; /* it has said why */
    }
    if (!thread_start(&game->gpu_thread, game->borrowed_topology, THREAD_ROLE_GPU, 0u, game_gpu_thread, game)) {
        atomic_store_explicit(&game->stop, 1u, memory_order_release);
        thread_join(&game->sim_thread);
        game->threads_running = 0u;
        game_lanes_threads(game, false);
        return false; /* it has said why */
    }
    return true;
}

bool game_loop_start(game_t *game, input_t *input) {
    ASSERT_ALWAYS(game->open != 0u && game->threads_running == 0u);
    game->borrowed_input = input;
    game->last_frame_ns = time_now_ns();
    game->loop_started_ns = game->last_frame_ns;
    game->pictures_shown = 0u;
    return game_threads_start(game);
}

bool game_loop_frame(game_t *game, const render_fly_input_t *input, float32_t seconds, display_buffer_t *target) {
    profiler_lane_t *const lane = game->borrowed_lane_main;
    PROFILER_BEGIN(lane, "loop_frame");
    arena_reset(&game->arena_frame);
    if (game->borrowed_player != nullptr) {
        game_walk_view(game, time_now_ns());
    } else {
        game_fly(game, input, seconds);
    }
    game_day_advance(game, seconds);
    game->camera_slots[game->cameras.back] = game->camera;
    render_exchange_publish(&game->cameras);
    render_frame_t *const frame = &game->borrowed_render->frame;
    const uint8_t *const picture = render_frame_take(frame);
    if (picture != nullptr) {
        game->borrowed_last_picture = picture;
        game->view_held = 0u;
        game->shown_camera = game->picture_cameras[frame->pictures.front];
        game->pictures_shown++;
    }
    game_fps_note(game, picture != nullptr ? 1u : 0u, seconds);
    if (game->borrowed_last_picture != nullptr) {
        const int64_t started_ns = time_now_ns();
        PROFILER_BEGIN(lane, "hud_compose");
        const uint8_t *const shown = game_hud_compose(game, game->borrowed_last_picture);
        PROFILER_END(lane);
        const int64_t composed_ns = time_now_ns();
        PROFILER_BEGIN(lane, "present");
        render_frame_present_full(shown, target);
        PROFILER_END(lane);
        game->hud_compose_us = (uint32_t)((composed_ns - started_ns) / 1000);
        game->hud_present_us = (uint32_t)((time_now_ns() - composed_ns) / 1000);
    }
    /* seconds is the time since the last frame, which a pause and another VT start again. */
    game_watch_frame_look(game, (int64_t)((float64_t)seconds * (float64_t)TIME_NS_PER_SECOND));
    PROFILER_END(lane);
    (void)game_gpu_alive(game); /* it says so, and that is failed */
    return atomic_load_explicit(&game->failed, memory_order_acquire) == 0u;
}

bool game_loop_stop(game_t *game) {
    atomic_store_explicit(&game->stop, 1u, memory_order_release);
    thread_join(&game->sim_thread);
    if (game->gpu_stuck == 0u) {
        thread_join(
            &game->gpu_thread); /* one that is stuck would never answer, and this would be one more thread stuck */
    }
    game_lanes_threads(game, false);
    game->threads_running = 0u;
    game->loop_ended_ns = time_now_ns();
    return atomic_load_explicit(&game->failed, memory_order_acquire) == 0u;
}

void game_loop_source_begin(game_t *game, input_t *input) {
    ASSERT_ALWAYS(game->open != 0u && game->threads_running == 0u);
    game->borrowed_input = input;
}

bool game_loop_threads_ensure(game_t *game) {
    /* Here and not before the run: display_frames_run forks vt_guard's guardian before its first frame, and the sim and gpu
     * threads should not be writing into the memory that fork is about to share. */
    if (game->threads_running == 0u && !game_loop_start(game, game->borrowed_input)) {
        atomic_store_explicit(&game->failed, 1u, memory_order_release);
        return false; /* it has said why */
    }
    return true;
}

bool game_loop_source(void *context, display_buffer_t *back) {
    game_t *const game = context;
    if (!game_loop_threads_ensure(game)) {
        return false;
    }
    const input_frame_t *const frame = input_poll(game->borrowed_input);
    /* With a screen up, Esc closes it. */
    if (input_keys_has(&frame->pressed, KEY_ESC) && !game_screen_up(game)) {
        return false;
    }
    return game_loop_input_frame(game, frame, back);
}

void game_loop_discard(void *context) {
    game_t *const game = context;
    input_test_pattern_discard(game->borrowed_input);
    /* The time away is not time spent flying. */
    game->last_frame_ns = time_now_ns();
}
