/* ctest run_failure. See README.md. A process with a CUDA context cannot fork and a card that has faulted stays faulted, so everything on the card happens in a child
 * of its own, forked before CUDA is touched, which reports by its exit status and says why on the same output. */
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

/* ---- What a loss leaves of the books ------------------------------------------------------------------------------------- */

/* A device_memory_t on the real card, and the real fault: what every call says afterwards, and that nothing is left on the books to look like a leak. */
static void card_books_body(test_context_t *t, char *root) {
    (void)root;
    gpu_bridge_t *const bridge = fail_aligned(t, sizeof(gpu_bridge_t));
    REQUIRE(gpu_bridge_init(bridge, gpu_bridge_limits_default()) == GPU_STATUS_OK);
    device_memory_t *const memory = &bridge->device_memory;
    device_block_t kept[4];
    for (uint32_t i = 0u; i < 4u; i++) {
        REQUIRE(device_memory_alloc(memory, DEVICE_POOL_FRAMES, 1u << 20, "kept", &kept[i]) == GPU_STATUS_OK);
    }
    REQUIRE(device_memory_free(memory, &kept[3]) == GPU_STATUS_OK);
    CHECK_EQ_U64(memory->lost, 0u);

    CHECK_EQ_U64(device_memory_fault_now(memory), GPU_STATUS_DEVICE_LOST);
    printf("  the runtime's own words: %s: %s\n", memory->last_error.borrowed_cuda_name,
           memory->last_error.borrowed_cuda_text);
    CHECK_EQ_U64(memory->last_error.status, GPU_STATUS_DEVICE_LOST);
    CHECK_EQ_U64(memory->lost, 1u);
    /* Sticky, as the status says: a write now fails the same way. */
    uint32_t value = 7u;
    const gpu_status_t written = device_memory_write(memory, &kept[0], 0u, &value, sizeof(value));
    printf("  a write after it: %s\n", gpu_status_name(written));
    CHECK(written != GPU_STATUS_OK);
    /* Giving back never fails now, and never asks. */
    CHECK_EQ_U64(device_memory_free(memory, &kept[0]), GPU_STATUS_OK);
    CHECK_EQ_U64(kept[0].owned_address, 0u);
    CHECK_EQ_U64(memory->live_blocks, 2u);
    /* Two still live, as a run that stopped in the middle of things leaves them. They are no leak, with the card gone, and deinit gives them up quietly. */
    CHECK(gpu_bridge_exit_check(bridge));
    gpu_bridge_deinit(bridge);
    heap_counters_t counters;
    heap_counters_read(&counters);
    CHECK_EQ_U64(counters.live_bytes, 0u);
}

