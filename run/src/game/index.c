/* See README.md. The Owner of the World, Tick and Frame arenas, and the loop connecting the sim, gpu and main threads. Later
 * stages add their tick and frame calls at the places marked below. */
#include "game/index.h"

#include "assert/index.h"
#include "item/stack/index.h"
#include "game/view/index.h"
#include "log/index.h"
#include "render/entities/index.h"
#include "tables/blocks/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

enum : uint64_t {
    /* Budgets of the two arenas with a lifetime shorter than a world's. Nothing uses them yet: Stages 11 to 15 do. */
    GAME_TICK_ARENA_BYTES = 64ull << 20,
    GAME_FRAME_ARENA_BYTES = 64ull << 20,
    /* A whole column of states, section after section, each in section_voxel_index order. */
    GAME_COLUMN_VOXELS = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS,
    GAME_COLUMN_BYTES = GAME_COLUMN_VOXELS * sizeof(uint16_t),
    GAME_WORLD_ARENA_BYTES = GAME_STREAM_JOBS * (GAME_COLUMN_BYTES + GAME_COLUMN_VOXELS + sizeof(game_column_job_t)) +
                             2ull * 4096ull + MOB_SPAWN_BYTES,
};
enum : int64_t {
    GAME_SIM_IDLE_NS = 1000000,  /* the sim looks for finished columns this often when it has nothing to do */
    GAME_GPU_IDLE_NS = 100000,   /* the gpu thread waits this long for main to take the picture it just made */
    GAME_SETTLE_IDLE_NS = 50000, /* a run with no threads waits this long for a worker */
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

void game_mob_set(game_t *game, mob_t *mob) {
    ASSERT_ALWAYS(game->open == 0u);
    game->borrowed_mob = mob;
}

void game_item_set(game_t *game, item_t *item) {
    ASSERT_ALWAYS(game->open == 0u);
    game->borrowed_item = item;
}

void game_deinit(game_t *game) {
    if (game->threads_running != 0u) {
        (void)game_loop_stop(game); /* it has said what went wrong, and there is no one left to tell */
    }
    game_close(game);
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
    if (render_open(render, &world->snapshot, render_pool_bytes_for(render, world->map.slot_count), budget) !=
        GPU_STATUS_OK) {
        world_close(world);
        return false;
    }
    if (render_open_frame(render, game->borrowed_texture, view->width, view->height, view->fog_end) != GPU_STATUS_OK) {
        render_close(render);
        world_close(world);
        return false;
    }
    if (view->lod.level_count != 0u) {
        const gpu_status_t status =
            render_open_lod(render, &view->lod, render_lod_sample_terrain, game->borrowed_terrain);
        if (status != GPU_STATUS_OK) {
            render_close(render);
            world_close(world);
            return false;
        }
    }
    render_frame_features_set(&render->frame, view->features);
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
        if (game->open_alive != nullptr) {
            game->open_alive(game->borrowed_open_alive_context);
        }
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
        job->borrowed_states = arena_alloc(&game->arena_world, GAME_COLUMN_BYTES, alignof(uint64_t));
        job->borrowed_opened = arena_alloc(&game->arena_world, GAME_COLUMN_VOXELS, alignof(uint64_t));
    }

    /* Flying: a little above whatever is at the world's middle, water included, looking out and down. Walking: on dry land near it. */
    int32_t start_x = 8, start_y = 8;
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
        render_open_lod(render, &now->lod, render_lod_sample_terrain, game->borrowed_terrain) != GPU_STATUS_OK) {
        /* The world still works, to where full detail goes, and the fog comes in to meet it. */
        LOG_WARN("game: no room on the card for a far field, so the world stops at %" PRIu32 " chunks",
                 now->full_detail);
        now->lod = (render_lod_plan_t){0};
        now->fog_end = game_view_fog_end(now->full_detail);
    }
    render_frame_fog_set(&render->frame, now->fog_end);
    render_frame_features_set(&render->frame, now->features);
    return true;
}

