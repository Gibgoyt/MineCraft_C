/* ctest run_save_state_game. See README.md. */
#include "harness/index.h"

#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "job/index.h"
#include "physics/index.h"
#include "render/index.h"
#include "save/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/menu/catalog/index.h"
#include "world/index.h"

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

enum : uint64_t {
    SS_PATH_BYTES = 512u,
    SS_FILE_BYTES = 8ull << 20,
};
enum : uint32_t {
    SS_WIDTH = 640u,
    SS_HEIGHT = 360u,
    SS_FRAMES_MOST = 4096u,
};
static const uint64_t SS_SEED = 9191u;

static void *ss_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

static char *ss_join(test_context_t *t, const char *left, const char *right) {
    char *const path = test_alloc(t, SS_PATH_BYTES);
    snprintf(path, SS_PATH_BYTES, "%s/%s", left, right);
    return path;
}

static char *ss_state_path(test_context_t *t, const char *root, uint64_t generation) {
    char *const path = test_alloc(t, SS_PATH_BYTES);
    snprintf(path, SS_PATH_BYTES, "%s/state.%" PRIu64, root, generation);
    return path;
}

static char *ss_temp_directory(test_context_t *t) {
    char *const directory = test_alloc(t, SS_PATH_BYTES);
    snprintf(directory, SS_PATH_BYTES, "/tmp/mc_save_state_XXXXXX");
    return mkdtemp(directory);
}

static void ss_temp_remove(test_context_t *t, const char *directory) {
    if (directory == nullptr || strncmp(directory, "/tmp/mc_save_state_", 19u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", (char *)directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static uint8_t *ss_slurp(test_context_t *t, const char *path, uint64_t *out_bytes) {
    uint8_t *const bytes = test_alloc(t, SS_FILE_BYTES);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, SS_FILE_BYTES, out_bytes), FILE_OK)) {
        *out_bytes = 0u;
    }
    return bytes;
}

/* How many state.<g> are in a directory, leaving out those on their way. */
static uint32_t ss_state_files(const char *directory) {
    DIR *const handle = opendir(directory);
    if (handle == nullptr) {
        return 0u;
    }
    uint32_t count = 0u;
    for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
        const uint64_t bytes = strlen(entry->d_name);
        const bool temporary = bytes > 4u && strcmp(entry->d_name + bytes - 4u, ".tmp") == 0;
        if (strncmp(entry->d_name, "state.", 6u) == 0 && !temporary) {
            count++;
        }
    }
    (void)closedir(handle);
    return count;
}

/* What a file says of the time. */
static save_state_time_t ss_file_time(const uint8_t *file, uint64_t bytes) {
    save_state_time_t time = {0};
    uint64_t section_bytes = 0u;
    const uint8_t *const data = save_state_section_find(file, bytes, SAVE_STATE_SECTION_TIME, &section_bytes);
    if (data != nullptr) {
        memcpy(&time, data, sizeof(time));
    }
    return time;
}

/* ---- The whole game ------------------------------------------------------------------------------------------------------ */

typedef struct ss_game_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    save_t *save;                /* 56 */
    thread_topology_t *topology; /* 64 */
    physics_t *physics;          /* 72: with a player */
    player_t *player;            /* 80 */
    item_t *item;                /* 88 */
    entity_t *entity;            /* 96 */
    mob_t *mob;                  /* 104 */
} ss_game_rig_t;
static_assert(sizeof(ss_game_rig_t) == 112);

static bool ss_game_begin(test_context_t *t, ss_game_rig_t *rig, bool with_player) {
    memset(rig, 0, sizeof(*rig));
    rig->bridge = ss_aligned(t, sizeof(gpu_bridge_t));
    rig->job = ss_aligned(t, sizeof(job_system_t));
    rig->world = ss_aligned(t, sizeof(world_t));
    rig->terrain = ss_aligned(t, sizeof(terrain_t));
    rig->render = ss_aligned(t, sizeof(render_t));
    rig->texture = ss_aligned(t, sizeof(texture_t));
    rig->game = ss_aligned(t, sizeof(game_t));
    rig->save = ss_aligned(t, sizeof(save_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, SS_SEED)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default())) ||
        !CHECK(save_init(rig->save))) {
        return false;
    }
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    game_save_set(rig->game, rig->save);
    if (with_player) {
        rig->physics = ss_aligned(t, sizeof(physics_t));
        rig->player = ss_aligned(t, sizeof(player_t));
        rig->item = ss_aligned(t, sizeof(item_t));
        rig->entity = ss_aligned(t, sizeof(entity_t));
        rig->mob = ss_aligned(t, sizeof(mob_t));
        if (!CHECK(physics_init(rig->physics))) {
            return false;
        }
        player_init(rig->player, rig->physics);
        if (!CHECK(item_init(rig->item)) || !CHECK(entity_init(rig->entity, rig->physics)) ||
            !CHECK(mob_init(rig->mob, rig->job))) {
            return false;
        }
        game_player_set(rig->game, rig->player);
        game_item_set(rig->game, rig->item);
        game_entity_set(rig->game, rig->entity);
        game_mob_set(rig->game, rig->mob);
    }
    return true;
}

static void ss_game_end(test_context_t *t, ss_game_rig_t *rig) {
    game_deinit(rig->game);
    save_deinit(rig->save);
    if (rig->player != nullptr) {
        mob_deinit(rig->mob);
        entity_deinit(rig->entity);
        item_deinit(rig->item);
        player_deinit(rig->player);
        physics_deinit(rig->physics);
    }
    render_deinit(rig->render);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
}

