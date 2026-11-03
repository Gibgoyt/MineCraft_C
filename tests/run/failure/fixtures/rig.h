/* What the tests of run_failure share: small things, a tmpfs of its own, the whole game on fake kernels and the real threads, and steps a frame at a time. Only index.c includes it.
 * Each test that uses the card or a mount does so in a child: see index.c. */
#ifndef MC_TESTS_RUN_FAILURE_FIXTURES_RIG_H
#define MC_TESTS_RUN_FAILURE_FIXTURES_RIG_H

#include "harness/index.h"

/* Both fake kernels. Their helpers share names, so the one for input is renamed coming in. */
#include "run/display/mode/fixtures/fake_card.h"
#define fake_close_fd evdev_close_fd
#define fake_ioctl_call evdev_ioctl_call
#define fake_read_fd evdev_read_fd
#define fake_record evdev_record
#define fake_ops evdev_ops
#include "run/input/fixtures/fake_evdev.h"
#undef fake_close_fd
#undef fake_ioctl_call
#undef fake_read_fd
#undef fake_record
#undef fake_ops
#include "run/input/fixtures/replay.h"
#include "run/physics/fixtures/test_world.h"

#include "entity/index.h"
#include "file/index.h"
#include "game/index.h"
#include "game/session/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "item/index.h"
#include "job/index.h"
#include "log/index.h"
#include "mob/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "save/index.h"
#include "settings/index.h"
#include "tables/blocks/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/index.h"
#include "world/index.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

enum : uint32_t {
    FAIL_PATH_BYTES = 512u,
    FAIL_FILLER_CHUNK = 1u << 20,
    FAIL_CARD_SECONDS = 150u, /* a child on the card ends by SIGALRM past this, which fails its test */
    FAIL_PLAIN_SECONDS = 60u,
    FAIL_SIGNALS_PER_SAVE = 200u,
};
static const uint64_t FAIL_SEED = 4242u;
static const char FAIL_DISK_OPTIONS[] = "size=48m";

/* The layout at 320 by 200, by hand from run/src/ui/menu/index.c, as in tests/run/ui/menu/. */
enum : int32_t {
    SMALL_MIDDLE_X = 160,
    SMALL_MIDDLE_Y = 100,
    SMALL_SINGLEPLAYER_Y = 85,
    SMALL_QUIT_Y = 133,
    SMALL_RESUME_Y = 85,
    SMALL_TITLE_Y = 133,
    SMALL_LEAVE_Y = 157, /* a fourth button, one more 24 down */
    SMALL_CREATE_NEW_X = 226,
    SMALL_PLAY_X = 94,
    SMALL_BUTTONS_Y = 158,
    SMALL_ROW_X = 150,
    SMALL_ROW_0_Y = 39,
};

/* ---- Small things ------------------------------------------------------------------------------------------------------ */

static char *fail_join(test_context_t *t, const char *left, const char *right) {
    char *const path = test_alloc(t, FAIL_PATH_BYTES);
    snprintf(path, FAIL_PATH_BYTES, "%s/%s", left, right);
    return path;
}

static char *fail_temp_directory(test_context_t *t) {
    char *const directory = test_alloc(t, FAIL_PATH_BYTES);
    snprintf(directory, FAIL_PATH_BYTES, "/tmp/mc_failure_XXXXXX");
    return mkdtemp(directory);
}