TEST(a_real_fault_is_sticky_and_leaves_nothing_on_the_books) {
    fail_in_child(t, card_books_body, FAIL_CARD_SECONDS);
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

/* ---- The card goes ------------------------------------------------------------------------------------------------------- */

static const fail_step_t steps_card_goes[] = {
    MAKE_WORLD_A,
    STEP_PICTURE,
    TICKS(1000000u), /* it never comes: the card goes first */
};

/* The real fault, on the real gpu thread, in the middle of a real game. */
static void card_lost_body(test_context_t *t, char *root) {
    fail_rig_t *const rig = test_alloc(t, sizeof(fail_rig_t));
    if (!fail_rig_begin(t, rig, root)) {
        return;
    }
    game_gpu_pretend_set(rig->game, 30u, false);
    fail_run_t run;
    fail_run_start(&run, t, rig, steps_card_goes, REPLAY_COUNT(steps_card_goes));
    const display_status_t status = game_session_run(rig->session, rig->display, fail_source, &run);
    printf("  frames %" PRIu64 ", ticks %" PRIu64 ", %" PRIu64 " flips\n", run.frames, rig->game->tick_count,
           (uint64_t)rig->card->flips);
    CHECK_EQ_U64(run.timed_out, 0u);
    CHECK_EQ_U64(status, DISPLAY_STATUS_OK); /* the display did all it was asked */
    CHECK_EQ_U64(rig->session->failed, 1u);
    CHECK(game_gpu_lost(rig->game));
    CHECK(!game_gpu_stuck(rig->game));
    CHECK_EQ_U64(rig->bridge->device_memory.lost, 1u);
    CHECK_EQ_U64(atomic_load(&rig->game->gpu_failure), GPU_STATUS_DEVICE_LOST);
    CHECK_EQ_U64(rig->game->open, 0u);
    CHECK_EQ_U64(rig->game->threads_running, 0u);
    fail_console_back(t, rig);
    /* No block counts as live, and it was all given up without a word to the runtime. */
    CHECK_EQ_U64(rig->bridge->device_memory.live_blocks, 0u);
    fail_world_kept(t, rig);
    fail_rig_end(t, rig);
}

TEST(the_card_faulting_under_a_game_saves_the_world_gives_the_console_back_and_ends) {
    fail_in_child(t, card_lost_body, FAIL_CARD_SECONDS);
}

/* A hang, which says nothing: the gpu thread never comes back. Nothing it may be using can be released, so this ends as the program does, by _exit. */
static void card_stuck_body(test_context_t *t, char *root) {
    fail_rig_t *const rig = test_alloc(t, sizeof(fail_rig_t));
    if (!fail_rig_begin(t, rig, root)) {
        return;
    }
    game_gpu_pretend_set(rig->game, 30u, true);
    fail_run_t run;
    fail_run_start(&run, t, rig, steps_card_goes, REPLAY_COUNT(steps_card_goes));
    const int64_t started_ns = time_now_ns();
    const display_status_t status = game_session_run(rig->session, rig->display, fail_source, &run);
    const int64_t took_ns = time_now_ns() - started_ns;
    printf("  over in %.1f s, %" PRIu64 " frames\n", (libc_double_t)time_ns_to_seconds(took_ns), run.frames);
    CHECK_EQ_U64(run.timed_out, 0u);
    CHECK_EQ_U64(status, DISPLAY_STATUS_OK);
    CHECK_EQ_U64(rig->session->failed, 1u);
    CHECK(game_gpu_stuck(rig->game));
    CHECK(game_gpu_lost(rig->game));
    CHECK(took_ns >= 10ll * TIME_NS_PER_SECOND);
    fail_console_back(t, rig);
    /* A save needs nothing that thread has: the sim stopped, and all of it went to disk. */
    CHECK_EQ_U64(rig->game->threads_running, 0u);
    CHECK_EQ_U64(rig->save->autosave.started, 0u);
    CHECK(rig->save->autosave.saves_done >= 1u);
    CHECK(save_autosave_trouble(&rig->save->autosave) == nullptr);
    save_close(rig->save);
    fail_world_kept(t, rig);
    (void)fflush(stdout);
    _exit(t->failure_count != 0u ? 1 : 0);
}

TEST(a_card_that_hangs_and_says_nothing_is_noticed_and_the_world_is_saved_all_the_same) {
    fail_in_child(t, card_stuck_body, FAIL_CARD_SECONDS);
}

/* The built program, all the way out, with no display: the exit status and the log are what a person sees. */
TEST(the_built_program_says_the_card_went_and_a_new_run_goes_on_from_the_world_it_saved) {
    REQUIRE(t->arg_count >= 1);
    char *const root = fail_temp_directory(t);
    REQUIRE(root != nullptr);
    char *const saves = fail_join(t, root, "saves");
    char *const lost[] = {t->borrowed_args[0],
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          saves,
                          (char *)"--settings",
                          fail_join(t, root, "settings.yaml"),
                          (char *)"--script",
                          (char *)"tests/run/failure/fixtures/lost.script",
                          (char *)"--width",
                          (char *)"640",
                          (char *)"--height",
                          (char *)"480",
                          (char *)"--render-distance",
                          (char *)"4",
                          (char *)"--pretend-gpu-lost",
                          (char *)"3",
                          nullptr};
    test_process_t process;
    test_process_run(t, lost, 120000u, &process);
    REQUIRE(process.started != 0u);
    CHECK(process.timed_out == 0u);
    if (!CHECK_EQ_I64(process.exit_code, 9)) { /* MAIN_EXIT_GPU_LOST */
        printf("%s\n", process.output);
    }
    CHECK_CONTAINS(process.output, "cudaError");
    CHECK_CONTAINS(process.output, "session: the card has gone");
    CHECK_CONTAINS(process.output, "The world was saved first");
    CHECK_CONTAINS(process.output, " 0 bytes live");
    CHECK(strstr(process.output, "leak") == nullptr);

    /* Without the flag the same world opens, from where it was. */
    char *const again[] = {t->borrowed_args[0],
                           (char *)"--headless",
                           (char *)"--menu",
                           (char *)"--saves",
                           saves,
                           (char *)"--settings",
                           fail_join(t, root, "settings.yaml"),
                           (char *)"--script",
                           (char *)"tests/run/failure/fixtures/again.script",
                           (char *)"--width",
                           (char *)"640",
                           (char *)"--height",
                           (char *)"480",
                           (char *)"--render-distance",
                           (char *)"4",
                           nullptr};
    test_process_run(t, again, 120000u, &process);
    REQUIRE(process.started != 0u);
    CHECK(process.timed_out == 0u);
    if (!CHECK_EQ_I64(process.exit_code, 0)) {
        printf("%s\n", process.output);
    }
    CHECK_CONTAINS(process.output, "game: the player goes on from");
    fail_temp_remove(t, root);
}

/* ---- Devices go ------------------------------------------------------------------------------------------------------------ */

TEST(what_is_plugged_in_follows_what_goes_and_comes_and_what_was_held_is_let_go) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const mouse = fake_add(fake, "event0", "Mouse", fake_caps_mouse, true);
    fake_device_t *const keyboard = fake_add(fake, "event1", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = fail_aligned(t, sizeof(input_t));
    REQUIRE(input_init(input, FAKE_DIRECTORY, &evdev_ops, fake));
    CHECK_EQ_U64(input_kinds_now(input), INPUT_KIND_KEYBOARD | INPUT_KIND_MOUSE);
    fail_send(keyboard, EV_KEY, KEY_W, 1);
    const input_frame_t *frame = input_poll(input);
    CHECK(input_keys_has(&frame->down, KEY_W));
    /* Pulled out with W held: the read says so, and W is up, in that same frame. */
    keyboard->read_errno = ENODEV;
    frame = input_poll(input);
    CHECK(!input_keys_has(&frame->down, KEY_W));
    CHECK(input_keys_has(&frame->released, KEY_W));
    CHECK_EQ_U64(input_kinds_now(input), INPUT_KIND_MOUSE);
    /* Its node goes, and a new one comes. */
    evdev_record(fake, IN_DELETE, "event1");
    keyboard->listed = 0u;
    (void)input_poll(input);
    CHECK_EQ_U64(input_kinds_now(input), INPUT_KIND_MOUSE);
    keyboard->read_errno = 0;
    keyboard->listed = 1u;
    evdev_record(fake, IN_CREATE, "event1");
    (void)input_poll(input);
    CHECK_EQ_U64(input_kinds_now(input), INPUT_KIND_KEYBOARD | INPUT_KIND_MOUSE);
    mouse->read_errno = EIO; /* any error at all is a device gone */
    (void)input_poll(input);
    CHECK_EQ_U64(input_kinds_now(input), INPUT_KIND_KEYBOARD);
    CHECK_EQ_U64(input->discovery.stats.lost, 2u);
    input_deinit(input);
    CHECK_EQ_U64(fake->unexpected_ioctls, 0u);
    /* The fixture's other helpers, which only their own tests need. */
    (void)fake_caps_touchpad;
    (void)fake_caps_power_button;
    (void)fake_find_call;
}

static bool call_autosave_often(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    /* Between the world opening and its threads, so nothing else is looking. World time is all a save waits for, and these tests have none to spare. */
    fail_rig_t *const rig = run->borrowed_rig;
    save_autosave_interval_set(&rig->save->autosave, 4u);
    rig->save->autosave.next_tick = rig->game->tick_count + 4u;
    return true;
}

static bool call_hold_w(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    fail_send(run->borrowed_rig->keyboard, EV_KEY, KEY_W, 1);
    return true;
}

static void fail_pull_out(fake_device_t *device) {
    device->read_errno = ENODEV;
    device->listed = 0u;
}

static bool call_keyboard_out(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    fail_pull_out(run->borrowed_rig->keyboard);
    return true;
}

static bool call_mouse_out(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    fail_pull_out(run->borrowed_rig->mouse);
    return true;
}

static void fail_replug(fail_rig_t *rig, fake_device_t *device) {
    device->read_errno = 0;
    device->listed = 1u;
    device->queue_head = 0u;
    device->queue_tail = 0u;
    evdev_record(rig->fake, IN_CREATE, device->file_name);
}

static bool call_keyboard_in(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    fail_replug(run->borrowed_rig, run->borrowed_rig->keyboard);
    return true;
}

static bool call_mouse_in(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    fail_replug(run->borrowed_rig, run->borrowed_rig->mouse);
    return true;
}

static bool call_paused_for_the_keyboard(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    test_context_t *const t = run->borrowed_test;
    const fail_rig_t *const rig = run->borrowed_rig;
    CHECK_EQ_U64(rig->session->state, GAME_SESSION_PAUSED);
    CHECK_EQ_U64(rig->session->pauses, 1u);
    CHECK_STR_EQ(rig->ui->menu.message, "The keyboard has gone: plug one in");
    CHECK(atomic_load(&rig->game->paused) != 0u);
    run->ticks_at_step = rig->game->tick_count;
    return true;
}

static bool call_frozen_still_and_said(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    test_context_t *const t = run->borrowed_test;
    const fail_rig_t *const rig = run->borrowed_rig;
    CHECK_EQ_U64(rig->game->tick_count, run->ticks_at_step);
    CHECK_STR_EQ(rig->ui->menu.message, "The keyboard has gone: plug one in");
    return true;
}

static bool call_back_and_still_paused(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    test_context_t *const t = run->borrowed_test;
    const fail_rig_t *const rig = run->borrowed_rig;
    CHECK_EQ_U64(rig->session->state, GAME_SESSION_PAUSED);
    CHECK_STR_EQ(rig->ui->menu.message, "");
    CHECK_EQ_U64(input_kinds_now(rig->input), INPUT_KIND_KEYBOARD | INPUT_KIND_MOUSE);
    return true;
}

static bool call_playing_again(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    test_context_t *const t = run->borrowed_test;
    CHECK_EQ_U64(run->borrowed_rig->session->state, GAME_SESSION_PLAYING);
    return true;
}

static bool call_both_gone_is_said(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    test_context_t *const t = run->borrowed_test;
    const fail_rig_t *const rig = run->borrowed_rig;
    CHECK_EQ_U64(rig->session->state, GAME_SESSION_PAUSED);
    CHECK_EQ_U64(rig->session->pauses, 2u);
    CHECK_STR_EQ(rig->ui->menu.message, "Keyboard and mouse have gone: plug them in");
    CHECK_EQ_U64(input_kinds_now(rig->input), 0u);
    return true;
}

static const fail_step_t steps_unplug[] = {
    MAKE_WORLD_A,
    STEP_PICTURE,
    TICKS(2u),
    CALL(call_hold_w),
    TICKS(2u),
    /* Pulled out with W down, and nobody near Esc. */
    CALL(call_keyboard_out),
    WAIT(10), /* a tick already under way as it paused ends by then */
    CALL(call_paused_for_the_keyboard),
    WAIT(50),
    CALL(call_frozen_still_and_said),
    /* Plugged back in: the words go, and the pause stays for a person to end. */
    CALL(call_keyboard_in),
    WAIT(3),
    CALL(call_back_and_still_paused),
    KEY(KEY_ESC),
    WAIT(3),
    CALL(call_playing_again),
    TICKS(2u),
    /* Now both. */
    CALL(call_keyboard_out),
    CALL(call_mouse_out),
    WAIT(3),
    CALL(call_both_gone_is_said),
    CALL(call_keyboard_in),
    CALL(call_mouse_in),
    WAIT(3),
    CALL(call_back_and_still_paused),
    QUIT_TO_TITLE,
    QUIT_GAME,
};

static void unplug_body(test_context_t *t, char *root) {
    fail_rig_t *const rig = test_alloc(t, sizeof(fail_rig_t));
    if (!fail_rig_begin(t, rig, root)) {
        return;
    }
    fail_run_t run;
    fail_run_start(&run, t, rig, steps_unplug, REPLAY_COUNT(steps_unplug));
    const display_status_t status = game_session_run(rig->session, rig->display, fail_source, &run);
    CHECK_EQ_U64(run.timed_out, 0u);
    CHECK_EQ_U64(status, DISPLAY_STATUS_OK);
    CHECK_EQ_U64(rig->session->failed, 0u);
    CHECK_EQ_U64(rig->session->state, GAME_SESSION_DONE);
    CHECK_EQ_U64(run.step, REPLAY_COUNT(steps_unplug));
    fail_console_back(t, rig);
    fail_world_kept(t, rig);
    fail_rig_end(t, rig);
}

TEST(a_keyboard_or_mouse_pulled_out_stops_the_world_and_says_so_until_it_is_back) {
    fail_in_child(t, unplug_body, FAIL_CARD_SECONDS);
}

static bool call_card_gone(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    fake_card_t *const card = run->borrowed_rig->card;
    card->fail_errno = ENODEV;
    card->fail_skip = 0u;
    card->fail_request = DRM_IOCTL_MODE_PAGE_FLIP;
    return true;
}

static const fail_step_t steps_display_gone[] = {
    MAKE_WORLD_A,         STEP_PICTURE,    TICKS(2u),
    CALL(call_card_gone), TICKS(1000000u), /* it never comes: the run ends first */
};

/* The other kind of device that can go, the screen's. Nothing can be shown, so the run ends, with the world saved. */
static void display_gone_body(test_context_t *t, char *root) {
    fail_rig_t *const rig = test_alloc(t, sizeof(fail_rig_t));
    if (!fail_rig_begin(t, rig, root)) {
        return;
    }
    fail_run_t run;
    fail_run_start(&run, t, rig, steps_display_gone, REPLAY_COUNT(steps_display_gone));
    const display_status_t status = game_session_run(rig->session, rig->display, fail_source, &run);
    CHECK_EQ_U64(run.timed_out, 0u);
    CHECK_EQ_U64(status, DISPLAY_STATUS_ERROR);
    CHECK_EQ_U64(rig->game->open, 0u);
    CHECK(!game_gpu_lost(rig->game));
    CHECK(rig->save->autosave.saves_done >= 1u);
    rig->card->fail_request = 0u; /* so that what is left closes as it should */
    fail_console_back(t, rig);
    fail_world_kept(t, rig);
    fail_rig_end(t, rig);
}

TEST(a_display_that_goes_ends_the_run_with_the_console_back_and_the_world_saved) {
    fail_in_child(t, display_gone_body, FAIL_CARD_SECONDS);
}

/* ---- The disk fills, with no card ------------------------------------------------------------------------------------------ */

typedef struct disk_rig_t {
    test_world_t w;
    player_t *player;
    item_t *item;
    entity_t *entity;
    mob_t *mob;
    mob_spawn_t *spawn;
    item_stack_t *held;
    save_t *save;
    job_system_t *job;
    thread_topology_t *topology;
    save_state_parts_t parts;
} disk_rig_t;
static_assert(sizeof(disk_rig_t) == 24u + 9u * 8u + sizeof(save_state_parts_t));

static bool disk_rig_begin(test_context_t *t, disk_rig_t *rig, const char *directory) {
    memset(rig, 0, sizeof(*rig));
    if (!test_world_begin(t, &rig->w)) {
        return false;
    }
    rig->player = test_world_aligned(t, sizeof(player_t));
    rig->item = test_world_aligned(t, sizeof(item_t));
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    rig->mob = test_world_aligned(t, sizeof(mob_t));
    rig->spawn = test_alloc(t, sizeof(mob_spawn_t));
    rig->held = test_alloc(t, sizeof(item_stack_t));
    rig->save = test_world_aligned(t, sizeof(save_t));
    rig->job = test_world_aligned(t, sizeof(job_system_t));
    rig->topology = test_alloc(t, sizeof(thread_topology_t));
    player_init(rig->player, rig->w.physics);
    if (!CHECK(item_init(rig->item)) || !CHECK(entity_init(rig->entity, rig->w.physics)) ||
        !CHECK(mob_init(rig->mob, nullptr)) || !CHECK(save_init(rig->save)) ||
        !CHECK(thread_topology_read(rig->topology)) || !CHECK(job_init(rig->job, rig->topology)) ||
        !CHECK_EQ_U64(save_open(rig->save, directory, FAIL_SEED), SAVE_WORLD_OK)) {
        return false;
    }
    player_spawn(rig->player, 3.5f64, 4.5f64, 1.0f64);
    item_open(rig->item);
    item_world_begin(rig->item, &rig->w.world->edit);
    entity_open(rig->entity, 41u);
    mob_spawn_start(rig->spawn, &rig->entity->arena_world, 43u);
    mob_open(rig->mob, rig->w.world, 47u);
    rig->parts = (save_state_parts_t){
        .borrowed_player = rig->player,
        .borrowed_item = rig->item,
        .borrowed_entity = rig->entity,
        .borrowed_mob = rig->mob,
        .borrowed_spawn = rig->spawn,
        .borrowed_held = rig->held,
    };
    return true;
}

static void disk_rig_end(disk_rig_t *rig) {
    mob_close(rig->mob);
    mob_deinit(rig->mob);
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    item_close(rig->item);
    item_deinit(rig->item);
    player_deinit(rig->player);
    save_deinit(rig->save);
    job_deinit(rig->job);
    test_world_end(&rig->w);
}

static int32_t disk_quiet_begin(void) {
    return log_set_fd(open("/dev/null", O_WRONLY | O_CLOEXEC));
}

static void disk_quiet_end(int32_t previous) {
    const int32_t quiet = log_set_fd(previous);
    (void)close(quiet);
}

static void disk_step_until_written(save_autosave_t *autosave, world_edit_t *edit, const save_state_parts_t *parts,
                                    uint64_t tick) {
    while (atomic_load_explicit(&autosave->writing, memory_order_acquire) != 0u) {
        time_sleep_ns(100000);
    }
    const uint64_t next = autosave->next_tick;
    autosave->next_tick = UINT64_MAX;
    save_autosave_step(autosave, edit, parts, tick);
    autosave->next_tick = next;
}

/* Each stamp a cell of its own in the first column. */
static void disk_stamp_cell(uint32_t stamp, int32_t *x, int32_t *y, int32_t *z) {
    *x = (int32_t)(stamp & 15u);
    *y = (int32_t)((stamp >> 4u) & 15u);
    *z = 400 + (int32_t)(stamp >> 8u);
}

static void disk_stamp(test_context_t *t, disk_rig_t *rig, uint32_t stamp) {
    int32_t x, y, z;
    disk_stamp_cell(stamp, &x, &y, &z);
    CHECK_EQ_U64(world_edit_block_set(&rig->w.world->edit, x, y, z, BLOCK_ID_STONE), WORLD_EDIT_STATUS_CHANGED);
}

/* Stamps from 1 to through are saved, and no others up to beyond. */
static void disk_stamps_check(test_context_t *t, save_t *save, uint32_t through, uint32_t beyond) {
    const uint64_t voxels = (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS;
    uint16_t *const states = test_alloc(t, voxels * sizeof(uint16_t));
    uint8_t *const scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);
    (void)save_world_column_overlay(&save->world, 0, 0, states, scratch);
    uint32_t wrong = 0u;
    for (uint32_t stamp = 1u; stamp <= beyond; stamp++) {
        int32_t x, y, z;
        disk_stamp_cell(stamp, &x, &y, &z);
        const uint32_t storage = (uint32_t)(z - COLUMN_Z_STORAGE_MIN);
        const uint32_t section = storage / SECTION_SIZE;
        const uint32_t voxel = section_voxel_index((uint32_t)x, (uint32_t)y, storage % SECTION_SIZE);
        const uint16_t found = states[(uint64_t)section * SECTION_VOXELS + voxel];
        wrong += (found == BLOCK_ID_STONE) != (stamp <= through) ? 1u : 0u;
    }
    CHECK_EQ_U64(wrong, 0u);
}

static void disk_real_enospc_body(test_context_t *t, char *root) {
    if (!fail_disk_small(t, root)) {
        return;
    }
    char *const directory = fail_join(t, root, "world");
    disk_rig_t *const rig = test_alloc(t, sizeof(disk_rig_t));
    if (!disk_rig_begin(t, rig, directory)) {
        return;
    }
    save_t *const save = rig->save;
    save_autosave_t *const autosave = &save->autosave;
    world_edit_t *const edit = &rig->w.world->edit;
    char *const blocks = fail_join(t, directory, "blocks");
    save_autosave_start(autosave, rig->job, &save->world, &save->state, 0u);
    save_autosave_interval_set(autosave, 10u);
    autosave->next_tick = 10u;
    CHECK(save_autosave_trouble(autosave) == nullptr);

    /* A first one, into room. */
    disk_stamp(t, rig, 1u);
    rig->parts.time.world_ticks = 10u;
    save_autosave_step(autosave, edit, &rig->parts, 10u);
    disk_step_until_written(autosave, edit, &rig->parts, 10u);
    REQUIRE(save->world.generation == 1u);
    CHECK(save_autosave_trouble(autosave) == nullptr);
    autosave->next_tick = 20u;

    /* The disk is full, for real, and the save goes on trying. Nothing that was saved is touched, and nothing half written stays. */
    fail_disk_fill(root);
    char probe_path[FAIL_PATH_BYTES];
    snprintf(probe_path, sizeof(probe_path), "%s/probe", root);
    const int32_t probe = open(probe_path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (probe >= 0) {
        errno = 0;
        CHECK_EQ_I64(write(probe, "x", 1u), -1);
        CHECK_EQ_I64(errno, ENOSPC); /* that it is the kernel's own word */
        (void)close(probe);
        (void)unlink(probe_path);
    }
    const int32_t quiet = disk_quiet_begin();
    disk_stamp(t, rig, 2u);
    rig->parts.time.world_ticks = 20u;
    save_autosave_step(autosave, edit, &rig->parts, 20u);
    disk_step_until_written(autosave, edit, &rig->parts, 20u);
    CHECK_EQ_U64(autosave->saves_failed, 1u);
    CHECK_EQ_U64(autosave->retry, 1u);
    CHECK_EQ_U64(save->world.generation, 1u);
    const char *const said = save_autosave_trouble(autosave);
    CHECK(said != nullptr);
    CHECK_STR_EQ(said != nullptr ? said : "", file_status_name(FILE_ERROR_DISK_FULL));
    CHECK_EQ_U64(fail_names_ending(blocks, ".tmp"), 0u);
    CHECK_EQ_U64(fail_names_ending(directory, ".tmp"), 0u);

    /* Everything else the game writes says the same, and leaves the same nothing. */
    settings_t *const settings = fail_aligned(t, sizeof(settings_t));
    REQUIRE(settings_init(settings));
    settings_load(settings, fail_join(t, root, "settings.yaml"));
    CHECK_EQ_U64(settings_save(settings), FILE_ERROR_DISK_FULL);
    CHECK_EQ_U64(fail_names_ending(root, ".tmp"), 0u);
    settings_deinit(settings);
    save_t *const other = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(other));
    const save_world_status_t no_new_world = save_open(other, fail_join(t, root, "new_world"), FAIL_SEED);
    CHECK(no_new_world != SAVE_WORLD_OK);
    save_close(other);

    /* Asking does not get a full disk written to any more often. */
    save_autosave_request(autosave);
    for (uint32_t i = 0u; i < 5u; i++) {
        save_autosave_step(autosave, edit, &rig->parts, 25u);
        CHECK_EQ_U64(atomic_load(&autosave->writing), 0u);
    }
    /* And as a world closes: it is told so, what was saved stands, and the world may go on. */
    disk_stamp(t, rig, 3u);
    CHECK_EQ_U64(save_autosave_finish(autosave, edit, &rig->parts), SAVE_WORLD_ERROR_FILE);
    CHECK_EQ_U64(save->world.generation, 1u);
    CHECK(save_autosave_trouble(autosave) != nullptr);
    CHECK_EQ_U64(autosave->started, 0u);
    save_autosave_resume(autosave, 30u);
    CHECK_EQ_U64(autosave->started, 1u);
    autosave->next_tick = 30u;
    save_autosave_step(autosave, edit, &rig->parts, 30u);
    disk_step_until_written(autosave, edit, &rig->parts, 30u);
    CHECK(save_autosave_trouble(autosave) != nullptr);
    disk_quiet_end(quiet);
    autosave->next_tick = 40u;

    /* Someone frees some room: it all goes, the next time it is due, and the words go. */
    fail_disk_free(root);
    disk_stamp(t, rig, 4u);
    rig->parts.time.world_ticks = 40u;
    save_autosave_step(autosave, edit, &rig->parts, 40u);
    disk_step_until_written(autosave, edit, &rig->parts, 40u);
    CHECK(save_autosave_trouble(autosave) == nullptr);
    CHECK(save->world.generation >= 2u);
    REQUIRE(save_autosave_finish(autosave, edit, &rig->parts) == SAVE_WORLD_OK);
    CHECK(save_autosave_trouble(autosave) == nullptr);
    CHECK_EQ_U64(fail_names_ending(blocks, ".tmp"), 0u);

    /* And the new world that could not be made can be now, as nothing was left in its way. */
    CHECK_EQ_U64(save_open(other, fail_join(t, root, "new_world"), FAIL_SEED), SAVE_WORLD_OK);
    save_close(other);
    save_deinit(other);

    /* All four stamps, from the disk, by a new opening. */
    save_close(save);
    REQUIRE(save_open(save, directory, FAIL_SEED) == SAVE_WORLD_OK);
    disk_stamps_check(t, save, 4u, 8u);
    disk_rig_end(rig);
}

TEST(a_disk_that_really_fills_loses_nothing_saved_and_all_of_it_goes_once_there_is_room) {
    fail_in_child(t, disk_real_enospc_body, FAIL_PLAIN_SECONDS);
}

/* ---- The disk fills under a game -------------------------------------------------------------------------------------------- */

static bool call_fill(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    fail_disk_fill(run->borrowed_rig->root);
    return true;
}

static bool call_free(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    fail_disk_free(run->borrowed_rig->root);
    return true;
}

static bool call_wait_trouble(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    return save_autosave_trouble(&run->borrowed_rig->save->autosave) != nullptr;
}

static bool call_wait_no_trouble(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    return save_autosave_trouble(&run->borrowed_rig->save->autosave) == nullptr;
}

/* The band along the top of what was last shown: mostly red, where the world behind it is whatever it is. */
static bool call_notice_shown(fail_run_t *run, const display_buffer_t *back) {
    const uint32_t *const row = (const uint32_t *)(const void *)(back->owned_map + 4u * back->pitch);
    uint32_t red = 0u;
    for (uint32_t x = 0u; x < back->width; x++) {
        const uint32_t pixel = row[x];
        red +=
            ((pixel >> 16u) & 0xFFu) > ((pixel >> 8u) & 0xFFu) + 40u && ((pixel >> 16u) & 0xFFu) > (pixel & 0xFFu) + 40u
                ? 1u
                : 0u;
    }
    run->notice_seen = red > back->width / 2u ? 1u : run->notice_seen;
    return run->notice_seen != 0u || run->frames > 100000u;
}

static bool call_pause_says_not_saving(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    test_context_t *const t = run->borrowed_test;
    const fail_rig_t *const rig = run->borrowed_rig;
    CHECK_EQ_U64(rig->session->state, GAME_SESSION_PAUSED);
    CHECK_STR_EQ(rig->ui->menu.message, "Not saving: disk full");
    CHECK_EQ_U64(rig->ui->menu.leave_offered, 0u);
    return true;
}

static bool call_stayed_and_offers(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    test_context_t *const t = run->borrowed_test;
    const fail_rig_t *const rig = run->borrowed_rig;
    CHECK_EQ_U64(rig->game->open, 1u);
    CHECK_EQ_U64(rig->session->state, GAME_SESSION_PAUSED);
    CHECK_EQ_U64(rig->session->failed, 0u);
    CHECK_STR_EQ(rig->ui->menu.message, "Not saved: disk full. Free some space and quit again");
    CHECK_EQ_U64(rig->ui->menu.leave_offered, 1u);
    CHECK_EQ_U64(rig->game->threads_running, 0u);
    return true;
}

static bool call_on_the_title_screen(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    test_context_t *const t = run->borrowed_test;
    const fail_rig_t *const rig = run->borrowed_rig;
    CHECK_EQ_U64(rig->game->open, 0u);
    CHECK_EQ_U64(rig->session->state, GAME_SESSION_MENU);
    CHECK_EQ_U64(rig->session->failed, 0u);
    return true;
}

static const fail_step_t steps_disk_full[] = {
    MAKE_WORLD_A,
    STEP_OPEN,
    CALL(call_autosave_often),
    STEP_PICTURE,
    TICKS(8u),
    /* It fills in play: the game goes on, and says so on the picture. */
    CALL(call_fill),
    CALL(call_wait_trouble),
    WAIT(5),
    CALL(call_notice_shown),
    KEY(KEY_ESC),
    WAIT(3),
    CALL(call_pause_says_not_saving),
    /* Leaving will not lose it behind a person's back. */
    QUIT_TO_TITLE,
    CALL(call_stayed_and_offers),
    /* Back to playing: the threads come back, and the autosave with them. */
    MOVE(SMALL_MIDDLE_X, SMALL_RESUME_Y),
    CLICK,
    TICKS(2u),
    /* Room again, and it goes by itself. */
    CALL(call_free),
    CALL(call_wait_no_trouble),
    KEY(KEY_ESC),
    WAIT(3),
    QUIT_TO_TITLE, /* and now it leaves */
    CALL(call_on_the_title_screen),

    /* Again, and this time freeing nothing: the way out, on purpose. */
    PLAY_WORLD_A,
    STEP_OPEN,
    CALL(call_autosave_often),
    STEP_PICTURE,
    TICKS(2u),
    CALL(call_fill),
    CALL(call_wait_trouble),
    KEY(KEY_ESC),
    WAIT(3),
    QUIT_TO_TITLE,
    CALL(call_stayed_and_offers),
    MOVE(SMALL_MIDDLE_X, SMALL_LEAVE_Y),
    CLICK,
    WAIT(3),
    CALL(call_on_the_title_screen),
    CALL(call_free),
    QUIT_GAME,
};

static void disk_full_game_body(test_context_t *t, char *root) {
    if (!fail_disk_small(t, root)) {
        return;
    }
    fail_rig_t *const rig = test_alloc(t, sizeof(fail_rig_t));
    if (!fail_rig_begin(t, rig, root)) {
        return;
    }
    fail_run_t run;
    fail_run_start(&run, t, rig, steps_disk_full, REPLAY_COUNT(steps_disk_full));
    const display_status_t status = game_session_run(rig->session, rig->display, fail_source, &run);
    printf("  frames %" PRIu64 ", step %" PRIu32 " of %" PRIu32 "\n", run.frames, run.step,
           REPLAY_COUNT(steps_disk_full));
    CHECK_EQ_U64(run.timed_out, 0u);
    CHECK_EQ_U64(status, DISPLAY_STATUS_OK);
    CHECK_EQ_U64(rig->session->failed, 0u);
    CHECK_EQ_U64(rig->session->state, GAME_SESSION_DONE);
    CHECK_EQ_U64(run.step, REPLAY_COUNT(steps_disk_full));
    CHECK_EQ_U64(run.notice_seen, 1u);
    CHECK_EQ_U64(rig->session->worlds_opened, 2u);
    fail_console_back(t, rig);
    CHECK_EQ_U64(fail_names_ending(fail_join(t, rig->saves, "a"), ".tmp"), 0u);
    fail_world_kept(t, rig);
    fail_rig_end(t, rig);
}

TEST(a_disk_that_fills_under_a_game_is_said_on_the_screen_and_nobody_is_held_there) {
    fail_in_child(t, disk_full_game_body, FAIL_CARD_SECONDS);
}

/* ---- Signals in the middle of a save ------------------------------------------------------------------------------------------ */

static void fail_on_signal(int32_t signal_number) {
    (void)signal_number;
}

typedef struct fail_storm_t {
    pthread_t target;
    _Atomic uint32_t stop;
    uint8_t _pad0[4];
} fail_storm_t;
static_assert(sizeof(fail_storm_t) == 16);

static void *fail_storm_thread(void *argument) {
    fail_storm_t *const storm = argument;
    while (atomic_load(&storm->stop) == 0u) {
        (void)pthread_kill(storm->target, SIGUSR1);
        (void)pthread_kill(storm->target, SIGUSR2);
        time_sleep_ns(100000);
    }
    return nullptr;
}

/* A VT switch asked for during a save is two signals into whatever thread has the disk. With no SA_RESTART, harder than the guard's own, no call may take one for a failure. */
TEST(the_signals_of_a_vt_switch_do_not_fail_a_save) {
    char *const root = fail_temp_directory(t);
    REQUIRE(root != nullptr);
    disk_rig_t *const rig = test_alloc(t, sizeof(disk_rig_t));
    REQUIRE(disk_rig_begin(t, rig, fail_join(t, root, "world")));
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_handler = fail_on_signal;
    struct sigaction saved_1;
    struct sigaction saved_2;
    REQUIRE(sigaction(SIGUSR1, &action, &saved_1) == 0);
    REQUIRE(sigaction(SIGUSR2, &action, &saved_2) == 0);
    fail_storm_t storm = {.target = pthread_self(), .stop = 0u, ._pad0 = {0}};
    pthread_t thread;
    REQUIRE(pthread_create(&thread, nullptr, fail_storm_thread, &storm) == 0);
    save_autosave_t *const autosave = &rig->save->autosave;
    save_autosave_start(autosave, rig->job, &rig->save->world, &rig->save->state, 0u);
    uint32_t stamps = 0u;
    for (uint32_t round = 0u; round < FAIL_SIGNALS_PER_SAVE / 10u; round++) {
        stamps++;
        disk_stamp(t, rig, stamps);
        CHECK_EQ_U64(save_autosave_finish(autosave, &rig->w.world->edit, &rig->parts), SAVE_WORLD_OK);
        save_autosave_resume(autosave, round);
    }
    atomic_store(&storm.stop, 1u);
    REQUIRE(pthread_join(thread, nullptr) == 0);
    save_autosave_stop(autosave);
    REQUIRE(sigaction(SIGUSR1, &saved_1, nullptr) == 0);
    REQUIRE(sigaction(SIGUSR2, &saved_2, nullptr) == 0);
    disk_stamps_check(t, rig->save, stamps, stamps + 8u);
    disk_rig_end(rig);
    fail_temp_remove(t, root);
}

/* ---- A VT switch before the console has been taken --------------------------------------------------------------------------- */

typedef struct vt_source_t {
    fake_card_t *borrowed_card;
    display_t *borrowed_display;
    uint64_t frames;
    uint64_t frames_wanted;
    uint64_t discards;
} vt_source_t;
static_assert(sizeof(vt_source_t) == 40);

static bool vt_source_frame(void *context, display_buffer_t *back) {
    (void)back;
    vt_source_t *const source = context;
    source->frames++;
    return source->frames < source->frames_wanted;
}

static void vt_source_discard(void *context) {
    vt_source_t *const source = context;
    source->discards++;
}

typedef struct vt_coming_back_t {
    fake_card_t *borrowed_card;
    display_vt_guard_t *borrowed_guard;
    int64_t after_ns;
    int32_t signal_number; /* what to send once time is up */
    uint32_t minor;        /* what to put on screen first */
    uint32_t kd_mode_seen;
    uint8_t saw_away;
    uint8_t _pad0[3];
} vt_coming_back_t;
static_assert(sizeof(vt_coming_back_t) == 40);

/* The person, on another VT, who is coming back. The guard has to have heard nothing and done nothing by then. */
static void *vt_coming_back_thread(void *argument) {
    vt_coming_back_t *const back = argument;
    const int64_t started_ns = time_now_ns();
    while (time_now_ns() - started_ns < back->after_ns) {
        time_sleep_ns(1000000);
    }
    back->saw_away = back->borrowed_guard->away;
    back->kd_mode_seen = (uint32_t)back->borrowed_card->kd_mode;
    back->borrowed_card->active_vt = back->minor;
    (void)kill(getpid(), back->signal_number);
    return nullptr;
}

TEST(with_another_vt_showing_the_console_waits_and_is_taken_on_coming_back) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    display_t *const display = fail_aligned(t, sizeof(display_t));
    const display_config_t config = fake_config();
    REQUIRE(display_init(display, &config, &fake_ops, card) == DISPLAY_STATUS_OK);
    /* A world took its time opening, or the notice was up, and the person went to look at something else. */
    card->active_vt = card->console_minor + 1u;
    vt_source_t source = {.borrowed_card = card, .borrowed_display = display, .frames_wanted = 20u};
    vt_coming_back_t coming = {.borrowed_card = card,
                               .borrowed_guard = &display->guard,
                               .after_ns = 300000000,
                               .signal_number = SIGUSR2,
                               .minor = card->console_minor};
    pthread_t thread;
    REQUIRE(pthread_create(&thread, nullptr, vt_coming_back_thread, &coming) == 0);
    const display_status_t status = display_frames_run(display, vt_source_frame, vt_source_discard, &source);
    REQUIRE(pthread_join(thread, nullptr) == 0);
    CHECK_EQ_U64(status, DISPLAY_STATUS_OK);
    /* Until then it was off the card and off the screen. */
    CHECK_EQ_U64(coming.saw_away, 1u);
    CHECK_EQ_U64(coming.kd_mode_seen, KD_TEXT);
    const int32_t dropped = fake_find_call(card, DRM_IOCTL_DROP_MASTER, 0u);
    const int32_t answered = fake_find_call(card, VT_RELDISP, VT_ACKACQ);
    const int32_t graphics = fake_find_call(card, KDSETMODE, KD_GRAPHICS);
    CHECK(dropped >= 0);
    CHECK(answered > dropped);
    CHECK(graphics > answered);
    CHECK_EQ_U64(source.frames, 20u);
    CHECK_EQ_U64(source.discards, 1u);
    CHECK_EQ_I64(display->guard.guardian_pid, 0);
    CHECK_EQ_I64(card->kd_mode, KD_TEXT);
    display_deinit(display);
    check_nothing_left(t, card);
}