bool game_reconfigure(game_t *game, const game_config_t *config) {
    ASSERT_ALWAYS(game->open != 0u);
    ASSERT_ALWAYS(config->width == game->config.width && config->height == game->config.height);
    ASSERT_ALWAYS(config->render_distance != 0u && config->render_distance <= MC_GAME_RENDER_DISTANCE_MAX);
    game->camera.half_fov_tangent = game_view_half_fov_tangent(config->fov_degrees);
    atomic_store_explicit(&game->uncapped, config->uncapped, memory_order_relaxed);
    game_view_t now;
    const uint32_t table_chunks = game->borrowed_world->map.table_radius - WORLD_MAP_KEEP_EXTRA;
    game_view_work_out(config, table_chunks, &now);
    if (memcmp(&now, &game->view, sizeof(now)) == 0) {
        game->config = *config;
        return true;
    }
    const bool running = game->threads_running != 0u;
    if (running && !game_loop_stop(game)) {
        return false; /* a thread has said why it failed */
    }
    const bool changed = game_view_change(game, &now);
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
    (void)worker;
    game_column_job_t *const job = borrowed_job;
    const terrain_density_box_t box = game_column_box(job->request.chunk_x * (int32_t)COLUMN_SIZE,
                                                      job->request.chunk_y * (int32_t)COLUMN_SIZE, COLUMN_SIZE);
    terrain_generate_box(job->borrowed_terrain, &box, job->borrowed_opened, job->borrowed_states);
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

static void game_spill(void *borrowed_game, const item_stack_t *stack, const int32_t cell[3]);

/* What the screens need to know of this moment, on the sim. */
static void game_screen_sim(game_t *game, ui_screen_sim_t *out) {
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

/* Whether a game has screens at all: they move what is carried, so they need something carried, and a body to carry it. */
static bool game_has_screens(const game_t *game) {
    return game->borrowed_ui != nullptr && game->borrowed_item != nullptr && game->borrowed_player != nullptr;
}

void game_sim_step(game_t *game) {
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
        (void)world_light_column_leaving(&world->light, leaving.slot);
        world_tick_column_leaving(&world->tick, leaving.slot);
        world_map_unload(map, leaving.slot); /* nothing to save yet: Stage 16.1 looks here first */
    }
    game_columns_commit(game);
    game_columns_request(game);
    (void)world_light_update(&world->light);
    (void)render_upload_resend_drain(&game->borrowed_render->upload, &world->edit);
    /* False is only that nothing changed. */
    (void)world_edit_snapshot_publish(&world->snapshot);
}

/* An item picked up goes to the inventory, or with none to the hand. What does not fit stays on the ground. */
static uint32_t game_pickup_take(void *borrowed_game, uint16_t item, uint16_t wear, uint32_t count) {
    game_t *const game = borrowed_game;
    if (game->borrowed_item != nullptr) {
        inventory_t *const inventory = &game->borrowed_item->inventory;
        const uint16_t id = block_drop_items[item].item;
        if (wear == 0u) {
            return inventory_add(inventory, id, count);
        }
        /* What wears stacks to one, while a drop may be several as worn as each other. */
        uint32_t taken = 0u;
        while (taken < count) {
            item_stack_t one = item_stack_make(id, 1u);
            one.wear = wear;
            if (inventory_add_stack(inventory, &one) == 0u) {
                break;
            }
            taken++;
        }
        return taken;
    }
    return player_interaction_hand_take(&game->borrowed_player->interaction, item, count);
}

/* The hands place from the hotbar slot in hand, and know nothing of it: they see its block, or nothing if it places none. */
static void game_hand_from_hotbar(game_t *game) {
    const item_stack_t *const held = inventory_selected(&game->borrowed_item->inventory);
    const uint16_t block = item_defs[held->item].block;
    player_interaction_hand_set(&game->borrowed_player->interaction, block == ITEM_NO_BLOCK ? BLOCK_ID_AIR : block,
                                block == ITEM_NO_BLOCK ? 0u : held->count);
    player_survival_hold_food(&game->borrowed_player->survival, held->item, item_defs[held->item].food,
                              item_defs[held->item].saturation);
}

/* Mining counts as what is in hand. A different tool starts the block again, so that the last touch cannot be the one that wears least. */
static void game_tool_from_hotbar(game_t *game) {
    player_interaction_t *const hands = &game->borrowed_player->interaction;
    uint8_t tool = BLOCK_TOOL_NONE, tier = BLOCK_TIER_NONE;
    equipment_tool_held(&game->borrowed_item->inventory, &tool, &tier);
    const uint32_t speed = equipment_tool_speed_milli(inventory_selected(&game->borrowed_item->inventory)->item);
    if (hands->held_tool != tool || hands->held_tier != tier || hands->held_speed_milli != speed) {
        hands->progress = 0u;
        player_interaction_hold_tool(hands, tool, tier);
        player_interaction_hold_speed(hands, speed);
    }
}

/* The right button with no left, as for placing. Only a look: player_interact_tick takes the latch next. A press and release that fall
 * between the two reach placing alone, and the next press is not far behind. */
static bool game_use_held(const player_t *player, int64_t tick_ns) {
    if (tick_ns - atomic_load_explicit(&player->intent_sent_ns, memory_order_relaxed) > PLAYER_INTENT_STALE_NS) {
        return false;
    }
    const uint32_t hands = atomic_load_explicit(&player->hands_word, memory_order_relaxed) |
                           atomic_load_explicit(&player->hands_latch, memory_order_relaxed);
    return (hands & PLAYER_HAND_PLACE) != 0u && (hands & PLAYER_HAND_ATTACK) == 0u;
}

/* What the hands broke this tick wears what broke it, and shears bring the block itself out as well. */
static void game_tool_after_break(game_t *game, uint16_t held_item) {
    const player_interaction_t *const hands = &game->borrowed_player->interaction;
    item_t *const item = game->borrowed_item;
    const uint16_t own = equipment_own_drop(held_item, hands->last_mined);
    if (own != ITEM_ID_NONE && game->borrowed_entity != nullptr) {
        const float64_t middle[3] = {(float64_t)hands->cell[0] + 0.5f64, (float64_t)hands->cell[1] + 0.5f64,
                                     (float64_t)hands->cell[2] + 0.5f64};
        item_drop_spawn(&game->borrowed_entity->item_drops, item_stack_drop_row(own), 1u, middle);
    }
    (void)equipment_tool_block_broken(&item->equipment, &item->inventory, hands->last_mined);
}

/* The right button on a block with a screen brings it up, as it goes down. A held button that comes to rest on one does not, or a screen
 * just closed would come straight back. While a screen is up, or the button is on such a block, the hands do nothing: a chest is not built on,
 * and sneaking is how to build on one. */
static void game_screen_hands(game_t *game, int64_t tick_ns) {
    player_t *const player = game->borrowed_player;
    ui_t *const ui = game->borrowed_ui;
    const raycast_hit_t *const hit = &player->target.hit;
    const bool held = game_use_held(player, tick_ns);
    const bool pressed = held && game->use_was_held == 0u;
    /* main sends no buttons while a screen is up, and one still held as it goes is no new press. */
    game->use_was_held = held || ui_screen_sim_kind(ui) != UI_SCREEN_NONE ? 1u : 0u;
    const bool sneaking = player_sneak_sent(player);
    const ui_screen_kind_t kind =
        hit->result == RAYCAST_RESULT_HIT && !sneaking ? ui_screen_kind_of_block(hit->block) : UI_SCREEN_NONE;
    if (pressed && kind != UI_SCREEN_NONE && !player_survival_holds_body(&player->survival)) {
        (void)ui_screen_sim_open(ui, game->borrowed_item, kind, hit->cell);
    }
    player->hands_held_off = ui_screen_sim_kind(ui) != UI_SCREEN_NONE || (held && kind != UI_SCREEN_NONE) ? 1u : 0u;
}

/* One tick of the hands: which slot is in hand first, so that a key and a click in one tick act in that order, then what the right button
 * does with armor or a hoe; what they placed comes off the slot, and what they broke wears it. */
static void game_interact_tick(game_t *game, int64_t tick_ns) {
    player_t *const player = game->borrowed_player;
    item_t *const item = game->borrowed_item;
    if (item == nullptr) {
        player_interact_tick(player, &game->borrowed_world->edit, tick_ns);
        return;
    }
    item_hotbar_apply(item);
    if (game_has_screens(game)) {
        game_screen_hands(game, tick_ns);
    }
    const raycast_hit_t *const hit = &player->target.hit;
    /* A press that brings a body back or wakes it is used up by that. */
    equipment_use_tick(&item->equipment, &item->inventory, &game->borrowed_world->edit,
                       game_use_held(player, tick_ns) && !player_survival_holds_body(&player->survival) &&
                           player->hands_held_off == 0u,
                       hit->result == RAYCAST_RESULT_HIT ? hit->cell : nullptr);
    game_hand_from_hotbar(game);
    game_tool_from_hotbar(game);
    const uint32_t before = player->interaction.hand_count;
    const uint64_t mined_before = player->interaction.mined_count;
    const uint64_t eaten = player->survival.eaten_count;
    const uint16_t held_item = inventory_selected(&item->inventory)->item;
    player_interact_tick(player, &game->borrowed_world->edit, tick_ns);
    (void)inventory_selected_take(&item->inventory, before - (uint32_t)player->interaction.hand_count);
    if (player->interaction.mined_count != mined_before) {
        game_tool_after_break(game, held_item);
    }
    if (player->survival.eaten_count != eaten) {
        (void)inventory_selected_take(&item->inventory, 1u);
        /* What a stew came in stays in hand. A stew stacks to one, so the slot it left is empty. */
        if (held_item == ITEM_ID_MUSHROOM_STEW) {
            *inventory_slot(&item->inventory, item->inventory.selected) = item_stack_make(ITEM_ID_BOWL, 1u);
            item->inventory.added_count++;
        }
    }
}

/* What a broken chest or furnace held comes out where it stood. With nothing to lie on, as in a run with no entities, it is gone. */
static void game_spill(void *borrowed_game, const item_stack_t *stack, const int32_t cell[3]) {
    game_t *const game = borrowed_game;
    if (game->borrowed_entity != nullptr) {
        const float64_t middle[3] = {(float64_t)cell[0] + 0.5f64, (float64_t)cell[1] + 0.5f64,
                                     (float64_t)cell[2] + 0.5f64};
        item_drop_spawn_worn(&game->borrowed_entity->item_drops, item_stack_drop_row(stack->item), stack->wear,
                             stack->count, middle);
    }
}

static void game_drops_spawn(game_t *game) {
    player_drop_t drop;
    while (player_interaction_drop_next(&game->borrowed_player->interaction, &drop)) {
        item_drop_spawn(&game->borrowed_entity->item_drops, drop.item, drop.count, drop.position);
    }
}

/* An arrow has hit the player: armor and a moment's invulnerability are the body's own business, and a blow it did not feel moves it not. */
static void game_arrow_hurt(void *borrowed_context, float32_t amount, const float64_t direction[COLLISION_AXES]) {
    player_t *const player = ((game_t *)borrowed_context)->borrowed_player;
    if (player_survival_hurt(&player->survival, amount, PLAYER_SURVIVAL_CAUSE_ATTACK) > 0.0f32) {
        projectile_knockback(player->movement.velocity, direction, player->movement.on_ground != 0u);
    }
}

/* A body that is down is no more to be aimed at than to be hit. */
static bool game_player_is_a_target(const player_t *player) {
    return player->survival.state != PLAYER_SURVIVAL_DEAD && player->survival.placing == 0u;
}

static float64_t game_player_height(const player_t *player) {
    return player->movement.crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING;
}

/* The skeletons think about where the player is now, and shoot. Before the bodies move, as their walking is a wish for this tick. */
static void game_skeletons_tick(game_t *game) {
    if (game->borrowed_mob == nullptr) {
        return;
    }
    const player_t *const player = game->borrowed_player;
    const player_movement_t *const movement = &player->movement;
    const mob_skeleton_view_t view = {
        .position = {movement->position[0], movement->position[1], movement->position[2]},
        .eye_height = movement->eye_height,
        .height = game_player_height(player),
        .alive = (uint8_t)(game_player_is_a_target(player) ? 1u : 0u),
    };
    mob_skeleton_tick(&game->borrowed_mob->skeleton, &game->borrowed_mob->path, game->borrowed_entity,
                      &game->borrowed_world->map, &view);
}

static void game_arrows_tick(game_t *game) {
    entity_t *const entity = game->borrowed_entity;
    const player_t *const player = game->borrowed_player;
    collision_box_t box;
    player_movement_box(player->movement.position, game_player_height(player), &box);
    projectile_tick(&entity->projectiles, &entity->store, &entity->grid, &game->borrowed_world->map,
                    game_player_is_a_target(player) ? &box : nullptr, game_arrow_hurt, game);
}

/* What the hands just broke, and what went by itself, comes out into the world, everything there moves, and the body picks up what it
 * now reaches. One block at a time out of the tick's ring: all of it at once could be more than the hands' ring holds. */
static void game_items_tick(game_t *game) {
    entity_t *const entity = game->borrowed_entity;
    player_t *const player = game->borrowed_player;
    game_drops_spawn(game);
    world_tick_broken_t broken;
    while (world_tick_broken_next(&game->borrowed_world->tick, &broken)) {
        player_interaction_drops_spawn(&player->interaction, broken.block, broken.x, broken.y, broken.z);
        game_drops_spawn(game);
    }
    /* Before the tick, so that the grid it builds knows what came and went. */
    const mob_spawn_view_t view = {
        .position = {player->movement.position[0], player->movement.position[1], player->movement.position[2]},
        .day_fraction = player->survival.day_fraction,
    };
    mob_spawn_tick(&game->mob_spawn, &entity->store, entity->borrowed_physics, &game->borrowed_world->map, &view);
    game_skeletons_tick(game);
    if (game->borrowed_mob != nullptr) {
        const mob_creeper_scene_t scene = {
            .borrowed_entity = entity,
            .borrowed_path = &game->borrowed_mob->path,
            .borrowed_edit = &game->borrowed_world->edit,
            .borrowed_movement = &player->movement,
            .borrowed_survival = &player->survival,
        };
        mob_creeper_tick(&game->borrowed_mob->creeper, &scene);
    }
    entity_tick(entity, &game->borrowed_world->map);
    game_arrows_tick(game);
    collision_box_t reach;
    player_pickup_reach(player, &reach);
    /* A dead body takes nothing, or it would take back what it just spilled. */
    if (!player_survival_holds_body(&player->survival)) {
        (void)item_drop_pickup(&entity->item_drops, &reach, game_pickup_take, game);
    }
    /* What was just picked up may be what the hand holds, and whoever looks before the next tick should see so. */
    if (game->borrowed_item != nullptr) {
        game_hand_from_hotbar(game);
    }
}

/* A body that has died leaves what it carried where it lies, what it wore and what was in its crafting grids too, each as worn as it was.
 * With no inventory there is nothing to leave but the one-slot hand of a test, which it keeps. */
static void game_death_spill(game_t *game) {
    item_t *const item = game->borrowed_item;
    if (item == nullptr) {
        return;
    }
    const player_movement_t *const movement = &game->borrowed_player->movement;
    const int32_t cell[3] = {(int32_t)floor(movement->position[0]), (int32_t)floor(movement->position[1]),
                             (int32_t)floor(movement->position[2])};
    for (uint32_t slot = 0u; slot < INVENTORY_SLOTS; slot++) {
        const item_stack_t *const stack = inventory_slot_read(&item->inventory, slot);
        if (!item_stack_is_empty(stack)) {
            item_stack_t out = {.item = ITEM_ID_NONE, .wear = 0u, .count = 0u, ._pad0 = {0}};
            (void)inventory_slot_move_out(&item->inventory, slot, &out, stack->count);
            game_spill(game, &out, cell);
        }
    }
    for (uint32_t slot = 0u; slot < EQUIPMENT_ARMOR_SLOTS; slot++) {
        item_stack_t *const stack = &item->equipment.armor[slot];
        if (!item_stack_is_empty(stack)) {
            game_spill(game, stack, cell);
            *stack = (item_stack_t){0};
        }
    }
    crafting_grid_t *const grids[2] = {&item->crafting_hands, &item->crafting_table};
    for (uint32_t g = 0u; g < 2u; g++) {
        for (uint32_t c = 0u; c < crafting_grid_cells(grids[g]); c++) {
            item_stack_t *const stack = crafting_grid_cell(grids[g], c);
            if (!item_stack_is_empty(stack)) {
                game_spill(game, stack, cell);
                memset(stack, 0, sizeof(*stack));
            }
        }
        crafting_grid_changed(grids[g]);
    }
}

/* One tick of what keeps the body going, and what that means for everything else. */
static void game_survive_tick(game_t *game) {
    player_t *const player = game->borrowed_player;
    item_t *const item = game->borrowed_item;
    if (item != nullptr) {
        player_survival_armor_set(&player->survival, (float32_t)equipment_armor_points(&item->equipment),
                                  (float32_t)equipment_armor_toughness(&item->equipment));
    }
    player_survive_tick(player, &game->borrowed_world->map);
    const float32_t armor_hit = player_survival_armor_hit_take(&player->survival);
    if (item != nullptr && armor_hit > 0.0f32) {
        (void)equipment_armor_hurt(&item->equipment, armor_hit);
    }
    if (player_survival_died_take(&player->survival)) {
        /* What a screen had out goes back first, so that it comes out with the rest. */
        if (game_has_screens(game)) {
            ui_screen_sim_t sim;
            game_screen_sim(game, &sim);
            ui_screen_sim_close(game->borrowed_ui, &sim);
        }
        game_death_spill(game);
    }
    if (player_survival_morning_take(&player->survival)) {
        atomic_store_explicit(&game->morning_latch, 1u, memory_order_relaxed);
    }
}

/* What the next frames draw: every item as it now lies, what is aimed at and how far along mining it is. Nobody draws anything before
 * a frame is open, and with neither a body nor items there is nothing to say. */
static void game_entities_publish(game_t *game) {
    render_entities_t *const entities = game->borrowed_render->borrowed_entities;
    if (entities == nullptr || (game->borrowed_entity == nullptr && game->borrowed_player == nullptr)) {
        return;
    }
    render_entities_begin(entities);
    if (game->borrowed_entity != nullptr) {
        const world_map_t *const map = &game->borrowed_world->map;
        const item_drop_store_t *const drops = &game->borrowed_entity->item_drops;
        for (uint32_t i = 0u; i < drops->count; i++) {
            const item_drop_t *const drop = &drops->borrowed_items[i];
            uint8_t stored = SHADE_LIGHT_OPEN_SKY;
            /* A column not loaded holds no light, and whatever it is, it is not drawn. */
            if (world_map_light_get(map, (int32_t)floor(drop->position[0]), (int32_t)floor(drop->position[1]),
                                    (int32_t)floor(drop->position[2]), &stored) != WORLD_MAP_STATUS_OK) {
                stored = SHADE_LIGHT_OPEN_SKY;
            }
            render_entities_add_item_drop(entities, drop->item, drop->position, (float32_t)ITEM_DROP_SIZE, drop->age,
                                          stored);
        }
        /* An arrow is drawn as the item it is, not turning. */
        const projectile_store_t *const arrows = &game->borrowed_entity->projectiles;
        for (uint32_t i = 0u; i < arrows->count; i++) {
            const projectile_t *const arrow = &arrows->borrowed_items[i];
            uint8_t stored = SHADE_LIGHT_OPEN_SKY;
            if (world_map_light_get(map, (int32_t)floor(arrow->position[0]), (int32_t)floor(arrow->position[1]),
                                    (int32_t)floor(arrow->position[2]), &stored) != WORLD_MAP_STATUS_OK) {
                stored = SHADE_LIGHT_OPEN_SKY;
            }
            render_entities_add_item_drop(entities, item_stack_drop_row(ITEM_ID_ARROW), arrow->position,
                                          (float32_t)ITEM_DROP_SIZE, 0u, stored);
        }
    }
    if (game->borrowed_player != nullptr) {
        const player_t *const player = game->borrowed_player;
        render_entities_target_t target;
        memset(&target, 0, sizeof(target));
        const raycast_hit_t *const hit = &player->target.hit;
        if (hit->result == RAYCAST_RESULT_HIT) {
            target.outline = 1u;
            memcpy(target.box_min, hit->bounds.min, sizeof(target.box_min));
            memcpy(target.box_max, hit->bounds.max, sizeof(target.box_max));
        }
        target.crack_frame = render_entities_crack_frame(player_interaction_progress(&player->interaction));
        memcpy(target.crack_cell, player->interaction.cell, sizeof(target.crack_cell));
        render_entities_target_set(entities, &target);
    }
    render_entities_publish(entities);
}

/* A screen whose block has gone goes with it, and with no one asking main is told how things are, as a furnace burns. */
static void game_screen_tick(game_t *game) {
    ui_t *const ui = game->borrowed_ui;
    ui_screen_sim_t sim;
    game_screen_sim(game, &sim);
    const ui_screen_kind_t kind = ui_screen_sim_kind(ui);
    if (kind != UI_SCREEN_NONE && kind != UI_SCREEN_INVENTORY) {
        const int32_t *const cell = ui_screen_sim_cell(ui);
        uint16_t block = BLOCK_ID_AIR;
        if (world_map_block_get(&game->borrowed_world->map, cell[0], cell[1], cell[2], &block) != WORLD_MAP_STATUS_OK ||
            ui_screen_kind_of_block(block) != kind) {
            ui_screen_sim_close(ui, &sim);
        }
    }
    ui_screen_sim_step(ui, &sim, true);
}

/* One tick of simulated time, due at tick_ns: this is where Stages 11 to 15 add theirs. */
/* What the HUD shows of the inventory goes to main as it stands after this tick. */
static void game_hud_publish(game_t *game) {
    if (game->borrowed_item == nullptr || game->borrowed_item->inventory.borrowed_slots == nullptr) {
        return;
    }
    const inventory_t *const inventory = &game->borrowed_item->inventory;
    for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
        uint64_t word = 0u;
        memcpy(&word, inventory_slot_read(inventory, slot), sizeof(word));
        atomic_store_explicit(&game->hud_hotbar[slot], word, memory_order_relaxed);
    }
    atomic_store_explicit(&game->hud_selected, inventory->selected, memory_order_relaxed);
}