static void fail_temp_remove(test_context_t *t, char *directory) {
    if (directory == nullptr || strncmp(directory, "/tmp/mc_failure_", 16u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static uint32_t fail_names_ending(const char *directory, const char *ending) {
    DIR *const handle = opendir(directory);
    if (handle == nullptr) {
        return 0u;
    }
    uint32_t count = 0u;
    const uint64_t ending_bytes = strlen(ending);
    for (struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
        const uint64_t bytes = strlen(entry->d_name);
        count += bytes >= ending_bytes && strcmp(entry->d_name + bytes - ending_bytes, ending) == 0 ? 1u : 0u;
    }
    (void)closedir(handle);
    return count;
}

static void *fail_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

typedef void (*fail_child_t)(test_context_t *t, char *root);

/* body in a child of its own, on a root that goes away after it. */
static void fail_in_child(test_context_t *t, fail_child_t body, uint32_t seconds) {
    char *const root = fail_temp_directory(t);
    REQUIRE(root != nullptr);
    (void)fflush(stdout);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        alarm(seconds);
        body(t, root);
        (void)fflush(stdout);
        _exit(t->failure_count != 0u ? 1 : 0);
    }
    int32_t status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    if (!CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        printf("  the child's status was %" PRId32 "\n", status);
    }
    fail_temp_remove(t, root);
}

/* ---- A disk that really fills ------------------------------------------------------------------------------------------ */

/* A file system of FAIL_DISK_OPTIONS at directory, for this process and its children only, so that the kernel's own ENOSPC is what comes back. A user namespace asks nothing of
 * anyone, and everything in it goes when the process does. Before any threads: unshare refuses a process that has them. */
static bool fail_disk_small(test_context_t *t, const char *directory) {
    const uint32_t uid = getuid();
    const uint32_t gid = getgid();
    if (!CHECK_EQ_I64(unshare(CLONE_NEWUSER | CLONE_NEWNS), 0)) {
        printf("  unshare: %s\n", strerror(errno));
        return false;
    }
    char text[64];
    const int32_t groups = open("/proc/self/setgroups", O_WRONLY | O_CLOEXEC);
    if (groups >= 0) {
        (void)!write(groups, "deny", 4u);
        (void)close(groups);
    }
    int32_t map = open("/proc/self/uid_map", O_WRONLY | O_CLOEXEC);
    int32_t bytes = snprintf(text, sizeof(text), "%" PRIu32 " %" PRIu32 " 1\n", uid, uid);
    if (!CHECK(map >= 0) || !CHECK_EQ_I64(write(map, text, (uint64_t)bytes), bytes)) {
        return false;
    }
    (void)close(map);
    map = open("/proc/self/gid_map", O_WRONLY | O_CLOEXEC);
    bytes = snprintf(text, sizeof(text), "%" PRIu32 " %" PRIu32 " 1\n", gid, gid);
    if (!CHECK(map >= 0) || !CHECK_EQ_I64(write(map, text, (uint64_t)bytes), bytes)) {
        return false;
    }
    (void)close(map);
    if (!CHECK_EQ_I64(mount("tmpfs", directory, "tmpfs", MS_NOSUID | MS_NODEV, FAIL_DISK_OPTIONS), 0)) {
        printf("  mount: %s\n", strerror(errno));
        return false;
    }
    return true;
}

/* Takes all there is. The name is what fail_disk_free removes. */
static void fail_disk_fill(const char *root) {
    char path[FAIL_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/filler", root);
    const int32_t fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        return;
    }
    static uint8_t zeros[FAIL_FILLER_CHUNK];
    for (uint64_t chunk = FAIL_FILLER_CHUNK; chunk != 0u; chunk /= 2u) {
        while (write(fd, zeros, chunk) == (int64_t)chunk) {
        }
    }
    (void)close(fd);
}

static void fail_disk_free(const char *root) {
    char path[FAIL_PATH_BYTES];
    snprintf(path, sizeof(path), "%s/filler", root);
    (void)unlink(path);
}

/* ---- The whole game, on fake kernels and the real threads ------------------------------------------------------------------ */

typedef struct fail_rig_t {
    gpu_bridge_t *bridge;
    job_system_t *job;
    world_t *world;
    terrain_t *terrain;
    render_t *render;
    texture_t *texture;
    game_t *game;
    physics_t *physics;
    player_t *player;
    entity_t *entity;
    item_t *item;
    mob_t *mob;
    save_t *save;
    ui_t *ui;
    thread_topology_t *topology;
    fake_card_t *card;
    display_t *display;
    fake_evdev_t *fake;
    fake_device_t *mouse;
    fake_device_t *keyboard;
    input_t *input;
    game_session_t *session;
    const char *root;
    const char *saves;
} fail_rig_t;
static_assert(sizeof(fail_rig_t) == 24u * 8u);

static bool fail_rig_begin(test_context_t *t, fail_rig_t *rig, const char *root) {
    memset(rig, 0, sizeof(*rig));
    rig->root = root;
    rig->saves = fail_join(t, root, "saves");
    rig->bridge = fail_aligned(t, sizeof(gpu_bridge_t));
    rig->job = fail_aligned(t, sizeof(job_system_t));
    rig->world = fail_aligned(t, sizeof(world_t));
    rig->terrain = fail_aligned(t, sizeof(terrain_t));
    rig->render = fail_aligned(t, sizeof(render_t));
    rig->texture = fail_aligned(t, sizeof(texture_t));
    rig->game = fail_aligned(t, sizeof(game_t));
    rig->physics = fail_aligned(t, sizeof(physics_t));
    rig->player = fail_aligned(t, sizeof(player_t));
    rig->entity = fail_aligned(t, sizeof(entity_t));
    rig->item = fail_aligned(t, sizeof(item_t));
    rig->mob = fail_aligned(t, sizeof(mob_t));
    rig->save = fail_aligned(t, sizeof(save_t));
    rig->ui = fail_aligned(t, sizeof(ui_t));
    rig->display = fail_aligned(t, sizeof(display_t));
    rig->input = fail_aligned(t, sizeof(input_t));
    rig->session = test_alloc(t, sizeof(game_session_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    if (!CHECK_EQ_U64(gpu_bridge_init(rig->bridge, gpu_bridge_limits_default()), GPU_STATUS_OK) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK(texture_init(rig->texture)) || !CHECK(world_init(rig->world, world_limits_default())) ||
        !CHECK(terrain_init(rig->terrain, TERRAIN_SEED_DEFAULT)) || !CHECK(physics_init(rig->physics))) {
        return false;
    }
    player_init(rig->player, rig->physics);
    if (!CHECK(entity_init(rig->entity, rig->physics)) || !CHECK(item_init(rig->item)) ||
        !CHECK(mob_init(rig->mob, nullptr)) || !CHECK(save_init(rig->save)) ||
        !CHECK(render_init(rig->render, &rig->bridge->device_memory, rig->job, render_limits_default())) ||
        !CHECK(ui_init(rig->ui, rig->texture))) {
        return false;
    }
    game_init(rig->game, rig->world, rig->terrain, rig->render, rig->job, rig->texture, rig->topology);
    game_ui_set(rig->game, rig->ui);
    game_player_set(rig->game, rig->player);
    game_entity_set(rig->game, rig->entity);
    game_item_set(rig->game, rig->item);
    game_mob_set(rig->game, rig->mob);
    game_save_set(rig->game, rig->save);

    rig->card = fake_card_new(t);
    fake_card_use_small_mode(rig->card);
    const display_config_t config = fake_config();
    if (!CHECK_EQ_U64(display_init(rig->display, &config, &fake_ops, rig->card), DISPLAY_STATUS_OK)) {
        return false;
    }
    rig->fake = fake_new(t);
    rig->mouse = fake_add(rig->fake, "event0", "Mouse", fake_caps_mouse, true);
    rig->keyboard = fake_add(rig->fake, "event1", "Keyboard", fake_caps_keyboard, true);
    if (!CHECK(input_init(rig->input, FAKE_DIRECTORY, &evdev_ops, rig->fake))) {
        return false;
    }
    const display_buffer_t *const buffer = &rig->display->flip.buffers[0];
    const game_config_t small = {.render_distance = 4u,
                                 .width = buffer->width,
                                 .height = buffer->height,
                                 .features_off = SHADE_FEATURE_DAY_CYCLE | SHADE_FEATURE_AO | SHADE_FEATURE_TAA,
                                 ._pad0 = {0}};
    game_session_init(rig->session, rig->game, rig->ui, rig->terrain, rig->input, &small, rig->saves, FAIL_SEED);
    return true;
}

/* What is left once the session has closed its world, in the order of main. Not for a card that is stuck: a thread is still in there. */
static void fail_rig_end(test_context_t *t, fail_rig_t *rig) {
    game_session_deinit(rig->session);
    input_deinit(rig->input);
    display_deinit(rig->display);
    game_deinit(rig->game);
    save_deinit(rig->save);
    ui_deinit(rig->ui);
    render_deinit(rig->render);
    mob_deinit(rig->mob);
    item_deinit(rig->item);
    entity_deinit(rig->entity);
    player_deinit(rig->player);
    physics_deinit(rig->physics);
    CHECK(gpu_bridge_exit_check(rig->bridge));
    terrain_deinit(rig->terrain);
    world_deinit(rig->world);
    texture_deinit(rig->texture);
    job_deinit(rig->job);
    gpu_bridge_deinit(rig->bridge);
    check_nothing_left(t, rig->card);
}

/* The world "a" as it is on the disk now, with nothing of the game's: what a person starting the program again would find. */
static void fail_world_kept(test_context_t *t, fail_rig_t *rig) {
    save_t *const save = rig->save;
    const save_world_status_t opened = save_open(save, fail_join(t, rig->saves, "a"), FAIL_SEED);
    if (!CHECK_EQ_U64(opened, SAVE_WORLD_OK)) {
        return;
    }
    float64_t feet[3] = {0.0f64, 0.0f64, 0.0f64};
    float32_t yaw = 0.0f32, pitch = 0.0f32;
    CHECK(save_state_kept_start(&save->state, feet, &yaw, &pitch));
    save_close(save);
}

/* ---- Steps a frame at a time ------------------------------------------------------------------------------------------- */

typedef struct fail_run_t fail_run_t;
typedef bool (*fail_call_t)(fail_run_t *run, const display_buffer_t *back);

typedef enum fail_step_kind_t : uint8_t {
    STEP_WAIT = 0,
    STEP_MOVE = 1,
    STEP_CLICK = 2,
    STEP_KEY = 3,
    STEP_WAIT_OPEN = 4,    /* the frame the world opened in: its threads have not started */
    STEP_WAIT_PICTURE = 5, /* a picture of it has been shown */
    STEP_WAIT_TICKS = 6,
    STEP_CALL = 7, /* until call says it is over */
} fail_step_kind_t;

typedef struct fail_step_t {
    fail_call_t call;
    uint32_t value;
    int32_t x;
    int32_t y;
    uint8_t kind;
    uint8_t _pad0[3];
} fail_step_t;
static_assert(sizeof(fail_step_t) == 24);

#define WAIT(frames) {.kind = STEP_WAIT, .value = (frames)}
#define MOVE(at_x, at_y) {.kind = STEP_MOVE, .x = (at_x), .y = (at_y)}
#define CLICK {.kind = STEP_CLICK}
#define KEY(code) {.kind = STEP_KEY, .value = (code)}
#define CALL(function) {.kind = STEP_CALL, .call = (function)}
#define TICKS(count) {.kind = STEP_WAIT_TICKS, .value = (count)}
#define STEP_OPEN {.kind = STEP_WAIT_OPEN}
#define STEP_PICTURE {.kind = STEP_WAIT_PICTURE}
#define MAKE_WORLD_A                                                                                                   \
    WAIT(3), MOVE(SMALL_MIDDLE_X, SMALL_SINGLEPLAYER_Y), CLICK, WAIT(2), MOVE(SMALL_CREATE_NEW_X, SMALL_BUTTONS_Y),    \
        CLICK, WAIT(2), KEY(KEY_A), WAIT(2), KEY(KEY_ENTER)
#define PLAY_WORLD_A                                                                                                   \
    MOVE(SMALL_MIDDLE_X, SMALL_SINGLEPLAYER_Y), CLICK, WAIT(2), MOVE(SMALL_ROW_X, SMALL_ROW_0_Y), CLICK, WAIT(2),      \
        MOVE(SMALL_PLAY_X, SMALL_BUTTONS_Y), CLICK
#define QUIT_TO_TITLE MOVE(SMALL_MIDDLE_X, SMALL_TITLE_Y), CLICK, WAIT(3)
#define QUIT_GAME MOVE(SMALL_MIDDLE_X, SMALL_QUIT_Y), CLICK

typedef struct fail_run_t {
    test_context_t *borrowed_test;
    fail_rig_t *borrowed_rig;
    const fail_step_t *borrowed_steps;
    void (*saved_alive)(void *context);
    void *saved_alive_context;
    int64_t started_ns;
    uint64_t frames;
    uint64_t ticks_at_step;
    uint32_t step_count;
    uint32_t step;
    uint32_t wait;
    int32_t pointer_x;
    int32_t pointer_y;
    uint32_t phase;
    uint32_t alive_calls;
    uint32_t alive_script; /* which of the VT scripts the alive hook is running: 0 for none */
    uint8_t step_begun;
    uint8_t timed_out;
    uint8_t notice_seen;
    uint8_t away_in_open;     /* the guard had let go, inside the frame the world opened in */
    uint8_t back_in_open;     /* and had come back, in that same frame */
    uint8_t answered_in_open; /* VT_RELDISP(1) was sent before the world finished opening */
    uint8_t _pad0[2];
} fail_run_t;
static_assert(sizeof(fail_run_t) == 104);

static void fail_send(fake_device_t *device, uint16_t type, uint16_t code, int32_t value) {
    const struct input_event events[] = {REPLAY_EVENT(type, code, value), REPLAY_SYN};
    fake_replay(device, events, REPLAY_COUNT(events));
}

static bool fail_step_do(fail_run_t *run, const fail_step_t *step, const display_buffer_t *back) {
    fail_rig_t *const rig = run->borrowed_rig;
    switch ((fail_step_kind_t)step->kind) {
    case STEP_WAIT:
        if (run->step_begun == 0u) {
            run->wait = step->value;
        }
        return run->wait-- == 0u;
    case STEP_MOVE:
        fail_send(rig->mouse, EV_REL, REL_X, step->x - run->pointer_x);
        fail_send(rig->mouse, EV_REL, REL_Y, step->y - run->pointer_y);
        run->pointer_x = step->x;
        run->pointer_y = step->y;
        return true;
    case STEP_CLICK:
    case STEP_KEY: {
        fake_device_t *const device = step->kind == STEP_CLICK ? rig->mouse : rig->keyboard;
        const uint16_t code = step->kind == STEP_CLICK ? (uint16_t)BTN_LEFT : (uint16_t)step->value;
        fail_send(device, EV_KEY, code, run->phase == 0u ? 1 : 0);
        run->phase = run->phase == 0u ? 1u : 0u;
        return run->phase == 0u;
    }
    case STEP_WAIT_OPEN:
        return rig->game->open != 0u;
    case STEP_WAIT_PICTURE:
        return rig->game->open != 0u && rig->game->pictures_shown > 0u;
    case STEP_WAIT_TICKS:
        if (run->step_begun == 0u) {
            run->ticks_at_step = rig->game->tick_count;
        }
        return rig->game->tick_count >= run->ticks_at_step + step->value;
    case STEP_CALL:
        return step->call(run, back);
    }
    return true;
}

static bool fail_source(void *context, display_buffer_t *back) {
    fail_run_t *const run = context;
    run->frames++;
    if (!game_session_source(run->borrowed_rig->session, back)) {
        return false;
    }
    if (time_now_ns() - run->started_ns > 120ll * TIME_NS_PER_SECOND) {
        run->timed_out = 1u;
        return false;
    }
    if (run->step < run->step_count) {
        const bool over = fail_step_do(run, &run->borrowed_steps[run->step], back);
        run->step_begun = (uint8_t)(over ? 0u : 1u);
        if (over) {
            run->step++;
        }
    }
    time_sleep_ns(1000000);
    return true;
}

static void fail_run_start(fail_run_t *run, test_context_t *t, fail_rig_t *rig, const fail_step_t *steps,
                           uint32_t step_count) {
    memset(run, 0, sizeof(*run));
    run->borrowed_test = t;
    run->borrowed_rig = rig;
    run->borrowed_steps = steps;
    run->step_count = step_count;
    run->started_ns = time_now_ns();
    run->pointer_x = SMALL_MIDDLE_X;
    run->pointer_y = SMALL_MIDDLE_Y;
}

static void fail_console_back(test_context_t *t, const fail_rig_t *rig) {
    CHECK_EQ_I64(rig->display->guard.guardian_pid, 0);
    CHECK_EQ_I64(rig->card->kd_mode, KD_TEXT);
}

#endif