TEST(stopped_on_another_vt_before_ever_taking_the_console_it_leaves_all_as_it_was) {
    fake_card_t *const card = fake_card_new(t);
    fake_card_use_small_mode(card);
    display_t *const display = fail_aligned(t, sizeof(display_t));
    const display_config_t config = fake_config();
    REQUIRE(display_init(display, &config, &fake_ops, card) == DISPLAY_STATUS_OK);
    card->active_vt = card->console_minor + 1u;
    vt_source_t source = {.borrowed_card = card, .borrowed_display = display, .frames_wanted = 20u};
    vt_coming_back_t coming = {.borrowed_card = card,
                               .borrowed_guard = &display->guard,
                               .after_ns = 200000000,
                               .signal_number = SIGTERM,
                               .minor = card->console_minor + 1u};
    pthread_t thread;
    REQUIRE(pthread_create(&thread, nullptr, vt_coming_back_thread, &coming) == 0);
    const display_status_t status = display_frames_run(display, vt_source_frame, vt_source_discard, &source);
    REQUIRE(pthread_join(thread, nullptr) == 0);
    CHECK_EQ_U64(status, DISPLAY_STATUS_OK);
    CHECK_EQ_U64(source.frames, 0u);
    CHECK_EQ_I64(fake_find_call(card, KDSETMODE, KD_GRAPHICS), -1);
    CHECK_EQ_I64(display->guard.guardian_pid, 0);
    display_deinit(display);
    check_nothing_left(t, card);
}