static int32_t ss_i64_compare(const void *left, const void *right) {
    const int64_t a = *(const int64_t *)left, b = *(const int64_t *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

/* Frames as a real run makes them, for this long, with the player looking down and digging. What main's part of each took, in order, and how long
 * came between new pictures from the gpu thread, which is what someone looking at it would see stop: game_loop_frame only shows what that thread
 * finished, so a hitch in the drawing is not in its own time. To within the nap between frames. */
static uint32_t ss_frames_run(ss_game_rig_t *rig, display_buffer_t *target, int64_t *out_frame_ns, int64_t *out_gap_ns,
                              int64_t seconds, uint32_t *out_gaps) {
    game_t *const game = rig->game;
    const int64_t until = time_now_ns() + seconds * TIME_NS_PER_SECOND;
    uint32_t count = 0u;
    uint32_t gaps = 0u;
    uint64_t pictures = game->pictures_shown;
    int64_t picture_ns = 0; /* the first picture has only how long the threads took to start before it */
    int64_t last_ns = time_now_ns();
    while (time_now_ns() < until && count < SS_FRAMES_MOST) {
        const int64_t started_ns = time_now_ns();
        /* Down, and it stays there: pitch stops at straight down. */
        const render_fly_input_t input = {.mouse_dy = 40};
        game_walk_send(game, &input, false, false, started_ns);
        game_hands_send(game, true, false);
        const float32_t passed = (float32_t)time_ns_to_seconds(started_ns - last_ns);
        last_ns = started_ns;
        if (!game_loop_frame(game, &input, passed, target)) {
            break;
        }
        const int64_t ended_ns = time_now_ns();
        out_frame_ns[count] = ended_ns - started_ns;
        count++;
        if (game->pictures_shown != pictures) {
            pictures = game->pictures_shown;
            if (picture_ns != 0 && gaps < SS_FRAMES_MOST) {
                out_gap_ns[gaps] = ended_ns - picture_ns;
                gaps++;
            }
            picture_ns = ended_ns;
        }
        time_sleep_ns(2000000);
    }
    *out_gaps = gaps;
    return count;
}

static void ss_times_print(const char *what, int64_t *times, uint32_t count) {
    qsort(times, count, sizeof(int64_t), ss_i64_compare);
    printf("  %s: %" PRIu32 ", median %.3f ms, 99th %.3f ms, longest %.3f ms\n", what, count,
           (libc_double_t)time_ns_to_ms(times[count / 2u]), (libc_double_t)time_ns_to_ms(times[count * 99u / 100u]),
           (libc_double_t)time_ns_to_ms(times[count - 1u]));
}

TEST(the_real_game_frames_during_autosaves_and_reloads_byte_for_byte) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    ss_game_rig_t *const rig = test_alloc(t, sizeof(ss_game_rig_t));
    REQUIRE(ss_game_begin(t, rig, true));
    game_t *const game = rig->game;
    save_t *const save = rig->save;
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    const game_config_t config = {.render_distance = 4u, .width = SS_WIDTH, .height = SS_HEIGHT, .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    CHECK_EQ_U64(save->autosave.started, 1u);
    CHECK_EQ_U64(save->autosave.next_tick, SAVE_AUTOSAVE_INTERVAL_TICKS);

    const uint64_t screen_bytes = (uint64_t)SS_WIDTH * SS_HEIGHT * 4u;
    display_buffer_t target = {.owned_map = ss_aligned(t, screen_bytes),
                               .map_bytes = screen_bytes,
                               .pitch = SS_WIDTH * 4u,
                               .width = SS_WIDTH,
                               .height = SS_HEIGHT};
    int64_t *const without = test_alloc(t, SS_FRAMES_MOST * sizeof(int64_t));
    int64_t *const with = test_alloc(t, SS_FRAMES_MOST * sizeof(int64_t));
    int64_t *const gap_without = test_alloc(t, SS_FRAMES_MOST * sizeof(int64_t));
    int64_t *const gap_with = test_alloc(t, SS_FRAMES_MOST * sizeof(int64_t));
    uint32_t gaps_without = 0u, gaps_with = 0u;

    /* First with no save coming, then with one at every chance: a save starts each few ticks, far oftener than in play. */
    save->autosave.next_tick = UINT64_MAX;
    REQUIRE(game_loop_start(game, nullptr));
    const uint32_t frames_without = ss_frames_run(rig, &target, without, gap_without, 4, &gaps_without);
    CHECK(game_loop_stop(game));
    save_autosave_interval_set(&save->autosave, 4u);
    save->autosave.next_tick = 0u;
    REQUIRE(game_loop_start(game, nullptr));
    const uint32_t frames_with = ss_frames_run(rig, &target, with, gap_with, 8, &gaps_with);
    CHECK(game_loop_stop(game));
    REQUIRE(frames_without > 10u && frames_with > 10u && gaps_without > 10u && gaps_with > 10u);
    ss_times_print("game_loop_frame without a save", without, frames_without);
    ss_times_print("game_loop_frame during saves", with, frames_with);
    ss_times_print("between new pictures without a save", gap_without, gaps_without);
    ss_times_print("between new pictures during saves", gap_with, gaps_with);
    printf("  %" PRIu64 " saves in those, %" PRIu64 " failed; %" PRIu64
           " sections written; the longest %.3f ms of the sim, %.3f ms of a worker; %" PRIu64 " blocks dug\n",
           save->autosave.saves_done, save->autosave.saves_failed, save->world.sections_written,
           (libc_double_t)time_ns_to_ms(save->autosave.capture_ns_most),
           (libc_double_t)time_ns_to_ms(save->autosave.write_ns_most), rig->player->interaction.mined_count);
    CHECK(save->autosave.saves_done >= 10u);
    CHECK_EQ_U64(save->autosave.saves_failed, 0u);
    CHECK(rig->player->interaction.mined_count >= 1u);
    CHECK(save->world.sections_written >= 1u);
    /* The sim's part stays far under a tick. */
    CHECK(time_ns_to_ms(save->autosave.capture_ns_most) < 50.0f64);
    /* And what is seen: no longer a wait for a picture, by more than two frames at 120 Hz, as this can only tell to within a frame each way. */
    CHECK(time_ns_to_ms(with[frames_with - 1u]) <= time_ns_to_ms(without[frames_without - 1u]) + 16.667f64);
    CHECK(time_ns_to_ms(gap_with[gaps_with / 2u]) <= time_ns_to_ms(gap_without[gaps_without / 2u]) + 8.333f64);
    CHECK(time_ns_to_ms(gap_with[gaps_with - 1u]) <= time_ns_to_ms(gap_without[gaps_without - 1u]) + 16.667f64);

    /* All of it, as a world closes, and then the same world from those files alone, with nothing having ticked. Three blocks from where it stood, so that
     * where the save says and where a new world would spawn are not one block. */
    rig->player->movement.position[0] += 3.0f64;
    rig->player->movement.position[1] += 3.0f64;
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    const uint64_t generation = save->world.generation;
    CHECK(generation >= 1u);
    CHECK_EQ_U64(ss_state_files(root), 1u);
    uint64_t bytes = 0u;
    const uint8_t *const file = ss_slurp(t, ss_state_path(t, root, generation), &bytes);
    REQUIRE(bytes > 0u);
    const float64_t feet[3] = {rig->player->movement.position[0], rig->player->movement.position[1],
                               rig->player->movement.position[2]};
    const uint64_t mined = rig->player->interaction.mined_count;
    const uint64_t ticks = game->tick_count;
    const uint32_t units = atomic_load(&game->day_units);
    game_close(game);
    save_close(save);

    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    CHECK_EQ_U64(save->world.generation, generation);
    REQUIRE(game_open(game, &config));
    CHECK(rig->player->movement.position[0] == feet[0] && rig->player->movement.position[1] == feet[1] &&
          rig->player->movement.position[2] == feet[2]);
    /* With nothing having ticked, the first picture's coordinates are already where the save put the body, not where the world's spawn is. */
    int32_t shown[3];
    game_hud_block_read(game, shown);
    CHECK_EQ_I64(shown[0], (int64_t)floor(feet[0]));
    CHECK_EQ_I64(shown[1], (int64_t)floor(feet[1]));
    CHECK_EQ_I64(shown[2], (int64_t)floor(feet[2]) - MC_WORLD_Z_SEA);
    CHECK_EQ_U64(rig->player->interaction.mined_count, mined);
    CHECK_EQ_U64(game->tick_count, ticks);
    CHECK_EQ_U64(atomic_load(&game->day_units), units);
    CHECK_EQ_U64(save->autosave.next_tick, ticks + SAVE_AUTOSAVE_INTERVAL_TICKS);
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    CHECK_EQ_U64(save->world.generation, generation); /* no block changed, so the same save */
    uint64_t again_bytes = 0u;
    const uint8_t *const again = ss_slurp(t, ss_state_path(t, root, generation), &again_bytes);
    REQUIRE(again_bytes == bytes);
    CHECK_MEM_EQ(again, file, bytes);
    game_close(game);
    save_close(save);
    ss_game_end(t, rig);
    ss_temp_remove(t, root);
}

/* Steps the sim until what it is waiting for is so. */
static bool ss_step_until(game_t *game, int32_t chunk_x, int32_t chunk_y, bool wanted) {
    const int64_t deadline = time_now_ns() + 60ll * TIME_NS_PER_SECOND;
    while (time_now_ns() < deadline) {
        game_sim_step(game);
        if ((world_map_column_at(&game->borrowed_world->map, chunk_x, chunk_y) != nullptr) == wanted) {
            return true;
        }
        time_sleep_ns(200000);
    }
    return false;
}

TEST(a_column_that_will_not_fit_waits_for_a_save_and_only_the_autosave_writes) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    ss_game_rig_t *const rig = test_alloc(t, sizeof(ss_game_rig_t));
    REQUIRE(ss_game_begin(t, rig, false));
    game_t *const game = rig->game;
    save_t *const save = rig->save;
    world_t *const world = rig->world;
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    const game_config_t config = {.render_distance = 3u, .width = 64u, .height = 36u, .show_date = 0u};
    REQUIRE(game_open(game, &config));
    const int32_t home_x = atomic_load(&game->center_x);
    const int32_t home_y = atomic_load(&game->center_y);
    REQUIRE(ss_step_until(game, home_x, home_y, true));

    /* A change in the column that is to leave, and one captured already, in a set that is about to be too full for more. */
    const int32_t x = home_x * 16 + 3, y = home_y * 16 + 5, z = 400;
    const uint16_t placed = 7u;
    REQUIRE(world_edit_block_set(&world->edit, x, y, z, placed) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(save_world_capture_dirty(&save->world, &world->edit) == SAVE_WORLD_OK);
    REQUIRE(world_edit_block_set(&world->edit, x + 1, y, z, placed) == WORLD_EDIT_STATUS_CHANGED);
    save_world_pending_t *const pending = &save->world.pending[save->world.active];
    REQUIRE(pending->count >= 1u);
    pending->used = SAVE_WORLD_PENDING_BYTES;

    /* It goes out of reach. The step that finds it will not fit asks for a save, and no write is made there: no save has been due. */
    atomic_store(&game->center_x, home_x + 40);
    const uint64_t due = save->autosave.next_tick;
    for (uint32_t i = 0u; i < 4000u && save->autosave.requested == 0u && save->autosave.saves_done == 0u; i++) {
        game_sim_step(game);
        time_sleep_ns(200000);
    }
    /* And that save, which is not the interval's, makes the room: it goes. */
    REQUIRE(ss_step_until(game, home_x, home_y, false));
    CHECK_EQ_U64(due, SAVE_AUTOSAVE_INTERVAL_TICKS);
    CHECK_EQ_U64(game->tick_count, 0u);
    atomic_store(&game->center_x, home_x);
    REQUIRE(ss_step_until(game, home_x, home_y, true));
    uint16_t block = 0u;
    CHECK_EQ_U64(world_map_block_get(&world->map, x, y, z, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, placed);
    CHECK_EQ_U64(world_map_block_get(&world->map, x + 1, y, z, &block), WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, placed);
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    CHECK(save->autosave.saves_done >= 2u); /* the one it waited for, and this */
    CHECK(save->world.generation >= 1u);
    CHECK_EQ_U64(ss_state_files(root), 1u);
    game_close(game);
    save_close(save);
    ss_game_end(t, rig);
    ss_temp_remove(t, root);
}

static void ss_no_spill(void *context, const item_stack_t *stack, const int32_t cell[3]) {
    (void)context;
    (void)stack;
    (void)cell;
}

/* Steps the sim until no write is under way, and it has looked at what the last one said. */
static bool ss_autosave_settle(game_t *game, save_t *save) {
    const int64_t deadline = time_now_ns() + 60ll * TIME_NS_PER_SECOND;
    while (time_now_ns() < deadline) {
        game_sim_step(game);
        if (atomic_load(&save->autosave.writing) == 0u && save->autosave.outstanding == 0u) {
            return true;
        }
        time_sleep_ns(200000);
    }
    return false;
}

TEST(the_game_keeps_what_flows_the_tick_count_and_what_a_chest_gone_this_tick_held) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    ss_game_rig_t *const rig = test_alloc(t, sizeof(ss_game_rig_t));
    REQUIRE(ss_game_begin(t, rig, true));
    game_t *const game = rig->game;
    save_t *const save = rig->save;
    world_t *const world = rig->world;
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    const game_config_t config = {.render_distance = 3u, .width = 64u, .height = 36u, .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    const int32_t home_x = atomic_load(&game->center_x) * 16, home_y = atomic_load(&game->center_y) * 16;

    /* High in the air, on a floor of its own, far from anything terrain made. */
    const int32_t z = 400;
    for (int32_t y = 0; y < 16; y++) {
        for (int32_t x = 0; x < 16; x++) {
            REQUIRE(world_edit_block_set(&world->edit, home_x + x, home_y + y, z - 1, BLOCK_ID_STONE) ==
                    WORLD_EDIT_STATUS_CHANGED);
        }
    }
    REQUIRE(world_edit_block_set(&world->edit, home_x + 8, home_y + 8, z, BLOCK_ID_WATER) == WORLD_EDIT_STATUS_CHANGED);
    for (uint32_t i = 0u; i < 100u; i++) {
        world_tick_step(&world->tick);
    }
    REQUIRE(world_tick_fluid_level(&world->tick, home_x + 9, home_y + 8, z) == 1u);
    REQUIRE(world_tick_fluid_level(&world->tick, home_x + 15, home_y + 8, z) == 7u);

    /* A chest with something in it that goes after the items have had their look for this tick, as a creeper's blast does. */
    const int32_t chest_x = home_x + 2, chest_y = home_y + 2, chest_z = z + 5;
    REQUIRE(world_edit_block_set(&world->edit, chest_x, chest_y, chest_z, BLOCK_ID_CHEST) == WORLD_EDIT_STATUS_CHANGED);
    item_world_edits_read(rig->item, &world->edit, ss_no_spill, nullptr);
    container_chest_t *const chest = container_at(&rig->item->chests, chest_x, chest_y, chest_z);
    REQUIRE(chest != nullptr);
    *container_slot(chest, 0u) = item_stack_make(ITEM_ID_DIRT, 5u);
    const uint32_t chests = rig->item->chests.count;
    const uint32_t drops = rig->entity->item_drops.count;
    REQUIRE(world_edit_block_set(&world->edit, chest_x, chest_y, chest_z, BLOCK_ID_AIR) == WORLD_EDIT_STATUS_CHANGED);

    /* The save comes before any tick does. What it keeps is the items on the ground, and no chest with nowhere to be. */
    save->autosave.next_tick = 0u;
    REQUIRE(ss_autosave_settle(game, save));
    CHECK(save->autosave.saves_done >= 1u);
    CHECK_EQ_U64(rig->item->chests.count, chests - 1u);
    CHECK_EQ_U64(rig->entity->item_drops.count, drops + 1u);

    game->tick_count = 777u;
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    game_close(game);
    save_close(save);

    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    CHECK_EQ_U64(game->tick_count, 777u);
    CHECK_EQ_U64(world->tick.now, 777u);
    CHECK_EQ_U64(rig->item->chests.count, chests - 1u);
    CHECK(container_at(&rig->item->chests, chest_x, chest_y, chest_z) == nullptr);
    CHECK_EQ_U64(rig->entity->item_drops.count, drops + 1u);
    REQUIRE(game_settle(game));
    uint16_t block = BLOCK_ID_AIR;
    REQUIRE(world_map_block_get(&world->map, home_x + 15, home_y + 8, z, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_WATER);
    CHECK_EQ_U64(world_tick_fluid_level(&world->tick, home_x + 8, home_y + 8, z), WORLD_TICK_LEVEL_SOURCE);
    CHECK_EQ_U64(world_tick_fluid_level(&world->tick, home_x + 9, home_y + 8, z), 1u);
    CHECK_EQ_U64(world_tick_fluid_level(&world->tick, home_x + 15, home_y + 8, z), 7u);
    CHECK(world_tick_pending(&world->tick) > 0u);
    game_close(game);
    save_close(save);
    ss_game_end(t, rig);
    ss_temp_remove(t, root);
}

/* How many bodies in the store are mobs. */
static uint32_t ss_mobs_in(const entity_store_t *store) {
    uint32_t mobs = 0u;
    for (uint32_t i = 0u; i < store->count; i++) {
        mobs += store->borrowed_kind[i] != MOB_ID_NONE && store->borrowed_kind[i] < MOB_COUNT ? 1u : 0u;
    }
    return mobs;
}

/* Plan 005 phase 3: a world saved with zombies, skeletons and creepers in it, as one from before they were turned off, opens without
 * them. They are put down by hand, as nothing brings them now. */
TEST(a_world_saved_with_turned_off_mobs_opens_without_them) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    ss_game_rig_t *const rig = test_alloc(t, sizeof(ss_game_rig_t));
    REQUIRE(ss_game_begin(t, rig, true));
    game_t *const game = rig->game;
    save_t *const save = rig->save;
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    const game_config_t config = {.render_distance = 3u, .width = 64u, .height = 36u, .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    entity_store_t *const store = &rig->entity->store;
    REQUIRE(ss_mobs_in(store) == 0u);
    static const mob_id_t MOBS[] = {MOB_ID_ZOMBIE, MOB_ID_ZOMBIE, MOB_ID_SKELETON, MOB_ID_CREEPER};
    for (uint32_t i = 0u; i < sizeof(MOBS) / sizeof(MOBS[0]); i++) {
        const float64_t place[COLLISION_AXES] = {rig->player->movement.position[0] + 3.0f64 + (float64_t)i,
                                                 rig->player->movement.position[1],
                                                 rig->player->movement.position[2] + 2.0f64};
        REQUIRE(mob_spawn_body(&game->mob_spawn, store, MOBS[i], place, 0.0f32) != ENTITY_HANDLE_NONE);
    }
    REQUIRE(ss_mobs_in(store) == 4u);
    const uint64_t despawned = game->mob_spawn.despawned_count;
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    game_close(game);
    save_close(save);

    /* The state on disk holds the four, as a save keeps what the store holds; game_open takes them away. */
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    CHECK_EQ_U64(ss_mobs_in(store), 0u);
    CHECK_EQ_U64(game->mob_spawn.despawned_count, despawned);
    /* And a save from then on holds none. */
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    game_close(game);
    save_close(save);
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    CHECK_EQ_U64(ss_mobs_in(store), 0u);
    game_close(game);
    save_close(save);
    ss_game_end(t, rig);
    ss_temp_remove(t, root);
}

TEST(what_was_waiting_for_its_first_tick_goes_on_after_a_reload) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    ss_game_rig_t *const rig = test_alloc(t, sizeof(ss_game_rig_t));
    REQUIRE(ss_game_begin(t, rig, false));
    game_t *const game = rig->game;
    save_t *const save = rig->save;
    world_t *const world = rig->world;
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    const game_config_t config = {.render_distance = 3u, .width = 64u, .height = 36u, .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    const int32_t home_x = atomic_load(&game->center_x) * 16, home_y = atomic_load(&game->center_y) * 16;
    const int32_t z = 400;
    for (int32_t y = 0; y < 16; y++) {
        for (int32_t x = 0; x < 16; x++) {
            REQUIRE(world_edit_block_set(&world->edit, home_x + x, home_y + y, z - 1, BLOCK_ID_STONE) ==
                    WORLD_EDIT_STATUS_CHANGED);
        }
    }
    /* Not one tick goes by: a source just placed, and sand just placed in the air. */
    REQUIRE(world_edit_block_set(&world->edit, home_x + 8, home_y + 8, z, BLOCK_ID_WATER) == WORLD_EDIT_STATUS_CHANGED);
    REQUIRE(world_edit_block_set(&world->edit, home_x + 2, home_y + 2, z + 3, BLOCK_ID_SAND) ==
            WORLD_EDIT_STATUS_CHANGED);
    CHECK_EQ_U64(world_tick_pending(&world->tick), 0u);
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    game_close(game);
    save_close(save);

    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    uint16_t block = BLOCK_ID_AIR;
    REQUIRE(world_map_block_get(&world->map, home_x + 2, home_y + 2, z + 3, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_SAND);
    CHECK(world_tick_pending(&world->tick) >= 2u);
    for (uint32_t i = 0u; i < 100u; i++) {
        world_tick_step(&world->tick);
    }
    REQUIRE(world_map_block_get(&world->map, home_x + 2, home_y + 2, z + 3, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_AIR);
    REQUIRE(world_map_block_get(&world->map, home_x + 2, home_y + 2, z, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_SAND);
    REQUIRE(world_map_block_get(&world->map, home_x + 9, home_y + 8, z, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_WATER);
    CHECK_EQ_U64(world_tick_fluid_level(&world->tick, home_x + 9, home_y + 8, z), 1u);
    game_close(game);
    save_close(save);
    ss_game_end(t, rig);
    ss_temp_remove(t, root);
}

/* What the finest level has at a chunk: its highest voxel as the table says, in blocks of storage height. */
static int32_t ss_far_top(const game_t *game, int32_t chunk_x, int32_t chunk_y) {
    const render_lod_level_t *const level = &game->borrowed_render->borrowed_lod->levels[0];
    uint32_t slot = 0u;
    if (!world_buffers_slot_of(&level->buffers.window, chunk_x >> level->shift, chunk_y >> level->shift, &slot)) {
        return INT32_MIN;
    }
    return ((int32_t)level->buffers.borrowed_shadow[slot].top_z - COLUMN_Z_STORAGE_MIN) << level->shift;
}

TEST(the_far_field_shows_what_a_save_holds_and_makes_again_what_is_captured) {
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    ss_game_rig_t *const rig = test_alloc(t, sizeof(ss_game_rig_t));
    REQUIRE(ss_game_begin(t, rig, false));
    game_t *const game = rig->game;
    save_t *const save = rig->save;
    world_t *const world = rig->world;
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    const game_config_t config = {
        .render_distance = 8u, .full_detail = 4u, .width = 64u, .height = 36u, .show_date = 0u};
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    render_lod_t *const lod = rig->render->borrowed_lod;
    REQUIRE(lod != nullptr && lod->level_count >= 1u);
    REQUIRE(lod->overlay != nullptr);
    const int32_t chunk_x = atomic_load(&game->center_x), chunk_y = atomic_load(&game->center_y);
    const int32_t ground = ss_far_top(game, chunk_x, chunk_y);
    const int32_t tower_top = COLUMN_Z_GAMEPLAY_MAX;
    const int32_t tower_bottom = tower_top - 2 * (int32_t)SECTION_SIZE;
    const int32_t tower_storage = (tower_top - COLUMN_Z_STORAGE_MIN) >> lod->levels[0].shift << lod->levels[0].shift;
    REQUIRE(ground < tower_storage);

    /* Asked of nothing: it is not caught up until a capture says so. */
    const int32_t base_x = chunk_x * 16, base_y = chunk_y * 16;
    for (int32_t z = tower_bottom; z <= tower_top; z++) {
        for (int32_t y = 0; y < 16; y++) {
            for (int32_t x = 0; x < 16; x++) {
                REQUIRE(world_edit_block_set(&world->edit, base_x + x, base_y + y, z, BLOCK_ID_STONE) ==
                        WORLD_EDIT_STATUS_CHANGED);
            }
        }
    }
    const uint64_t built = lod->stats.columns_built;
    const uint32_t shown = lod->levels[0].shown_side;
    REQUIRE(game_settle(game));
    CHECK_EQ_U64(lod->stats.columns_built, built);
    REQUIRE(save_world_capture_dirty(&save->world, &world->edit) == SAVE_WORLD_OK);
    game_far_changes_take(game);
    /* What was there stays shown until what replaces it has come, though it is worked out again in the meantime as it is for any column that comes in. */
    lod->levels[0].shown_stale = 1u;
    REQUIRE(render_lod_frame(lod, game->camera.position) == GPU_STATUS_OK);
    CHECK_EQ_U64(lod->levels[0].shown_side, shown);
    REQUIRE(game_settle(game));
    CHECK_EQ_U64(lod->levels[0].shown_side, shown);
    CHECK(lod->stats.columns_built >= built + 1u);
    CHECK(lod->stats.columns_built <= built + lod->level_count);
    CHECK_EQ_I64(ss_far_top(game, chunk_x, chunk_y), tower_storage);

    /* From the files alone, in a far field that starts with a new world. */
    REQUIRE(game_save_finish(game) == SAVE_WORLD_OK);
    game_close(game);
    save_close(save);
    REQUIRE(save_open(save, root, SS_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(game, &config));
    REQUIRE(game_settle(game));
    CHECK_EQ_I64(ss_far_top(game, chunk_x, chunk_y), tower_storage);
    /* Loading it for the far field is not loading it for the game. */
    const uint64_t loaded = atomic_load(&save->world.sections_loaded);

    /* And gone again, with what was over the ground's top given back. */
    for (int32_t z = tower_bottom; z <= tower_top; z++) {
        for (int32_t y = 0; y < 16; y++) {
            for (int32_t x = 0; x < 16; x++) {
                REQUIRE(world_edit_block_set(&world->edit, base_x + x, base_y + y, z, BLOCK_ID_AIR) ==
                        WORLD_EDIT_STATUS_CHANGED);
            }
        }
    }
    REQUIRE(save_world_capture_dirty(&save->world, &world->edit) == SAVE_WORLD_OK);
    game_far_changes_take(game);
    REQUIRE(game_settle(game));
    CHECK_EQ_I64(ss_far_top(game, chunk_x, chunk_y), ground);
    CHECK_EQ_U64(atomic_load(&save->world.sections_loaded), loaded);
    CHECK_EQ_U64(atomic_load(&save->world.sections_damaged), 0u);
    game_close(game);
    save_close(save);
    ss_game_end(t, rig);
    ss_temp_remove(t, root);
}

/* ---- The built program ---------------------------------------------------------------------------------------------------- */

/* Opens the first world on the list, looks down, digs, and goes on until it is killed or a very long way past any save. At 640 by 480, scale 2: see
 * tests/run/ui/menu/fixtures/flow.script for how these clicks were worked out. */
static const char SS_PLAY_SCRIPT[] = "1 move 0 -40\n"
                                     "+1 button left down\n+1 button left up\n"
                                     "+1 move -20 -122\n"
                                     "+1 button left down\n+1 button left up\n"
                                     "+1 move -112 318\n"
                                     "+1 button left down\n+1 button left up\n"
                                     "+4 move 112 500\n"
                                     "+4 button left down\n"
                                     "+70 button left up\n"
                                     "+200000 end\n";
/* The same round, for a few ticks, and out. */
static const char SS_LOOK_SCRIPT[] = "1 move 0 -40\n"
                                     "+1 button left down\n+1 button left up\n"
                                     "+1 move -20 -122\n"
                                     "+1 button left down\n+1 button left up\n"
                                     "+1 move -112 318\n"
                                     "+1 button left down\n+1 button left up\n"
                                     "+6 end\n";

static uint64_t ss_manifest_generation(const char *blocks) {
    char path[SS_PATH_BYTES];
    (void)snprintf(path, sizeof(path), "%s/manifest", blocks);
    save_world_manifest_header_t header;
    file_t file = {.fd = -1, ._pad0 = {0}};
    if (file_open(&file, path, FILE_MODE_READ) != FILE_OK) {
        return 0u;
    }
    const file_status_t status = file_read_exact(&file, &header, sizeof(header));
    (void)file_close(&file);
    return status == FILE_OK ? header.generation : 0u;
}

TEST(the_built_program_killed_in_play_goes_on_from_its_last_autosave) {
    REQUIRE(t->arg_count >= 1);
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = ss_join(t, root, "saves");
    ui_menu_world_t made;
    REQUIRE(ui_menu_catalog_create(saves, "dug", TERRAIN_SEED_DEFAULT, &made) == FILE_OK);
    char *const world_directory = ss_join(t, saves, made.directory);
    char *const blocks = ss_join(t, world_directory, "blocks");
    char *const play = ss_join(t, root, "play.script");
    REQUIRE(file_write_atomic(play, SS_PLAY_SCRIPT, strlen(SS_PLAY_SCRIPT)) == FILE_OK);
    char *const log_path = ss_join(t, root, "play.log");

    /* In a group of its own, and under timeout, so that nothing outlives this if the test dies first. */
    (void)fflush(stdout);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        (void)setpgid(0, 0);
        const int32_t nothing = open("/dev/null", O_RDONLY | O_CLOEXEC);
        const int32_t out = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (nothing < 0 || out < 0 || dup2(nothing, 0) < 0 || dup2(out, 1) < 0 || dup2(out, 2) < 0) {
            _exit(20);
        }
        char *const argv[] = {(char *)"/usr/bin/timeout",
                              (char *)"-s",
                              (char *)"KILL",
                              (char *)"200",
                              t->borrowed_args[0],
                              (char *)"--headless",
                              (char *)"--menu",
                              (char *)"--saves",
                              saves,
                              (char *)"--script",
                              play,
                              (char *)"--screenshot-dir",
                              root,
                              (char *)"--width",
                              (char *)"640",
                              (char *)"--height",
                              (char *)"480",
                              (char *)"--render-distance",
                              (char *)"4",
                              nullptr};
        (void)execv(argv[0], argv);
        _exit(21);
    }
    (void)setpgid(child, child);
    /* Until its first autosave is whole, which is a good way past the digging. */
    const int64_t deadline = time_now_ns() + 180ll * TIME_NS_PER_SECOND;
    uint64_t generation = 0u;
    while (time_now_ns() < deadline && generation == 0u) {
        generation = ss_manifest_generation(blocks);
        time_sleep_ns(5000000);
    }
    time_sleep_ns(100000000);
    REQUIRE(kill(-child, SIGKILL) == 0);
    int32_t status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    /* timeout itself dies by that signal too. */
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    /* That was timeout. The game under it is its orphan, whose lock on the world goes only when its exit is done, and that takes as long as its memory
     * does to give back. So the group is waited on too, or a busy machine has the save refused as open in another game. */
    const int64_t gone_by = time_now_ns() + 60ll * TIME_NS_PER_SECOND;
    while (kill(-child, 0) == 0 && time_now_ns() < gone_by) {
        time_sleep_ns(5000000);
    }
    if (!CHECK(generation >= 1u)) {
        uint64_t bytes = 0u;
        const uint8_t *const log_bytes = ss_slurp(t, log_path, &bytes);
        printf("%.*s\n", (int32_t)bytes, (const char *)log_bytes);
    }

    /* Read back with no program. */
    save_t *const save = ss_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));
    REQUIRE(save_open(save, world_directory, TERRAIN_SEED_DEFAULT) == SAVE_WORLD_OK);
    generation = save->world.generation;
    CHECK(generation >= 1u);
    CHECK_EQ_U64(save->state.kept, 1u);
    CHECK_EQ_U64(ss_state_files(world_directory), 1u);
    uint64_t bytes = 0u;
    const uint8_t *const file = ss_slurp(t, ss_state_path(t, world_directory, generation), &bytes);
    const save_state_time_t time = ss_file_time(file, bytes);
    float64_t feet[3];
    float32_t yaw = 0.0f32, pitch = 0.0f32;
    REQUIRE(save_state_kept_start(&save->state, feet, &yaw, &pitch));
    /* Saved in play: on an interval, after the digging, with where it looks. */
    CHECK(time.world_ticks >= SAVE_AUTOSAVE_INTERVAL_TICKS);
    CHECK(pitch < -0.5f32 || pitch > 0.5f32);
    save_close(save);
    save_deinit(save);

    /* It runs on from there, and the world it finds has what was dug, read back with nothing past reading. */
    char *const look = ss_join(t, root, "look.script");
    REQUIRE(file_write_atomic(look, SS_LOOK_SCRIPT, strlen(SS_LOOK_SCRIPT)) == FILE_OK);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          saves,
                          (char *)"--script",
                          look,
                          (char *)"--screenshot-dir",
                          root,
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          (char *)"--render-distance",
                          (char *)"4",
                          nullptr};
    test_process_t process;
    test_process_run(t, argv, 120000u, &process);
    REQUIRE(process.started != 0u);
    if (!CHECK_EQ_I64(process.exit_code, 0)) {
        printf("%s\n", process.output);
    }
    CHECK_CONTAINS(process.output, "was read");
    CHECK_CONTAINS(process.output, "the player goes on from");
    CHECK_CONTAINS(process.output, " 0 past reading");
    CHECK_CONTAINS(process.output, " 0 bytes live");
    /* The manifest may have stayed, as nothing dug in those few ticks. */
    save_t *const after = ss_aligned(t, sizeof(save_t));
    REQUIRE(save_init(after));
    REQUIRE(save_open(after, world_directory, TERRAIN_SEED_DEFAULT) == SAVE_WORLD_OK);
    CHECK(after->world.generation >= generation);
    CHECK_EQ_U64(after->state.kept, 1u);
    uint64_t after_bytes = 0u;
    const uint8_t *const after_file =
        ss_slurp(t, ss_state_path(t, world_directory, after->world.generation), &after_bytes);
    const save_state_time_t time_after = ss_file_time(after_file, after_bytes);
    printf("  killed in play: state at %" PRIu64 " ticks at save %" PRIu64 ", %" PRIu64 " ticks after running on\n",
           time.world_ticks, generation, time_after.world_ticks);
    CHECK(time_after.world_ticks > time.world_ticks);
    CHECK(time_after.world_ticks <= time.world_ticks + 20u);
    save_deinit(after);
    ss_temp_remove(t, root);
}

/* The top of the world in the built program, with real keys and real hands. The save puts a stone floor just under the last layer and the player on it,
 * with dirt in hand and looking along +x. Then: looks down at the floor two blocks on, and places on it, which is the last layer. Looks up to that
 * block's top, and places, which is past the last layer. And jumps up onto it, where the body is above the top. */
enum : int32_t {
    SS_TOP_LOOK_FLOOR_COUNTS = 272,
    SS_TOP_LOOK_TOP_COUNTS = 120,
};
static const char SS_TOP_SCRIPT_FORMAT[] = "1 move 0 -40\n"
                                           "+1 button left down\n+1 button left up\n"
                                           "+1 move -20 -122\n"
                                           "+1 button left down\n+1 button left up\n"
                                           "+1 move -112 318\n"
                                           "+1 button left down\n+1 button left up\n"
                                           "+6 screenshot arrived.png\n"
                                           "+1 move 0 %d\n"
                                           "+1 button right down\n"
                                           "+2 button right up\n"
                                           "+10 screenshot placed.png\n"
                                           "+1 move 0 %d\n"
                                           "+1 button right down\n"
                                           "+4 button right up\n"
                                           "+10 screenshot refused.png\n"
                                           "+1 key w down\n"
                                           "+3 key space down\n"
                                           "+6 key space up\n"
                                           "+2 key w up\n"
                                           "+30 screenshot stood.png\n"
                                           "+1 end\n";

TEST(the_built_program_places_at_the_top_of_the_world_and_not_past_it_and_stands_above_it) {
    REQUIRE(t->arg_count >= 1);
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = ss_join(t, root, "saves");
    ui_menu_world_t made;
    REQUIRE(ui_menu_catalog_create(saves, "top", SS_SEED, &made) == FILE_OK);
    char *const world_directory = ss_join(t, saves, made.directory);

    ss_game_rig_t *rig = test_alloc(t, sizeof(ss_game_rig_t));
    REQUIRE(ss_game_begin(t, rig, true));
    REQUIRE(save_open(rig->save, world_directory, SS_SEED) == SAVE_WORLD_OK);
    const game_config_t config = {.render_distance = 4u, .width = 64u, .height = 36u, .show_date = 0u};
    REQUIRE(game_open(rig->game, &config));
    REQUIRE(game_settle(rig->game));
    const int32_t cell_x = (int32_t)floor(rig->player->movement.position[0]);
    const int32_t cell_y = (int32_t)floor(rig->player->movement.position[1]);
    const int32_t top = COLUMN_Z_GAMEPLAY_MAX;
    for (int32_t y = cell_y - 6; y <= cell_y + 6; y++) {
        for (int32_t x = cell_x - 6; x <= cell_x + 6; x++) {
            REQUIRE(world_edit_block_set(&rig->world->edit, x, y, top - 1, BLOCK_ID_STONE) ==
                    WORLD_EDIT_STATUS_CHANGED);
        }
    }
    player_spawn(rig->player, (float64_t)cell_x + 0.5f64, (float64_t)cell_y + 0.5f64, (float64_t)top);
    rig->game->camera.yaw = 0.0f32;
    rig->game->camera.pitch = 0.0f32;
    player_look_send(rig->player, 0.0f32, 0.0f32);
    REQUIRE(inventory_add(&rig->item->inventory, ITEM_ID_DIRT, 8u) == 8u);
    REQUIRE(game_save_finish(rig->game) == SAVE_WORLD_OK);
    game_close(rig->game);
    save_close(rig->save);
    ss_game_end(t, rig);

    char *const play = ss_join(t, root, "top.script");
    char script[sizeof(SS_TOP_SCRIPT_FORMAT) + 32u];
    const int32_t written = snprintf(script, sizeof(script), SS_TOP_SCRIPT_FORMAT, (int32_t)SS_TOP_LOOK_FLOOR_COUNTS,
                                     (int32_t)SS_TOP_LOOK_TOP_COUNTS - (int32_t)SS_TOP_LOOK_FLOOR_COUNTS);
    REQUIRE(written > 0 && (uint64_t)written < sizeof(script));
    REQUIRE(file_write_atomic(play, script, (uint64_t)written) == FILE_OK);
    char *const settings = ss_join(t, root, "settings.yaml");
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          saves,
                          (char *)"--settings",
                          settings,
                          (char *)"--script",
                          play,
                          (char *)"--screenshot-dir",
                          root,
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          (char *)"--render-distance",
                          (char *)"4",
                          nullptr};
    test_process_t process;
    test_process_run(t, argv, 120000u, &process);
    REQUIRE(process.started != 0u);
    if (!CHECK_EQ_I64(process.exit_code, 0)) {
        printf("%s\n", process.output);
    }
    CHECK_CONTAINS(process.output, "the player goes on from");
    CHECK_CONTAINS(process.output, " 0 past reading");

    /* What it left, read back. */
    rig = test_alloc(t, sizeof(ss_game_rig_t));
    REQUIRE(ss_game_begin(t, rig, true));
    REQUIRE(save_open(rig->save, world_directory, SS_SEED) == SAVE_WORLD_OK);
    REQUIRE(game_open(rig->game, &config));
    REQUIRE(game_settle(rig->game));
    uint16_t block = BLOCK_ID_STONE;
    REQUIRE(world_map_block_get(&rig->world->map, cell_x + 2, cell_y, top, &block) == WORLD_MAP_STATUS_OK);
    CHECK_EQ_U64(block, BLOCK_ID_DIRT);
    /* Storage has a layer past it, and nothing the player did reached that. */
    CHECK_EQ_U64(world_map_block_get(&rig->world->map, cell_x + 2, cell_y, top + 1, &block),
                 WORLD_MAP_STATUS_Z_OUT_OF_RANGE);
    const column_t *const column =
        world_map_column_at(&rig->world->map, column_chunk_of(cell_x + 2), column_chunk_of(cell_y));
    REQUIRE(column != nullptr);
    CHECK_EQ_U64(section_block_get(&column->sections[column_section_of(top + 1)],
                                   section_voxel_index(column_local_of(cell_x + 2), column_local_of(cell_y),
                                                       column_section_local_of(top + 1))),
                 BLOCK_ID_AIR);
    /* One went into the world. The second press took nothing from the hand. */
    CHECK_EQ_U64(inventory_count(&rig->item->inventory, ITEM_ID_DIRT), 7u);
    /* On that block, with the feet on a height no block may fill. */
    printf("  top of the world: feet at %.3f, %.3f, %.3f\n", (libc_double_t)rig->player->movement.position[0],
           (libc_double_t)rig->player->movement.position[1], (libc_double_t)rig->player->movement.position[2]);
    CHECK(rig->player->movement.position[2] == (float64_t)(top + 1));
    CHECK(rig->player->movement.position[0] > (float64_t)(cell_x + 1));
    game_close(rig->game);
    save_close(rig->save);
    ss_game_end(t, rig);
    /* What a person reads of it is its four pictures: see the README. */
    if (getenv("MC_TOP_KEEP") != nullptr) {
        printf("  kept in %s\n", root);
    } else {
        ss_temp_remove(t, root);
    }
}

/* Two worlds, as the list sorts them. The first: click its row, Play, and be refused, with the list up again. Then the pointer goes down a row for the second, and Play. */
static const char SS_REFUSED_SCRIPT[] = "1 move 0 -40\n"
                                        "+1 button left down\n+1 button left up\n"
                                        "+1 move -132 196\n"
                                        "+2 screenshot before.png\n"
                                        "+1 move 112 -318\n"
                                        "+1 button left down\n+1 button left up\n"
                                        "+1 move -112 318\n"
                                        "+1 button left down\n+1 button left up\n"
                                        "+2 screenshot refused.png\n"
                                        "+1 move 112 -270\n"
                                        "+1 button left down\n+1 button left up\n"
                                        "+1 move -112 270\n"
                                        "+1 button left down\n+1 button left up\n"
                                        "+6 end\n";

TEST(a_world_refused_at_open_is_said_on_the_list_and_another_can_be_played) {
    REQUIRE(t->arg_count >= 1);
    char *const root = ss_temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = ss_join(t, root, "saves");
    ui_menu_world_t bad, good;
    REQUIRE(ui_menu_catalog_create(saves, "a bad one", 77u, &bad) == FILE_OK);
    REQUIRE(ui_menu_catalog_create(saves, "b good one", 78u, &good) == FILE_OK);
    /* Saved by a later build than this. Whoever has that build wants these bytes back. */
    char *const blocks = ss_join(t, ss_join(t, saves, bad.directory), "blocks");
    REQUIRE(file_make_dirs(blocks) == FILE_OK);
    char *const manifest = ss_join(t, blocks, "manifest");
    static const uint8_t SS_NOT_A_MANIFEST[] = "this is not a manifest, and nothing should change it";
    REQUIRE(file_write_atomic(manifest, SS_NOT_A_MANIFEST, sizeof(SS_NOT_A_MANIFEST)) == FILE_OK);

    char *const script = ss_join(t, root, "refused.script");
    REQUIRE(file_write_atomic(script, SS_REFUSED_SCRIPT, strlen(SS_REFUSED_SCRIPT)) == FILE_OK);
    char *const argv[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          saves,
                          (char *)"--script",
                          script,
                          (char *)"--screenshot-dir",
                          root,
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          (char *)"--render-distance",
                          (char *)"4",
                          nullptr};
    test_process_t process;
    test_process_run(t, argv, 120000u, &process);
    REQUIRE(process.started != 0u);
    if (!CHECK_EQ_I64(process.exit_code, 0)) {
        printf("%s\n", process.output);
    }
    CHECK_CONTAINS(process.output, "world 'a bad one' will not be opened: damaged");
    CHECK_CONTAINS(process.output, "world 'b good one' is open");
    CHECK_CONTAINS(process.output, " 0 bytes live");
    uint64_t bytes = 0u;
    const uint8_t *const kept = ss_slurp(t, manifest, &bytes);
    REQUIRE(bytes == sizeof(SS_NOT_A_MANIFEST));
    CHECK_MEM_EQ(kept, SS_NOT_A_MANIFEST, bytes);
    /* The same screen with the pointer in the same place, nothing chosen in either: all that can differ is what it says. */
    uint64_t before_bytes = 0u, refused_bytes = 0u;
    const uint8_t *const before = ss_slurp(t, ss_join(t, root, "before.png"), &before_bytes);
    const uint8_t *const refused = ss_slurp(t, ss_join(t, root, "refused.png"), &refused_bytes);
    REQUIRE(before_bytes > 0u && refused_bytes > 0u);
    CHECK(before_bytes != refused_bytes || memcmp(before, refused, before_bytes) != 0);
    ss_temp_remove(t, root);
}

TEST_MAIN()
