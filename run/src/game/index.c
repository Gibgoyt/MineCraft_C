/* See README.md. The Owner of the World, Tick and Frame arenas, and the loop connecting the sim, gpu and main threads. */
#include "game/index.h"

#include "assert/index.h"
#include "entity/animation/index.h"
#include "item/stack/index.h"
#include "game/view/index.h"
#include "log/index.h"
#include "render/entities/index.h"
#include "tables/blocks/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    /* Budgets of the two arenas with a lifetime shorter than a world's. */
    GAME_TICK_ARENA_BYTES = 64ull << 20,
    GAME_FRAME_ARENA_BYTES = 64ull << 20,
    /* A whole column of states, section after section, each in section_voxel_index order. */
    GAME_COLUMN_VOXELS = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS,
    GAME_COLUMN_BYTES = GAME_COLUMN_VOXELS * sizeof(uint16_t),
    GAME_WORLD_ARENA_BYTES = GAME_STREAM_JOBS * (GAME_COLUMN_BYTES + GAME_COLUMN_VOXELS + sizeof(game_column_job_t)) +
                             2ull * 4096ull + MOB_SPAWN_BYTES,
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

/* How far from the world's middle a body looks for dry land, in squares this many blocks apart. A world with none in 1 km of the
 * middle starts it in the water it would have had anyway. */