/* ---- A VT switch while a world opens ------------------------------------------------------------------------------------------ */

/* Takes the place of what the session told the game to call, and asks for the switch from there, so that it falls in the middle of opening whatever the machine's speed. */
static void vt_alive_hook(void *context) {
    fail_run_t *const run = context;
    fail_rig_t *const rig = run->borrowed_rig;
    fake_card_t *const card = rig->card;
    display_vt_guard_t *const guard = &rig->display->guard;
    run->alive_calls++;
    /* The first alive call of the world's opening. */
    if (run->alive_script != 0u && run->alive_calls == 1u) {
        card->vt_release_pending = 1u;
        raise(SIGUSR1);
    }
    run->saved_alive(run->saved_alive_context);
    if (run->alive_script == 0u) {
        return;
    }
    if (run->alive_calls == 1u) {
        /* Let go of already, with the world not yet open: that is the whole point. */
        run->away_in_open = guard->away;
        run->answered_in_open = fake_find_call(card, VT_RELDISP, 1u) >= 0 ? 1u : 0u;
        /* What is typed on the other VT. It must not count on this side, Esc above all. */
        fail_send(rig->keyboard, EV_KEY, KEY_ESC, 1);
        fail_send(rig->keyboard, EV_KEY, KEY_ESC, 0);
    }
    if (run->alive_script == 1u && run->alive_calls == 2u) {
        card->active_vt = card->console_minor;
        raise(SIGUSR2); /* before the world has finished opening */
    }
    if (run->alive_script == 1u && run->alive_calls == 3u) {
        run->back_in_open = guard->away == 0u ? 1u : 0u;
    }
}