/* What every mob does, and what comes of it for the body. Before the bodies move and before the body's own tick, whose armor and death
 * logic then see the blow. */
static void game_mobs_tick(game_t *game) {
    player_t *const player = game->borrowed_player;
    const player_movement_t *const movement = &player->movement;
    const mob_zombie_view_t view = {
        .position = {movement->position[0], movement->position[1], movement->position[2]},
        .eye = movement->eye_height,
        .half_width = PLAYER_MOVEMENT_WIDTH * 0.5f64,
        .height = movement->crouched != 0u ? PLAYER_MOVEMENT_HEIGHT_CROUCHED : PLAYER_MOVEMENT_HEIGHT_STANDING,
        .alive = player->survival.state != PLAYER_SURVIVAL_DEAD && player->survival.placing == 0u ? 1u : 0u,
    };
    const float32_t blow = mob_minds_tick(game->borrowed_mob, game->borrowed_entity, &view);
    if (blow > 0.0f32) {
        (void)player_survival_hurt(&player->survival, blow, PLAYER_SURVIVAL_CAUSE_ATTACK);
    }
}

static void game_sim_tick(game_t *game, int64_t tick_ns) {
    arena_reset(&game->arena_tick);
    if (game->borrowed_player != nullptr) {
        player_survival_day_set(&game->borrowed_player->survival,
                                (float64_t)atomic_load_explicit(&game->day_units, memory_order_relaxed) /
                                    4294967296.0f64);
        /* First, so that the body and its next aim both meet the world as the hands leave it. */
        game_interact_tick(game, tick_ns);
        if (game_has_screens(game)) {
            game_screen_tick(game);
        }
        if (game->borrowed_item != nullptr) {
            item_world_tick(game->borrowed_item, &game->borrowed_world->edit, game_spill, game);
        }
        /* Before the bodies move, so that what it answers is of the world as the hands left it. */
        if (game->borrowed_mob != nullptr) {
            mob_tick(game->borrowed_mob);
        }
        if (game->borrowed_entity != nullptr) {
            if (game->borrowed_mob != nullptr) {
                game_mobs_tick(game);
            }
            game_items_tick(game);
        }
        player_tick(game->borrowed_player, &game->borrowed_world->map, tick_ns);
        game_survive_tick(game);
    }
    world_tick_step(&game->borrowed_world->tick);
    game_entities_publish(game);
    game_hud_publish(game);
    game->tick_count++;
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

void game_walk_send(game_t *game, const render_fly_input_t *input, bool jump, bool sneak, int64_t now_ns) {
    player_camera_look(&game->camera,
                       game_view_look_scaled(input->mouse_dx, game->config.sensitivity_percent, &game->look_rest[0]),
                       game_view_look_scaled(input->mouse_dy, game->config.sensitivity_percent, &game->look_rest[1]));
    player_look_send(game->borrowed_player, game->camera.yaw, game->camera.pitch);
    const player_intent_t intent = {.yaw = game->camera.yaw,
                                    .forward = input->forward,
                                    .right = input->right,
                                    .jump = jump ? 1u : 0u,
                                    .sneak = sneak ? 1u : 0u,
                                    .sprint = input->fast,
                                    ._pad0 = {0}};
    player_send(game->borrowed_player, &intent, now_ns);
}

void game_hands_send(game_t *game, bool attack, bool place) {
    player_hands_send(game->borrowed_player, attack, place);
}

void game_hotbar_send(game_t *game, const bool keys[INVENTORY_HOTBAR_SLOTS], int64_t wheel) {
    item_t *const item = game->borrowed_item;
    if (item == nullptr) {
        return;
    }
    for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
        if (keys[slot]) {
            item_hotbar_send(item, slot);
        }
    }
    /* Round the ends comes to the same, so that a wheel never overflows what it goes through. */
    item_hotbar_scroll_send(item, -(int32_t)(wheel % (int64_t)INVENTORY_HOTBAR_SLOTS));
}