enum : int32_t {
    GAME_SPAWN_STEP = 16,
    GAME_SPAWN_RINGS = 64,
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
        render_lod_all_changed(lod);
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

static float32_t game_day_of_units(uint32_t units) {
    return (float32_t)((float64_t)units / 4294967296.0f64);
}

void game_day_set(game_t *game, float64_t day_fraction) {
    game->day_fraction = day_fraction - floor(day_fraction);
    atomic_store_explicit(&game->day_units, game_day_units_of(game->day_fraction), memory_order_relaxed);
}

static void game_day_advance(game_t *game, float32_t seconds) {
    if (atomic_exchange_explicit(&game->morning_latch, 0u, memory_order_relaxed) != 0u) {
        game_day_set(game, PLAYER_SURVIVAL_MORNING);
    }
    if ((game->borrowed_render->frame.scene.features & SHADE_FEATURE_DAY_CYCLE) != 0u) {
        game_day_set(game, game->day_fraction + (float64_t)seconds / GAME_DAY_SECONDS);
    }
}

/* On whichever thread draws: the next frame is of the time main last said. */
static void game_day_apply(game_t *game) {
    render_frame_time_set(&game->borrowed_render->frame,
                          game_day_of_units(atomic_load_explicit(&game->day_units, memory_order_relaxed)));
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

/* The highest z with anything in it at this x and y, water and what grows included. Works in a job's memory, before any job runs. */
static int32_t game_ground_z(const terrain_t *terrain, const game_column_job_t *job, int32_t x, int32_t y) {
    const terrain_density_box_t box = game_column_box(x, y, 1u);
    terrain_generate_box(terrain, &box, job->borrowed_opened, job->borrowed_states);
    for (int32_t storage = (int32_t)box.count_z - 1; storage > 0; storage--) {
        if (job->borrowed_states[storage] != BLOCK_ID_AIR) {
            return storage + COLUMN_Z_STORAGE_MIN;
        }
    }
    return COLUMN_Z_STORAGE_MIN;
}

/* Whether a body could stand on this column's top: not water, not a flower, and not a tree's leaves. Reads what game_ground_z left in
 * the job's memory. */
static bool game_top_is_dry_land(const game_column_job_t *job, int32_t ground_z) {
    const uint16_t top = job->borrowed_states[ground_z - COLUMN_Z_STORAGE_MIN];
    return (block_defs[top].flags & BLOCK_FLAG_SOLID) != 0u && (block_defs[top].flags & BLOCK_FLAG_DECAYS) == 0u;
}

/* The nearest dry land to the world's middle, ring by ring of a square around it, and the middle itself if there is none. The same
 * seed gives the same place. */
static void game_spawn_find(const game_t *game, const terrain_t *terrain, const game_column_job_t *job,
                            int32_t middle_x, int32_t middle_y, int32_t *out_x, int32_t *out_y) {
    *out_x = middle_x;
    *out_y = middle_y;
    for (int32_t ring = 0; ring <= GAME_SPAWN_RINGS; ring++) {
        game_open_alive_tell(game);
        for (int32_t dy = -ring; dy <= ring; dy++) {
            /* The inside of a square has been looked at already: only its first and last columns remain on these rows. */
            const int32_t across = dy == -ring || dy == ring ? 1 : (ring == 0 ? 1 : 2 * ring);
            for (int32_t dx = -ring; dx <= ring; dx += across) {
                const int32_t x = middle_x + dx * GAME_SPAWN_STEP, y = middle_y + dy * GAME_SPAWN_STEP;
                if (game_top_is_dry_land(job, game_ground_z(terrain, job, x, y))) {
                    *out_x = x;
                    *out_y = y;
                    return;
                }
            }
        }
    }
}

/* A new world's HUD: nothing carried, and nothing timed yet. The overlay stays as the player left it. */
static void game_hud_start(game_t *game) {
    for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
        atomic_store_explicit(&game->hud_hotbar[slot], 0u, memory_order_relaxed);
    }
    atomic_store_explicit(&game->hud_selected, 0u, memory_order_relaxed);
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
    game->borrowed_jobs =
        arena_alloc_array(&game->arena_world, GAME_STREAM_JOBS, sizeof(game_column_job_t), alignof(game_column_job_t));
    const terrain_t *const terrain = game->borrowed_terrain;
    for (uint32_t i = 0u; i < GAME_STREAM_JOBS; i++) {
        game_column_job_t *const job = &game->borrowed_jobs[i];
        memset(job, 0, sizeof(*job));
        job->borrowed_terrain = terrain;
        job->borrowed_save = game->borrowed_save != nullptr ? &game->borrowed_save->world : nullptr;
        job->borrowed_states = arena_alloc(&game->arena_world, GAME_COLUMN_BYTES, alignof(uint64_t));
        job->borrowed_opened = arena_alloc(&game->arena_world, GAME_COLUMN_VOXELS, alignof(uint64_t));
    }

    /* Flying: a little above whatever is at the world's middle, water included, looking out and down. Walking: on dry land near it. */
    int32_t start_x = 8, start_y = 8;
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
        if (game->borrowed_player != nullptr) {
            game_spawn_find(game, terrain, &game->borrowed_jobs[0], start_x, start_y, &start_x, &start_y);
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
    game_day_set(game, GAME_DAY_START);
    game_day_apply(game);
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
        game->tick_count = parts.time.world_ticks;
        world_tick_now_set(&game->borrowed_world->tick, game->tick_count);
        save_world_levels_from(&save->world, &game->borrowed_world->tick);
        game_day_set(game, (float64_t)parts.time.day_units / 4294967296.0f64);
        game_day_apply(game);
        save_autosave_start(&save->autosave, game->borrowed_job, &save->world, &save->state, game->tick_count);
    }
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
    if (render->borrowed_lod != nullptr && memcmp(&now->lod, &was->lod, sizeof(now->lod)) != 0) {
        render_close_lod(render);
    }
    world_map_set_view_radius(&game->borrowed_world->map, now->full_detail);
    if (now->lod.level_count != 0u && render->borrowed_lod == nullptr &&
        game_lod_open(game, &now->lod) != GPU_STATUS_OK) {
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
    if (job->borrowed_save != nullptr) {
        for (uint64_t left = job->levels_mask; left != 0u; left &= left - 1u) {
            const uint32_t section = (uint32_t)__builtin_ctzll(left);
            world_tick_section_levels_restore(&world->tick, job->request.chunk_x, job->request.chunk_y, section,
                                              job->borrowed_opened + SAVE_WORLD_OVERLAY_SCRATCH_BYTES +
                                                  (uint64_t)section * SAVE_WORLD_RECORD_LEVELS_BYTES);
        }
        world_tick_column_saved(&world->tick, job->request.slot, job->request.chunk_x, job->request.chunk_y,
                                job->saved_sections);
    }
    /* Short of memory, it says so, and world_light_update tries again. */
    (void)world_light_column_added(&world->light, job->request.slot);
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

/* What the screens need to know of this moment, on the sim. */
void game_screen_sim(game_t *game, ui_screen_sim_t *out) {
    const player_t *const player = game->borrowed_player;
    const float64_t *const position = player->movement.position;
    *out = (ui_screen_sim_t){
        .borrowed_item = game->borrowed_item,
        .spill = game_spill,
        .borrowed_context = game,
        .cell = {(int32_t)floor(position[0]), (int32_t)floor(position[1]), (int32_t)floor(position[2])},
        .may_open = player_survival_holds_body(&player->survival) ? 0u : 1u,
        ._pad0 = {0}};
}

bool game_has_screens(const game_t *game) {
    return game->borrowed_ui != nullptr && game->borrowed_item != nullptr && game->borrowed_player != nullptr;
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
    PROFILER_END(game->borrowed_lane_sim);
}

static void game_center_on_eye(game_t *game) {
    const int32_t cell_x = (int32_t)floor(game->camera.position[0]);
    const int32_t cell_y = (int32_t)floor(game->camera.position[1]);
    atomic_store_explicit(&game->center_x, column_chunk_of(cell_x), memory_order_relaxed);
    atomic_store_explicit(&game->center_y, column_chunk_of(cell_y), memory_order_relaxed);
}

void game_fly(game_t *game, const render_fly_input_t *input, float32_t seconds) {
    render_fly_input_t scaled = *input;
    scaled.mouse_dx = game_view_look_scaled(input->mouse_dx, game->config.sensitivity_percent, &game->look_rest[0]);
    scaled.mouse_dy = game_view_look_scaled(input->mouse_dy, game->config.sensitivity_percent, &game->look_rest[1]);
    render_camera_fly(&game->camera, &scaled, seconds);
    game_center_on_eye(game);
}

/* The eye goes where the ticks have got to, by now_ns. */
static void game_walk_view(game_t *game, int64_t now_ns) {
    player_eye_at(game->borrowed_player, now_ns, game->camera.position);
    game_center_on_eye(game);
}

bool game_screen_input(game_t *game, const input_frame_t *frame) {
    if (!game_has_screens(game)) {
        return false;
    }
    return ui_screen_input(game->borrowed_ui, frame, game->config.width, game->config.height);
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

/* What the card's clock said, on the host's timeline: end to end, the last ending now, as the wait for it has just come back. See
 * run/src/profiler/README.md. */
static void game_kernels_note(game_t *game, const frame_timings_t *timings) {
    profiler_lane_t *const lane = game->borrowed_lane_card;
    if (lane == nullptr) {
        return;
    }
    int64_t start_ns = time_now_ns() - (int64_t)((float64_t)timings->total_ms * (float64_t)TIME_NS_PER_MS);
    for (uint32_t i = 0u; i < FRAME_KERNEL_COUNT; i++) {
        if ((timings->kernels_ran & (1u << i)) != 0u) {
            const int64_t duration_ns = (int64_t)((float64_t)timings->kernel_ms[i] * (float64_t)TIME_NS_PER_MS);
            profiler_span_add(lane, frame_kernel_name((frame_kernel_t)i), start_ns, duration_ns);
            start_ns += duration_ns;
        }
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
    PROFILER_BEGIN(game->borrowed_lane_gpu, "frame_draw");
    const gpu_status_t status = render_frame_draw(&render->frame, &render->buffers, camera);
    PROFILER_END(game->borrowed_lane_gpu);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    game_kernels_note(game, &render->frame.last_timings);
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
static void game_hud_trouble_draw(game_t *game) {
    const save_t *const save = game->borrowed_save;
    if (save == nullptr || save->world.open == 0u) {
        return;
    }
    const char *const trouble = save_autosave_trouble(&save->autosave);
    if (trouble != nullptr) {
        char text[UI_HUD_NOTICE_BYTES];
        (void)snprintf(text, sizeof(text), "Not saving: %s", trouble);
        ui_hud_notice_draw(&game->borrowed_ui->canvas, &game->borrowed_ui->font, text);
    }
}

static const uint8_t *game_hud_compose(game_t *game, const uint8_t *drawn) {
    ui_t *const ui = game->borrowed_ui;
    const bool walking = game->borrowed_player != nullptr;
    const bool screen_up = game_has_screens(game) && ui_screen_up(ui);
    const uint8_t *const picture = game_picture_full(game, drawn);
    if (ui == nullptr || (!walking && game->hud_debug == 0u && !screen_up)) {
        return picture;
    }
    if (!ui_frame_begin_over(ui, picture, game->config.width, game->config.height)) {
        return picture;
    }
    if (walking) {
        ui_hud_state_t state;
        game_hud_state_read(game, &state);
        ui_hud_draw(&ui->hud, &ui->canvas, &ui->font, &state);
        if (player_vitals_read(game->borrowed_player).state == (uint8_t)PLAYER_SURVIVAL_DEAD) {
            /* There is no death screen, and the view on the ground says nothing of how to get up. This comes before a full disk: a person
             * who cannot get up can do nothing about that. */
            ui_hud_notice_draw(&ui->canvas, &ui->font, "You died. Press a mouse button or Space to come back.");
        } else {
            game_hud_trouble_draw(game);
        }
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
    render_fly_input_t input;
    game_fly_from_script(tick->state, &input);
    /* Time here is the count of ticks, so one script gives one path. */
    const int64_t tick_ns = (int64_t)(game->tick_count + 1u) * GAME_TICK_NS;
    game_day_advance(game, 1.0f32 / (float32_t)MC_GAME_TICK_HZ);
    game_fps_note(game, 0u, 1.0f32 / (float32_t)MC_GAME_TICK_HZ);
    game_debug_send(game, display_script_state_is_down(tick->state, KEY_F3));
    if (game->borrowed_player == nullptr) {
        game_fly(game, &input, 1.0f32 / (float32_t)MC_GAME_TICK_HZ);
        game_sim_tick(game, tick_ns);
        return true;
    }
    /* A draw settles too, but a script need not draw every tick, and the body would then wait for ground that never comes. */
    if (!game_settle(game)) {
        return false;
    }
    input_frame_t frame;
    game_input_from_script(tick, &frame);
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
    game_day_apply(game);
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
    const bool ok = game_scene_draw_timed(game, frame);
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
    game_day_apply(game);
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
        game_day_apply(game);
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
    if (!thread_start(&game->sim_thread, game->borrowed_topology, THREAD_ROLE_SIM, 0u, game_sim_thread, game)) {
        game_lanes_threads(game, false);
        return false; /* it has said why */
    }
    if (!thread_start(&game->gpu_thread, game->borrowed_topology, THREAD_ROLE_GPU, 0u, game_gpu_thread, game)) {
        atomic_store_explicit(&game->stop, 1u, memory_order_release);
        thread_join(&game->sim_thread);
        game_lanes_threads(game, false);
        return false; /* it has said why */
    }
    game->threads_running = 1u;
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

bool game_screen_up(game_t *game) {
    return game_has_screens(game) && ui_screen_up(game->borrowed_ui);
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

void game_pause_set(game_t *game, bool paused) {
    atomic_store_explicit(&game->paused, paused ? 1u : 0u, memory_order_relaxed);
    game->last_frame_ns = time_now_ns();
}

void game_loop_discard(void *context) {
    game_t *const game = context;
    input_test_pattern_discard(game->borrowed_input);
    /* The time away is not time spent flying. */
    game->last_frame_ns = time_now_ns();
}