static bool call_vt_hook_set(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    game_t *const game = run->borrowed_rig->game;
    if (run->saved_alive == nullptr) {
        run->saved_alive = game->open_alive;
        run->saved_alive_context = game->borrowed_open_alive_context;
    }
    game_open_alive_set(game, vt_alive_hook, run);
    run->borrowed_rig->card->vt_release_stays = 1u;
    run->alive_calls = 0u;
    run->alive_script = 1u;
    return true;
}

static bool call_vt_second_script(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    run->alive_calls = 0u;
    run->alive_script = 2u;
    run->away_in_open = 0u;
    return true;
}

static bool call_first_opening_checked(fail_run_t *run, const display_buffer_t *back) {
    (void)back;
    test_context_t *const t = run->borrowed_test;
    const fail_rig_t *const rig = run->borrowed_rig;
    CHECK_EQ_U64(run->away_in_open, 1u);
    CHECK_EQ_U64(run->answered_in_open, 1u);
    CHECK_EQ_U64(run->back_in_open, 1u);
    CHECK(run->alive_calls >= 3u);
    CHECK_EQ_U64(rig->display->guard.away, 0u);
    /* That Esc was typed at something else. */
    CHECK_EQ_U64(rig->session->pauses, 0u);
    CHECK_EQ_U64(rig->session->state, GAME_SESSION_PLAYING);
    return true;
}