/* The eye goes where the ticks have got to, by now_ns. */
static void game_walk_view(game_t *game, int64_t now_ns) {
    player_eye_at(game->borrowed_player, now_ns, game->camera.position);
    game_center_on_eye(game);
}

/* ---- A run on one thread ------------------------------------------------------------------------------------------ */

/* Whether every section the eye's place wants is on the card, or as much of it as the pool will take. */
static bool game_is_settled(const game_t *game) {
    const world_t *const world = game->borrowed_world;
    const render_t *const render = game->borrowed_render;
    const world_edit_snapshot_t *const snapshot = &world->snapshot;
    const bool unread = snapshot->published_count != snapshot->acquired_count + snapshot->overwritten_count;
    const bool sections_waiting =
        world->edit.queue_count[WORLD_EDIT_CONSUMER_RENDER] != 0u && render->buffers.pool_full_count == 0u;
    return game->stream_quiet != 0u && world->map.pending_count == 0u && !unread && !sections_waiting &&
           render_upload_idle(&render->upload) &&
           (render->borrowed_lod == nullptr || render_lod_idle(render->borrowed_lod));
}

bool game_settle(game_t *game) {
    render_t *const render = game->borrowed_render;
    for (;;) {
        const uint32_t waiting_before = game->jobs_in_use;
        game_sim_step(game);
        const gpu_status_t status = render_upload_frame(&render->upload);
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&render->buffers.last_error);
            return false;
        }
        const uint64_t far_built = render->borrowed_lod != nullptr ? render->borrowed_lod->stats.columns_built : 0u;
        if (render->borrowed_lod != nullptr &&
            render_lod_frame(render->borrowed_lod, game->camera.position) != GPU_STATUS_OK) {
            return false; /* it has said why */
        }
        if (render->buffers.pool_full_count != 0u && game->pool_full_said == 0u) {
            LOG_WARN("game: the section pool is full, so part of the world will not be drawn");
            game->pool_full_said = 1u;
        }
        if (game_is_settled(game)) {
            return true;
        }
        const bool far_waiting = render->borrowed_lod != nullptr && render->borrowed_lod->jobs_in_use != 0u &&
                                 far_built == render->borrowed_lod->stats.columns_built;
        if (far_waiting || (waiting_before != 0u && waiting_before == game->jobs_in_use)) {
            time_sleep_ns(GAME_SETTLE_IDLE_NS);
        }
    }
}

