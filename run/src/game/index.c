/* See README.md. The Owner of the World, Tick and Frame arenas, and the loop connecting the sim, gpu and main threads. Later
 * stages add their tick and frame calls at the places marked below. */
#include "game/index.h"

#include "assert/index.h"
#include "log/index.h"
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
    GAME_WORLD_ARENA_BYTES =
        GAME_STREAM_JOBS * (GAME_COLUMN_BYTES + GAME_COLUMN_VOXELS + sizeof(game_column_job_t)) + 2ull * 4096ull,
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

/* The map, and what the card holds of it. Each says why if it cannot, and whatever came before it is given back. */
static bool game_open_parts(game_t *game, const game_config_t *config, int32_t chunk_x, int32_t chunk_y) {
    world_t *const world = game->borrowed_world;
    render_t *const render = game->borrowed_render;
    if (!world_open(world, config->render_distance + WORLD_MAP_KEEP_EXTRA, chunk_x, chunk_y)) {
        return false;
    }
    world_map_set_view_radius(&world->map, config->render_distance);
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
    /* The fog is whole a chunk short of what is loaded: the eye moves about inside a chunk before the map follows. */
    const uint32_t fog_chunks = config->render_distance > 1u ? config->render_distance - 1u : 1u;
    const float32_t fog_end = (float32_t)(fog_chunks * COLUMN_SIZE);
    if (render_open_frame(render, game->borrowed_texture, config->width, config->height, fog_end) != GPU_STATUS_OK) {
        render_close(render);
        world_close(world);
        return false;
    }
    render_frame_features_set(&render->frame, SHADE_FEATURES_ALL & ~(uint32_t)config->features_off);
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
static void game_spawn_find(const terrain_t *terrain, const game_column_job_t *job, int32_t middle_x, int32_t middle_y,
                            int32_t *out_x, int32_t *out_y) {
    *out_x = middle_x;
    *out_y = middle_y;
    for (int32_t ring = 0; ring <= GAME_SPAWN_RINGS; ring++) {
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
        game_spawn_find(terrain, &game->borrowed_jobs[0], start_x, start_y, &start_x, &start_y);
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

    if (!game_open_parts(game, config, chunk_x, chunk_y)) {
        job_group_deinit(&game->group);
        game_arenas_stop(game);
        heap_release(&game->owned_region);
        return false;
    }
    game_day_set(game, GAME_DAY_START);
    game_day_apply(game);
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
    render_close(game->borrowed_render);
    world_close(game->borrowed_world);
    game_arenas_stop(game);
    heap_release(&game->owned_region);
    game->open = 0u;
    game->jobs_in_use = 0u;
    game->stream_quiet = 0u;
    game->borrowed_jobs = nullptr;
    game->borrowed_last_picture = nullptr;
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

void game_sim_step(game_t *game) {
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

/* One tick of simulated time, due at tick_ns: this is where Stages 11 to 15 add theirs. */
static void game_sim_tick(game_t *game, int64_t tick_ns) {
    arena_reset(&game->arena_tick);
    if (game->borrowed_player != nullptr) {
        player_tick(game->borrowed_player, &game->borrowed_world->map, tick_ns);
    }
    world_tick_step(&game->borrowed_world->tick);
    game->tick_count++;
}

static void game_center_on_eye(game_t *game) {
    const int32_t cell_x = (int32_t)floor(game->camera.position[0]);
    const int32_t cell_y = (int32_t)floor(game->camera.position[1]);
    atomic_store_explicit(&game->center_x, column_chunk_of(cell_x), memory_order_relaxed);
    atomic_store_explicit(&game->center_y, column_chunk_of(cell_y), memory_order_relaxed);
}

void game_fly(game_t *game, const render_fly_input_t *input, float32_t seconds) {
    render_camera_fly(&game->camera, input, seconds);
    game_center_on_eye(game);
}

void game_walk_send(game_t *game, const render_fly_input_t *input, bool jump, bool sneak, int64_t now_ns) {
    player_camera_look(&game->camera, input->mouse_dx, input->mouse_dy);
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

/* The eye goes where the ticks have got to, by now_ns. */
static void game_walk_view(game_t *game, int64_t now_ns) {
    player_eye_at(game->borrowed_player, now_ns, game->camera.position);
    game_center_on_eye(game);
}

/* The block aimed at, on a picture just put in target, which camera drew. Without a player nothing is aimed at. */
static void game_outline_draw(const game_t *game, const render_camera_t *camera, display_buffer_t *target) {
    if (game->borrowed_player != nullptr) {
        player_camera_outline_draw(camera, player_target_view(game->borrowed_player), target);
    }
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
           render_upload_idle(&render->upload);
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
        if (render->buffers.pool_full_count != 0u && game->pool_full_said == 0u) {
            LOG_WARN("game: the section pool is full, so part of the world will not be drawn");
            game->pool_full_said = 1u;
        }
        if (game_is_settled(game)) {
            return true;
        }
        if (waiting_before != 0u && waiting_before == game->jobs_in_use) {
            time_sleep_ns(GAME_SETTLE_IDLE_NS);
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

static bool game_scene_tick(void *context, const display_headless_tick_t *tick) {
    game_t *const game = context;
    render_fly_input_t input;
    game_fly_from_script(tick->state, &input);
    /* Time here is the count of ticks, so one script gives one path. */
    const int64_t tick_ns = (int64_t)(game->tick_count + 1u) * GAME_TICK_NS;
    game_day_advance(game, 1.0f32 / (float32_t)MC_GAME_TICK_HZ);
    if (game->borrowed_player == nullptr) {
        game_fly(game, &input, 1.0f32 / (float32_t)MC_GAME_TICK_HZ);
        game_sim_tick(game, tick_ns);
        return true;
    }
    /* A draw settles too, but a script need not draw every tick, and the body would then wait for ground that never comes. */
    if (!game_settle(game)) {
        return false;
    }
    game_walk_send(game, &input, display_script_state_is_down(tick->state, KEY_SPACE),
                   display_script_state_is_down(tick->state, KEY_LEFTSHIFT), tick_ns);
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
    const gpu_status_t status = render_frame_draw(&render->frame, &render->buffers, &game->camera);
    if (status != GPU_STATUS_OK) {
        gpu_error_log(&render->frame.gpu.last_error);
        return false;
    }
    const uint8_t *const picture = render_frame_take(&render->frame);
    ASSERT_ALWAYS(picture != nullptr);
    render_frame_present(&render->frame, picture, frame);
    game_outline_draw(game, &game->camera, frame);
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
        if (now_ns >= next_tick_ns) {
            game_sim_tick(game, next_tick_ns);
            next_tick_ns += GAME_TICK_NS;
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
        /* A picture no one has taken yet is plenty: drawing past the display's rate would only heat the card. */
        if (render_exchange_waiting(&render->frame.pictures)) {
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
        game_day_apply(game);
        /* In the slot this picture is going into, for the outline. The publish in the draw hands main both. */
        game->picture_cameras[render->frame.pictures.back] = *camera;
        status = render_frame_draw(&render->frame, &render->buffers, camera);
        if (status != GPU_STATUS_OK) {
            gpu_error_log(&render->frame.gpu.last_error);
            atomic_store_explicit(&game->failed, 1u, memory_order_release);
            return;
        }
    }
}

bool game_loop_start(game_t *game, input_t *input) {
    ASSERT_ALWAYS(game->open != 0u && game->threads_running == 0u);
    game->borrowed_input = input;
    game->last_frame_ns = time_now_ns();
    game->loop_started_ns = game->last_frame_ns;
    game->pictures_shown = 0u;
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
        game->shown_camera = game->picture_cameras[frame->pictures.front];
        game->pictures_shown++;
    }
    if (game->borrowed_last_picture != nullptr) {
        render_frame_present(frame, game->borrowed_last_picture, target);
        game_outline_draw(game, &game->shown_camera, target);
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

bool game_loop_source(void *context, display_buffer_t *back) {
    game_t *const game = context;
    /* Here and not before the run: display_frames_run forks vt_guard's guardian before its first frame, and the sim and gpu
     * threads should not be writing into the memory that fork is about to share. */
    if (game->threads_running == 0u && !game_loop_start(game, game->borrowed_input)) {
        atomic_store_explicit(&game->failed, 1u, memory_order_release);
        return false; /* it has said why */
    }
    const input_frame_t *const frame = input_poll(game->borrowed_input);
    if (input_keys_has(&frame->pressed, KEY_ESC)) {
        return false;
    }
    const int64_t now_ns = time_now_ns();
    const float32_t seconds = (float32_t)time_ns_to_seconds(now_ns - game->last_frame_ns);
    game->last_frame_ns = now_ns;
    render_fly_input_t input;
    game_fly_from_input(frame, &input);
    if (game->borrowed_player != nullptr) {
        game_walk_send(game, &input, input_keys_has(&frame->down, KEY_SPACE),
                       input_keys_has(&frame->down, KEY_LEFTSHIFT), now_ns);
    }
    return game_loop_frame(game, &input, seconds, back);
}

void game_loop_discard(void *context) {
    game_t *const game = context;
    input_test_pattern_discard(game->borrowed_input);
    /* The time away is not time spent flying. */
    game->last_frame_ns = time_now_ns();
}

void game_report(game_t *game) {
    if (game->borrowed_player != nullptr) {
        const player_movement_t *const movement = &game->borrowed_player->movement;
        LOG_INFO("game: the player is at %.3f, %.3f, %.3f after %" PRIu64 " ticks",
                 (libc_double_t)movement->position[0], (libc_double_t)movement->position[1],
                 (libc_double_t)movement->position[2], game->borrowed_player->tick_count);
    }
    LOG_INFO("game: %" PRIu64 " columns generated, %" PRIu64 " ticks, %" PRIu64
             " sections sent, %.1f MiB of slabs held",
             game->columns_made, game->tick_count, game->borrowed_render->upload.stats.sections_sent,
             (libc_double_t)game->borrowed_render->buffers.slab_bytes / (libc_double_t)(1u << 20));
    /* The threads have no one to tell at the time: nothing can log while the console shows the picture. */
    if (game->borrowed_render->buffers.pool_full_count != 0u) {
        LOG_WARN("game: the section pool was full %" PRIu64 " times, so part of the world was not drawn",
                 (uint64_t)game->borrowed_render->buffers.pool_full_count);
    }
    /* The flip rate display_frames_run reports counts a picture shown twice as two. This is the rate of new ones. */
    if (game->pictures_shown != 0u && game->loop_ended_ns > game->loop_started_ns) {
        const float64_t seconds = time_ns_to_seconds(game->loop_ended_ns - game->loop_started_ns);
        LOG_INFO("game: %" PRIu64 " new pictures on the screen in %.1f s: %.1f FPS", game->pictures_shown,
                 (libc_double_t)seconds, (libc_double_t)((float64_t)game->pictures_shown / seconds));
    }
    render_frame_report(&game->borrowed_render->frame);
}