static const fail_step_t steps_vt[] = {
    /* The hook goes in while nothing is opening. */
    WAIT(3),
    CALL(call_vt_hook_set),
    MOVE(SMALL_MIDDLE_X, SMALL_SINGLEPLAYER_Y),
    CLICK,
    WAIT(2),
    MOVE(SMALL_CREATE_NEW_X, SMALL_BUTTONS_Y),
    CLICK,
    WAIT(2),
    KEY(KEY_A),
    WAIT(2),
    KEY(KEY_ENTER),
    STEP_PICTURE,
    TICKS(2u),
    CALL(call_first_opening_checked),
    KEY(KEY_ESC),
    WAIT(3),
    QUIT_TO_TITLE,
    /* The second time it stays away past the end of opening, and nothing goes to the card until the person comes back. */
    CALL(call_vt_second_script),
    PLAY_WORLD_A,
    STEP_PICTURE,
    TICKS(2u),
    KEY(KEY_ESC),
    WAIT(3),
    QUIT_TO_TITLE,
    QUIT_GAME,
};

typedef struct vt_second_back_t {
    fail_run_t *borrowed_run;
    _Atomic uint32_t stop;
    uint32_t flips_while_away;
    uint8_t saw_open_away;
    uint8_t _pad0[7];
} vt_second_back_t;
static_assert(sizeof(vt_second_back_t) == 24);