bool game_screen_input(game_t *game, const input_frame_t *frame) {
    if (!game_has_screens(game)) {
        return false;
    }
    return ui_screen_input(game->borrowed_ui, frame, game->config.width, game->config.height);
}

void game_input_from_script(const display_headless_tick_t *tick, input_frame_t *out) {
    static_assert(sizeof(tick->state->down) == sizeof(out->down.words));
    memset(out, 0, sizeof(*out));
    memcpy(out->down.words, tick->state->down, sizeof(out->down.words));
    out->mouse_dx = tick->state->tick_dx;
    out->mouse_dy = tick->state->tick_dy;
    out->wheel = tick->state->tick_wheel;
    for (uint32_t i = 0u; i < tick->event_count; i++) {
        const display_script_event_t *const event = &tick->events[i];
        if (event->kind == DISPLAY_SCRIPT_KEY) {
            input_keys_t *const keys = event->pressed != 0u ? &out->pressed : &out->released;
            keys->words[event->code / 64u] |= 1ull << (event->code % 64u);
        }
    }
}

void game_fly_from_script(const display_script_state_t *state, render_fly_input_t *out) {
    memset(out, 0, sizeof(*out));
    out->mouse_dx = state->tick_dx;
    out->mouse_dy = state->tick_dy;
    out->wheel = state->tick_wheel;
    out->forward = (int8_t)((display_script_state_is_down(state, KEY_W) ? 1 : 0) -
                            (display_script_state_is_down(state, KEY_S) ? 1 : 0));
    out->right = (int8_t)((display_script_state_is_down(state, KEY_D) ? 1 : 0) -
                          (display_script_state_is_down(state, KEY_A) ? 1 : 0));
    out->up = (int8_t)((display_script_state_is_down(state, KEY_SPACE) ? 1 : 0) -
                       (display_script_state_is_down(state, KEY_LEFTSHIFT) ? 1 : 0));
    out->fast = display_script_state_is_down(state, KEY_LEFTCTRL) ? 1u : 0u;
}

void game_fly_from_input(const input_frame_t *frame, render_fly_input_t *out) {
    memset(out, 0, sizeof(*out));
    out->mouse_dx = frame->mouse_dx;
    out->mouse_dy = frame->mouse_dy;
    out->wheel = frame->wheel;
    out->forward =
        (int8_t)((input_keys_has(&frame->down, KEY_W) ? 1 : 0) - (input_keys_has(&frame->down, KEY_S) ? 1 : 0));
    out->right =
        (int8_t)((input_keys_has(&frame->down, KEY_D) ? 1 : 0) - (input_keys_has(&frame->down, KEY_A) ? 1 : 0));
    out->up = (int8_t)((input_keys_has(&frame->down, KEY_SPACE) ? 1 : 0) -
                       (input_keys_has(&frame->down, KEY_LEFTSHIFT) ? 1 : 0));
    out->fast = input_keys_has(&frame->down, KEY_LEFTCTRL) ? 1u : 0u;
}

/* ---- What is drawn on a picture ------------------------------------------------------------------------------------- */

static_assert(sizeof(item_stack_t) == sizeof(uint64_t));
static_assert((uint32_t)UI_HUD_STAGE_COARSE == (uint32_t)FRAME_PASS_COARSE);
static_assert((uint32_t)UI_HUD_STAGE_PRIMARY == (uint32_t)FRAME_PASS_PRIMARY);
static_assert((uint32_t)UI_HUD_STAGE_SHADE == (uint32_t)FRAME_PASS_SHADE);
static_assert((uint32_t)UI_HUD_STAGE_POST == (uint32_t)FRAME_PASS_POST);
static_assert((uint32_t)UI_HUD_STAGE_READBACK == (uint32_t)FRAME_PASS_READBACK);
static_assert((uint32_t)UI_HUD_STAGE_GPU_TOTAL == (uint32_t)FRAME_PASS_COUNT);