/* The person on the other VT, the second time: stays until the world has opened and well past it, and watches that nothing touched the card meanwhile. */
static void *vt_second_back_thread(void *argument) {
    vt_second_back_t *const back = argument;
    fail_run_t *const run = back->borrowed_run;
    fail_rig_t *const rig = run->borrowed_rig;
    while (atomic_load(&back->stop) == 0u) {
        if (run->alive_script == 2u && rig->display->guard.away != 0u && rig->game->open != 0u) {
            back->saw_open_away = 1u;
            const uint64_t flips = rig->card->flips;
            time_sleep_ns(300000000);
            back->flips_while_away = (uint32_t)(rig->card->flips - flips);
            run->alive_script = 3u; /* the hook has no more to do */
            rig->card->active_vt = rig->card->console_minor;
            (void)kill(getpid(), SIGUSR2);
            return nullptr;
        }
        time_sleep_ns(1000000);
    }
    return nullptr;
}

static void vt_during_load_body(test_context_t *t, char *root) {
    fail_rig_t *const rig = test_alloc(t, sizeof(fail_rig_t));
    if (!fail_rig_begin(t, rig, root)) {
        return;
    }
    fail_run_t run;
    fail_run_start(&run, t, rig, steps_vt, REPLAY_COUNT(steps_vt));
    vt_second_back_t second = {.borrowed_run = &run};
    pthread_t thread;
    REQUIRE(pthread_create(&thread, nullptr, vt_second_back_thread, &second) == 0);
    const display_status_t status = game_session_run(rig->session, rig->display, fail_source, &run);
    atomic_store(&second.stop, 1u);
    REQUIRE(pthread_join(thread, nullptr) == 0);
    printf("  frames %" PRIu64 ", step %" PRIu32 " of %" PRIu32 ", %" PRIu64 " flips\n", run.frames, run.step,
           REPLAY_COUNT(steps_vt), (uint64_t)rig->card->flips);
    CHECK_EQ_U64(run.timed_out, 0u);
    CHECK_EQ_U64(status, DISPLAY_STATUS_OK);
    CHECK_EQ_U64(rig->session->failed, 0u);
    CHECK_EQ_U64(rig->session->state, GAME_SESSION_DONE);
    CHECK_EQ_U64(run.step, REPLAY_COUNT(steps_vt));
    CHECK_EQ_U64(rig->session->worlds_opened, 2u);
    CHECK_EQ_U64(second.saw_open_away, 1u);
    CHECK_EQ_U64(second.flips_while_away, 0u);
    fail_console_back(t, rig);
    fail_world_kept(t, rig);
    fail_rig_end(t, rig);
}

TEST(a_vt_switch_while_a_world_opens_is_answered_at_once_and_the_world_goes_on_opening) {
    fail_in_child(t, vt_during_load_body, FAIL_CARD_SECONDS);
}

TEST_MAIN()