void game_debug_send(game_t *game, bool down) {
    if (down && game->hud_debug_key == 0u) {
        game->hud_debug ^= 1u;
    }
    game->hud_debug_key = down ? 1u : 0u;
}

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
    const gpu_status_t status = render_frame_draw(&render->frame, &render->buffers, camera);
    if (status != GPU_STATUS_OK) {
        return status;
    }
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

static bool game_scene_tick(void *context, const display_headless_tick_t *tick) {
    game_t *const game = context;
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

static bool game_scene_draw(void *context, const display_headless_tick_t *tick, display_buffer_t *frame) {
    (void)tick;
    game_t *const game = context;
    render_t *const render = game->borrowed_render;
    arena_reset(&game->arena_frame);
    if (!game_settle(game)) {
        return false;
    }
    /* What later stages draw each frame goes in here. */
    game_day_apply(game);
    const gpu_status_t status = game_frame_draw(game, &game->camera);
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&render->frame.gpu.last_error);
        return false;
    }
    const uint8_t *const picture = render_frame_take(&render->frame);
    ASSERT_ALWAYS(picture != nullptr);
    game_fps_note(game, 1u, 0.0f32);
    game->borrowed_last_picture = picture; /* what a pause screen goes over */
    game->view_held = 0u;
    game->view_stale = 0u;
    render_frame_present_full(game_hud_compose(game, picture), frame);
    return true;
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
        gpu_status_t status = render_upload_frame(&render->upload);
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&render->buffers.last_error);
            atomic_store_explicit(&game->failed, 1u, memory_order_release);
            return;
        }
        if (render->borrowed_lod != nullptr) {
            status = render_lod_frame(render->borrowed_lod, camera->position);
            if (status != GPU_STATUS_OK) {
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
            atomic_store_explicit(&game->failed, 1u, memory_order_release);
            return;
        }
    }
}

/* Both threads, from the camera as it stands. False, having said why, with neither running. */
static bool game_threads_start(game_t *game) {
    render_exchange_start(&game->cameras);
    /* Before the threads, so the first camera the gpu thread reads is a real one. */
    for (uint32_t i = 0u; i < RENDER_EXCHANGE_SLOTS; i++) {
        game->camera_slots[i] = game->camera;
    }
    atomic_store_explicit(&game->stop, 0u, memory_order_relaxed);
    if (!thread_start(&game->sim_thread, game->borrowed_topology, THREAD_ROLE_SIM, 0u, game_sim_thread, game)) {
        return false; /* it has said why */
    }
    if (!thread_start(&game->gpu_thread, game->borrowed_topology, THREAD_ROLE_GPU, 0u, game_gpu_thread, game)) {
        atomic_store_explicit(&game->stop, 1u, memory_order_release);
        thread_join(&game->sim_thread);
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
        const uint8_t *const shown = game_hud_compose(game, game->borrowed_last_picture);
        const int64_t composed_ns = time_now_ns();
        render_frame_present_full(shown, target);
        game->hud_compose_us = (uint32_t)((composed_ns - started_ns) / 1000);
        game->hud_present_us = (uint32_t)((time_now_ns() - composed_ns) / 1000);
    }
    return atomic_load_explicit(&game->failed, memory_order_acquire) == 0u;
}

bool game_loop_stop(game_t *game) {
    atomic_store_explicit(&game->stop, 1u, memory_order_release);
    thread_join(&game->sim_thread);
    thread_join(&game->gpu_thread);
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

bool game_loop_input_frame(game_t *game, const input_frame_t *frame, display_buffer_t *back) {
    const bool screen_up = game_screen_input(game, frame);
    /* A tap over between two polls is in pressed and not in down. */
    game_debug_send(game, input_keys_has(&frame->down, KEY_F3) || input_keys_has(&frame->pressed, KEY_F3));
    const int64_t now_ns = time_now_ns();
    const float32_t seconds = (float32_t)time_ns_to_seconds(now_ns - game->last_frame_ns);
    game->last_frame_ns = now_ns;
    render_fly_input_t input;
    game_fly_from_input(frame, &input);
    if (screen_up) {
        /* The body stands, and the mouse is the pointer. Sent all the same, or the sim would take it that nobody is at the controls. */
        memset(&input, 0, sizeof(input));
        game_walk_send(game, &input, false, false, now_ns);
        game_hands_send(game, false, false);
    } else if (game->borrowed_player != nullptr) {
        game_walk_send(game, &input, input_keys_has(&frame->down, KEY_SPACE),
                       input_keys_has(&frame->down, KEY_LEFTSHIFT), now_ns);
        /* A click over between two polls is in pressed and not in down. */
        game_hands_send(game, input_keys_has(&frame->down, BTN_LEFT) || input_keys_has(&frame->pressed, BTN_LEFT),
                        input_keys_has(&frame->down, BTN_RIGHT) || input_keys_has(&frame->pressed, BTN_RIGHT));
        bool hotbar[INVENTORY_HOTBAR_SLOTS];
        for (uint32_t slot = 0u; slot < INVENTORY_HOTBAR_SLOTS; slot++) {
            hotbar[slot] = input_keys_has(&frame->pressed, KEY_1 + slot);
        }
        game_hotbar_send(game, hotbar, frame->wheel);
    }
    return game_loop_frame(game, &input, seconds, back);
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

void game_loop_input_clear(game_t *game) {
    if (game->borrowed_player != nullptr) {
        const render_fly_input_t none = {0};
        game_walk_send(game, &none, false, false, time_now_ns());
        game_hands_send(game, false, false);
    }
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
